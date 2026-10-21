import 'dart:convert';
import 'dart:io';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/services.dart';
import 'package:path/path.dart' as p;

import '../models/console.dart';
import '../models/game.dart';
import 'core_manager.dart';
import 'disc_sniffer.dart';
import 'emulator_installer.dart';
import 'external_catalog.dart';
import 'external_support.dart';
import 'library.dart';
import 'paths.dart';

ExternalSupport createExternalSupport() => NativeExternalSupport();

/// Desktop: emulators are found on disk and started as child processes
/// with the command lines from external_catalog.dart.
/// Android: games are linked through the system file picker (persistent
/// read access) or found in game folders, and handed to the emulator with
/// an intent carrying a one-time read grant (see MainActivity.kt).
class NativeExternalSupport extends ExternalSupport {
  static const _channel = MethodChannel('romrunner/external');
  Map<String, dynamic>? _state;

  @override
  Future<bool> hasBuiltIn(Console c) async =>
      c.hasInHouse && await CoreManager.instance.locate(c, core: c.inHouse) != null;

  @override
  HostOs? get host => Platform.isWindows
      ? HostOs.windows
      : Platform.isMacOS
          ? HostOs.macos
          : Platform.isLinux
              ? HostOs.linux
              : Platform.isAndroid
                  ? HostOs.android
                  : null;

  bool get _android => Platform.isAndroid;

  @override
  bool get canBrowse => host != null && !_android;

  // ---- persisted state: <app support>/externals.json ----

  Future<File> _file(String name) async => File(p.join((await AppPaths.get()).root, name));

  Future<Map<String, dynamic>> _load() async {
    if (_state != null) return _state!;
    try {
      final f = await _file('externals.json');
      if (f.existsSync()) {
        _state = jsonDecode(await f.readAsString()) as Map<String, dynamic>;
      } else {
        _state = await _migrateSwitchJson();
      }
    } catch (_) {
      // Corrupt file: start over rather than break the library.
    }
    _state ??= {};
    _state!['emulators'] ??= <String, dynamic>{};
    _state!['games'] ??= <dynamic>[];
    return _state!;
  }

  /// Earlier builds stored only Switch settings in switch.json.
  Future<Map<String, dynamic>?> _migrateSwitchJson() async {
    final old = await _file('switch.json');
    if (!old.existsSync()) return null;
    final s = jsonDecode(await old.readAsString()) as Map<String, dynamic>;
    final emu = s['emulator'];
    return {
      'emulators': {if (emu is Map) Console.nswitch.slug: emu},
      'games': [
        for (final g in (s['games'] as List? ?? const []))
          if (g is Map) {...g, 'console': Console.nswitch.slug},
      ],
    };
  }

  Future<void> _save() async {
    final f = await _file('externals.json');
    await f.writeAsString(const JsonEncoder.withIndent('  ').convert(_state));
  }

  List<Map<String, dynamic>> _entries(Map<String, dynamic> s) =>
      (s['games'] as List).whereType<Map>().map((e) => e.cast<String, dynamic>()).toList();

  Console? _bySlug(String? slug) {
    for (final c in Console.values) {
      if (c.slug == slug) return c;
    }
    return null;
  }

  Game? _toGame(Map<String, dynamic> e) {
    final c = _bySlug(e['console'] as String?);
    final name = e['name'] as String?, path = e['path'] as String?;
    if (c == null || name == null || path == null) return null;
    return Game(title: Game.titleFromFile(name), console: c, fileName: name, path: path);
  }

  Future<List<Game>> _link(List<({String name, String path, Console console})> files) async {
    final s = await _load();
    final entries = _entries(s);
    final added = <Game>[];
    for (final f in files) {
      entries.removeWhere((e) => e['path'] == f.path);
      final e = {'name': f.name, 'path': f.path, 'console': f.console.slug};
      entries.add(e);
      added.add(_toGame(e)!);
    }
    s['games'] = entries;
    await _save();
    return added;
  }

  // ---- emulator choice ----

  @override
  Future<EmulatorChoice?> current(Console c) async =>
      EmulatorChoice.fromJson(((await _load())['emulators'] as Map)[c.slug]);

  @override
  Future<void> choose(Console c, EmulatorChoice choice) async {
    ((await _load())['emulators'] as Map)[c.slug] = choice.toJson();
    await _save();
  }

  @override
  Future<List<EmulatorChoice>> detect(Console c) async {
    final os = host;
    if (os == null) return const [];
    final emulators = emulatorsFor(c, os);
    if (_android) return _detectAndroid(c, emulators);
    final found = <EmulatorChoice>[];
    final seen = <String>{};
    for (final e in emulators) {
      for (final path in _findOnDisk(e, os)) {
        if (seen.add(path)) found.add(EmulatorChoice(path, e.name, detail: path, emulator: e.id));
      }
    }
    return found;
  }

  Future<List<EmulatorChoice>> _detectAndroid(Console c, List<ExternalEmulator> emulators) async {
    final packages = [for (final e in emulators) for (final a in e.android) a.package];
    final installed = await _channel.invokeListMethod<Map>('installed', {'packages': packages}) ?? const [];
    final byPackage = {for (final m in installed) m['id'] as String: m['label'] as String};
    final out = <EmulatorChoice>[
      for (final e in emulators)
        for (final a in e.android)
          if (byPackage[a.package] != null)
            EmulatorChoice(a.package, byPackage[a.package]!, detail: a.package, emulator: e.id),
    ];
    if (c == Console.nswitch) {
      // Other Yuzu-family forks that open content:// game files.
      final generic = await _channel.invokeListMethod<Map>('switchHandlers') ?? const [];
      for (final m in generic) {
        final id = m['id'] as String;
        if (out.every((o) => o.id != id)) out.add(EmulatorChoice(id, m['label'] as String, detail: id));
      }
    }
    return out;
  }

  /// Executables for [e] on this computer (PATH, common folders, AppImages,
  /// Flatpaks, /Applications).
  List<String> _findOnDisk(ExternalEmulator e, HostOs os) {
    final home = Platform.environment['HOME'] ?? Platform.environment['USERPROFILE'] ?? '';
    final hits = <String>[];
    void tryFile(String path) {
      if (path.isNotEmpty && File(path).existsSync() && !hits.contains(path)) hits.add(path);
    }

    final pathDirs = (Platform.environment['PATH'] ?? '').split(os == HostOs.windows ? ';' : ':');
    switch (os) {
      case HostOs.macos:
        for (final dir in ['/Applications', p.join(home, 'Applications')]) {
          final d = Directory(dir);
          if (!d.existsSync()) continue;
          for (final entry in d.listSync()) {
            final n = p.basename(entry.path).toLowerCase();
            if (e.macApps.any((a) => a.toLowerCase() == n) && !hits.contains(entry.path)) hits.add(entry.path);
          }
        }
      case HostOs.windows:
        for (final dir in pathDirs) {
          for (final n in e.windows) {
            tryFile(p.join(dir, n));
          }
        }
        // Portable emulators usually live one folder down in these places.
        final env = Platform.environment;
        final roots = [
          env['ProgramFiles'],
          env['ProgramFiles(x86)'],
          if (env['LOCALAPPDATA'] != null) p.join(env['LOCALAPPDATA']!, 'Programs'),
          p.join(home, 'Emulators'),
          p.join(home, 'Desktop'),
          p.join(home, 'Downloads'),
          r'C:\Emulators',
        ];
        for (final root in roots.whereType<String>()) {
          final d = Directory(root);
          if (!d.existsSync()) continue;
          try {
            for (final sub in d.listSync().whereType<Directory>()) {
              for (final n in e.windows) {
                tryFile(p.join(sub.path, n));
              }
            }
          } catch (_) {
            // Unreadable folder; skip it.
          }
        }
      case HostOs.linux:
        for (final dir in pathDirs) {
          for (final n in e.linux) {
            tryFile(p.join(dir, n));
          }
        }
        for (final id in e.flatpaks) {
          tryFile('/var/lib/flatpak/exports/bin/$id');
          tryFile(p.join(home, '.local/share/flatpak/exports/bin', id));
        }
        for (final dir in ['Applications', 'AppImages', '.local/bin', 'bin', 'Downloads', '']) {
          final d = Directory(p.join(home, dir));
          if (!d.existsSync()) continue;
          try {
            for (final f in d.listSync().whereType<File>()) {
              final n = p.basename(f.path).toLowerCase();
              if (n.endsWith('.appimage') && e.linuxAppImages.any((a) => n.startsWith(a.toLowerCase()))) tryFile(f.path);
            }
          } catch (_) {}
        }
      case HostOs.android:
        break;
    }
    return hits;
  }

  @override
  Future<EmulatorChoice?> browse(Console c) async {
    if (!canBrowse) return null;
    final picked = await FilePicker.pickFiles(dialogTitle: 'Choose your ${c.fullName} emulator', type: FileType.any);
    final path = picked.isEmpty ? null : picked.first.path;
    if (path == null) return null;
    final e = guessEmulator(c, path);
    return EmulatorChoice(path, e?.name ?? p.basenameWithoutExtension(path), detail: path, emulator: e?.id);
  }

  // ---- games ----

  @override
  Future<List<Game>> addGames() async {
    final files = <({String name, String path})>[];
    if (_android) {
      final list = await _channel.invokeListMethod<Map>('pickGames') ?? const [];
      files.addAll([for (final m in list) (name: m['name'] as String, path: m['uri'] as String)]);
    } else {
      final picked = await FilePicker.pickFiles(dialogTitle: 'Add games (linked, not copied)', type: FileType.any);
      files.addAll([for (final f in picked) if (f.path != null) (name: f.name, path: f.path!)]);
    }
    final keep = <({String name, String path, Console console})>[];
    for (final f in files) {
      // Android content:// URIs can't be sniffed; rely on the extension.
      final c = await identify(_android ? f.name : f.path, sniff: _android ? null : sniffDisc);
      if (c != null && c.launchesExternally && supports(c)) {
        keep.add((name: f.name, path: f.path, console: c));
      } else if (_android) {
        await _channel.invokeMethod('release', {'uri': f.path});
      }
    }
    return _link(keep);
  }

  @override
  Future<List<Game>> linkFiles(Map<String, Console> pathToConsole) => _link([
        for (final MapEntry(key: path, value: c) in pathToConsole.entries) (name: p.basename(path), path: path, console: c),
      ]);

  @override
  Future<List<Game>> linkedGames() async => [for (final e in _entries(await _load())) ?_toGame(e)];

  @override
  Future<void> forget(Game game) async {
    final s = await _load();
    s['games'] = _entries(s)..removeWhere((e) => e['path'] == game.path);
    await _save();
    if (_android && game.path != null && game.path!.startsWith('content:')) {
      await _channel.invokeMethod('release', {'uri': game.path});
    }
  }

  // ---- launching ----

  Future<String?> _titleId(String path) async {
    if (path.startsWith('content:')) return null;
    final lines = const LineSplitter().convert(await File(path).readAsString());
    return lines.map((l) => l.trim()).firstWhere((l) => l.isNotEmpty, orElse: () => '');
  }

  @override
  Future<void> launch(Game game) async {
    final c = game.console;
    final os = host;
    if (os == null || !supports(c)) throw ExternalLaunchException(unsupportedReason(c));
    var choice = await current(c);
    if (choice == null) {
      // Nothing chosen yet: use the first installed emulator we can find.
      final found = await detect(c);
      if (found.isNotEmpty) {
        choice = found.first;
        await choose(c, choice);
      }
    }
    if (choice == null) {
      final auto = installable(c);
      throw ExternalLaunchException(
          auto.isNotEmpty
              ? '${c.fullName} needs ${auto.first.name}, which isn\'t installed yet.'
              : 'No ${c.fullName} emulator found. Install ${emulatorNames(c, os)}, or choose it in Settings.',
          needsSetup: auto.isEmpty,
          installable: auto.isEmpty ? null : auto.first);
    }
    final chosen = choice;
    final path = game.path!;
    final emulator = emulatorById(chosen.emulator ?? '');
    final injected = emulator?.injectsTitleId == true ? await _titleId(path) : null;

    if (_android) {
      final spec = emulator?.android.where((a) => a.package == chosen.id).firstOrNull ??
          AndroidLaunch(chosen.id, action: 'android.intent.action.VIEW');
      try {
        await _channel.invokeMethod('launch', {
          'spec': chosen.id == ExternalSupport.chooseEachTime
              ? AndroidLaunch(ExternalSupport.chooseEachTime, action: 'android.intent.action.VIEW').toMap()
              : spec.toMap(),
          if (path.startsWith('content:')) 'uri': path else 'path': path,
          'inject': ?injected,
        });
      } on PlatformException catch (e) {
        throw ExternalLaunchException(e.message ?? 'Could not open ${chosen.label}.', needsSetup: e.code == 'not_found');
      }
      return;
    }

    if (!File(path).existsSync() && !Directory(path).existsSync()) {
      throw ExternalLaunchException('Can\'t find ${game.fileName}. It may have been moved, or its drive is disconnected.');
    }
    var exe = chosen.id;
    if (os == HostOs.macos && exe.toLowerCase().endsWith('.app')) {
      exe = _macBinary(exe) ?? exe;
    }
    if (!File(exe).existsSync()) {
      throw ExternalLaunchException('${chosen.label} isn\'t at ${chosen.id} anymore. Choose it again in Settings.',
          needsSetup: true);
    }
    final args = emulator?.argsFor(os, rom: path, injected: injected) ?? [path];
    try {
      // Freshly downloaded AppImages aren't executable yet.
      if (os == HostOs.linux && File(exe).statSync().mode & 0x49 == 0) {
        await Process.run('chmod', ['+x', exe]);
      }
      await Process.start(exe, args,
          mode: ProcessStartMode.detached,
          workingDirectory: p.dirname(exe),
          environment: os == HostOs.linux && exe.toLowerCase().endsWith('.appimage') && !_hasFuse2()
              ? {'APPIMAGE_EXTRACT_AND_RUN': '1'} // AppImages need FUSE 2 otherwise
              : null);
    } on ProcessException catch (e) {
      throw ExternalLaunchException('Could not start ${chosen.label}: ${e.message}');
    }
  }

  static bool _hasFuse2() => const [
        '/usr/lib/x86_64-linux-gnu/libfuse.so.2',
        '/lib/x86_64-linux-gnu/libfuse.so.2',
        '/usr/lib/aarch64-linux-gnu/libfuse.so.2',
        '/usr/lib/libfuse.so.2',
        '/usr/lib64/libfuse.so.2',
      ].any((f) => File(f).existsSync());

  /// The executable inside a macOS .app (CFBundleExecutable).
  String? _macBinary(String app) {
    final plist = File(p.join(app, 'Contents', 'Info.plist'));
    if (plist.existsSync()) {
      final m = RegExp(r'<key>CFBundleExecutable</key>\s*<string>([^<]+)</string>').firstMatch(plist.readAsStringSync());
      if (m != null) {
        final bin = p.join(app, 'Contents', 'MacOS', m.group(1)!);
        if (File(bin).existsSync()) return bin;
      }
    }
    final dir = Directory(p.join(app, 'Contents', 'MacOS'));
    if (!dir.existsSync()) return null;
    final files = dir.listSync().whereType<File>().toList();
    return files.isEmpty ? null : files.first.path;
  }

  // ---- automatic install ----

  @override
  List<ExternalEmulator> installable(Console c) {
    final os = host;
    if (os == null) return const [];
    return [for (final e in emulatorsFor(c, os)) if (EmulatorInstaller.instance.canInstall(e)) e];
  }

  @override
  Future<EmulatorChoice?> install(ExternalEmulator e, Console c, {void Function(String, double?)? onProgress}) =>
      EmulatorInstaller.instance.install(e, c, onProgress: onProgress);

  @override
  String homepage(ExternalEmulator e) => EmulatorInstaller.instance.homepage(e);
}
