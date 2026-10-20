import '../models/console.dart';
import '../models/game.dart';
import 'external_catalog.dart';
import 'external_support_native.dart' if (dart.library.js_interop) 'external_support_web.dart' as impl;

/// An installed emulator app chosen for a system.
///
/// [id] is the executable / .app path on desktop, the package name on
/// Android, or [ExternalSupport.chooseEachTime]. [emulator] is the catalog
/// id (see external_catalog.dart), used to pick the right arguments.
class EmulatorChoice {
  const EmulatorChoice(this.id, this.label, {this.detail, this.emulator});
  final String id;
  final String label;
  final String? detail;
  final String? emulator;

  Map<String, String> toJson() => {'id': id, 'label': label, 'detail': ?detail, 'emulator': ?emulator};

  static EmulatorChoice? fromJson(Object? json) {
    if (json is! Map) return null;
    final id = json['id'], label = json['label'];
    if (id is! String || label is! String) return null;
    final detail = json['detail'], emulator = json['emulator'];
    return EmulatorChoice(id, label,
        detail: detail is String ? detail : null, emulator: emulator is String ? emulator : null);
  }
}

class ExternalLaunchException implements Exception {
  ExternalLaunchException(this.message, {this.needsSetup = false, this.installable});
  final String message;

  /// True when the fix is to pick an emulator in Settings.
  final bool needsSetup;

  /// No emulator is installed, but RomRunner can download this one.
  final ExternalEmulator? installable;
  @override
  String toString() => message;
}

/// Systems RomRunner doesn't emulate itself (PS2, Switch, Xbox ...) are
/// linked, never copied (games are often many GB), and launched in the
/// emulator app the user picked for that system.
abstract class ExternalSupport {
  static final ExternalSupport instance = impl.createExternalSupport();

  /// Android only: show the system "Open with" sheet every time.
  static const chooseEachTime = '*';

  /// The platform's catalog key; null where external emulators can't be
  /// launched at all (iOS, web).
  HostOs? get host;

  /// Systems that can be opened in another app on this device.
  List<Console> get systems => [
        for (final c in Console.values)
          if (c.launchesExternally && host != null && (emulatorsFor(c, host!).isNotEmpty || allowsAnyApp(c))) c,
      ];

  /// Systems that are external but have no emulator for this platform.
  List<Console> get unavailable => [
        for (final c in Console.values)
          if (c.launchesExternally && !systems.contains(c)) c,
      ];

  /// Android: any app that opens the file may be used (system picker).
  bool allowsAnyApp(Console c) => host == HostOs.android;

  bool supports(Console c) => systems.contains(c);

  String unsupportedReason(Console c) {
    if (host == null) {
      return '${c.fullName} games open in a separate emulator app, which isn\'t possible on this platform.';
    }
    final where = [
      for (final os in HostOs.values)
        if ((externalCatalog[c] ?? const []).any((e) => e.availableOn(os))) _osName(os),
    ];
    return where.isEmpty
        ? 'No ${c.fullName} emulator is supported yet.'
        : 'No ${c.fullName} emulator for this platform. Available on ${where.join(', ')}.';
  }

  static String _osName(HostOs os) => switch (os) {
        HostOs.windows => 'Windows',
        HostOs.macos => 'macOS',
        HostOs.linux => 'Linux',
        HostOs.android => 'Android',
      };

  /// Desktop: the user can browse for the emulator executable / .app.
  bool get canBrowse;

  Future<EmulatorChoice?> current(Console c);
  Future<void> choose(Console c, EmulatorChoice choice);

  /// Installed emulators for [c] found on this device.
  Future<List<EmulatorChoice>> detect(Console c);

  /// Desktop only: file dialog to pick the emulator for [c].
  Future<EmulatorChoice?> browse(Console c);

  /// Opens a picker, works out each file's system and links the ones that
  /// belong to external systems. Returns the games added.
  Future<List<Game>> addGames();

  /// Links files already picked elsewhere (desktop Add ROMs).
  Future<List<Game>> linkFiles(Map<String, Console> pathToConsole);

  Future<List<Game>> linkedGames();
  Future<void> forget(Game game);

  /// Throws [ExternalLaunchException] with a user-facing message on failure.
  Future<void> launch(Game game);

  // ---- automatic install from official releases ----

  /// Emulators for [c] RomRunner can download on this platform.
  List<ExternalEmulator> installable(Console c) => const [];

  /// Downloads and installs [e] for [c]. Desktop: returns the new choice
  /// (already selected). Android: returns null after opening the system
  /// installer. [onProgress] gets a status line and a 0..1 fraction or null.
  Future<EmulatorChoice?> install(ExternalEmulator e, Console c, {void Function(String, double?)? onProgress}) =>
      throw UnsupportedError('Automatic install isn\'t available here.');

  /// Official site of [e], shown next to downloads.
  String homepage(ExternalEmulator e) => '';
}

/// What a system needs from the user's own console besides the emulator.
/// RomRunner can't (and won't) download these.
String? consoleFilesNote(Console c) => switch (c) {
      Console.ps2 => 'PCSX2 needs a PS2 BIOS dumped from your console; it asks for it on first start.',
      Console.xbox => 'xemu needs the MCPX boot ROM, flash BIOS and hard-disk image from your Xbox.',
      Console.nswitch => 'Switch emulators need prod.keys and firmware dumped from your Switch.',
      Console.ps3 => 'RPCS3 needs the PS3 system software, which Sony offers free on playstation.com; RPCS3 walks you through installing it.',
      Console.wiiu => 'Encrypted Wii U games need keys.txt from your console; decrypted games work without it.',
      Console.n3ds => '3DS games must be decrypted dumps from your own console.',
      Console.vita => 'Vita3K needs the Vita firmware (free from Sony) and games installed into Vita3K.',
      _ => null,
    };
