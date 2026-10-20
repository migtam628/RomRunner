import '../models/game.dart';
import 'switch_support_native.dart' if (dart.library.js_interop) 'switch_support_web.dart' as impl;

/// An installed Switch emulator RomRunner can hand games to.
///
/// [id] is an executable or .app path on desktop, a package name on Android,
/// or [SwitchSupport.chooseEachTime].
class EmulatorChoice {
  const EmulatorChoice(this.id, this.label, {this.detail});
  final String id;
  final String label;
  final String? detail;

  Map<String, String> toJson() => {'id': id, 'label': label, 'detail': ?detail};
  static EmulatorChoice? fromJson(Object? json) {
    if (json is! Map) return null;
    final id = json['id'], label = json['label'];
    if (id is! String || label is! String) return null;
    final detail = json['detail'];
    return EmulatorChoice(id, label, detail: detail is String ? detail : null);
  }
}

class SwitchLaunchException implements Exception {
  SwitchLaunchException(this.message, {this.needsSetup = false});
  final String message;

  /// True when the fix is to pick an emulator in Settings.
  final bool needsSetup;
  @override
  String toString() => message;
}

/// Switch games are not emulated by RomRunner. They are linked (never copied:
/// they are often 5-20 GB) and launched in the emulator the user picked.
abstract class SwitchSupport {
  static final SwitchSupport instance = impl.createSwitchSupport();

  /// Android-only option: show the system "Open with" sheet every time.
  static const chooseEachTime = '*';

  bool get supported;
  String get unsupportedReason;

  /// Desktop: the user can browse for the emulator executable / .app.
  bool get canBrowse;

  Future<EmulatorChoice?> current();
  Future<void> choose(EmulatorChoice choice);

  /// Installed emulators found on this device.
  Future<List<EmulatorChoice>> detect();

  /// Desktop only: file dialog to pick the emulator.
  Future<EmulatorChoice?> browse();

  /// Opens a picker for .nsp/.xci/.nro files and links them.
  Future<List<Game>> addGames();

  /// Links files already picked elsewhere (desktop paths).
  Future<List<Game>> linkFiles(List<String> paths);

  Future<List<Game>> linkedGames();
  Future<void> forget(Game game);

  /// Throws [SwitchLaunchException] with a user-facing message on failure.
  Future<void> launch(Game game);
}

/// Launch arguments for desktop emulators.
/// Yuzu-family (Eden, Citron, Sudachi, Suyu): `-f -g <game>`.
/// Ryujinx-family (Ryujinx, Ryubing): `<game> --fullscreen`.
List<String> switchLaunchArgs(String emulatorPath, String gamePath) {
  final name = emulatorPath.replaceAll('\\', '/').split('/').where((s) => s.isNotEmpty).last.toLowerCase();
  if (name.contains('ryujinx') || name.contains('ryubing')) return [gamePath, '--fullscreen'];
  return ['-f', '-g', gamePath];
}
