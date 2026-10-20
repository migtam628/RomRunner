import 'dart:convert';
import 'dart:io';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/services.dart';
import 'package:path/path.dart' as p;

import '../models/console.dart';
import '../models/game.dart';
import 'paths.dart';
import 'switch_support.dart';

SwitchSupport createSwitchSupport() => NativeSwitchSupport();

/// Name fragments of known Switch emulators (Yuzu and Ryujinx families).
const _hints = ['eden', 'citron', 'ryujinx', 'ryubing', 'sudachi', 'suyu', 'yuzu', 'kenji', 'torzu', 'sumi'];

/// Desktop: games are linked by file path and launched as a child process.
/// Android: games are picked through the system file picker with persistent
/// read access, then handed to the emulator with an ACTION_VIEW intent that
/// grants it read access to that one file (see MainActivity.kt).
class NativeSwitchSupport implements SwitchSupport {
  static const _channel = MethodChannel('romrunner/switch');
  Map<String, dynamic>? _state;

  bool get _android => Platform.isAndroid;
  bool get _desktop => Platform.isWindows || Platform.isMacOS || Platform.isLinux;

  @override
  bool get supported => _android || _desktop;

  @override
  String get unsupportedReason => Platform.isIOS
      ? 'iOS doesn\'t allow the JIT compiler that Switch emulators need, so Switch games can\'t run here.'
      : 'Switch games aren\'t supported on this platform.';

  @override
  bool get canBrowse => _desktop;

  // ---- persisted state: <app support>/switch.json ----

  Future<File> _file() async => File(p.join((await AppPaths.get()).root, 'switch.json'));

  Future<Map<String, dynamic>> _load() async {
    if (_state != null) return _state!;
    try {
      final f = await _file();
      if (f.existsSync()) _state = jsonDecode(await f.readAsString()) as Map<String, dynamic>;
    } catch (_) {
      // Corrupt file: start over rather than crash the library.
    }
    _state ??= {};
    _state!['games'] ??= <dynamic>[];
    return _state!;
  }

  Future<void> _save() async {
    final f = await _file();
    await f.writeAsString(const JsonEncoder.withIndent('  ').convert(_state));
  }

  List<Map<String, dynamic>> _entries(Map<String, dynamic> s) =>
      (s['games'] as List).whereType<Map>().map((e) => e.cast<String, dynamic>()).toList();

  Game _toGame(Map<String, dynamic> e) {
    final name = e['name'] as String;
    return Game(title: Game.titleFromFile(name), console: Console.nswitch, fileName: name, path: e['path'] as String);
  }

  bool _isSwitchFile(String name) =>
      Console.nswitch.extensions.contains(p.extension(name).replaceFirst('.', '').toLowerCase());

  Future<List<Game>> _link(List<({String name, String path})> files) async {
    final s = await _load();
    final entries = _entries(s);
    final added = <Game>[];
    for (final f in files) {
      if (!_isSwitchFile(f.name)) continue;
      entries.removeWhere((e) => e['path'] == f.path);
      final e = {'name': f.name, 'path': f.path};
      entries.add(e);
      added.add(_toGame(e));
    }
    s['games'] = entries;
    await _save();
    return added;
  }

  // ---- emulator choice ----

  @override
  Future<EmulatorChoice?> current() async => EmulatorChoice.fromJson((await _load())['emulator']);

  @override
  Future<void> choose(EmulatorChoice choice) async {
    (await _load())['emulator'] = choice.toJson();
    await _save();
  }

  @override
  Future<List<EmulatorChoice>> detect() async {
    if (_android) {
      final list = await _channel.invokeListMethod<Map>('emulators') ?? const [];
      return [
        for (final m in list) EmulatorChoice(m['id'] as String, m['label'] as String, detail: m['id'] as String),
      ];
    }
    final found = <String>{};
    final home = Platform.environment['HOME'] ?? Platform.environment['USERPROFILE'] ?? '';

    if (Platform.isMacOS) {
      for (final dir in ['/Applications', p.join(home, 'Applications')]) {
        final d = Directory(dir);
        if (!d.existsSync()) continue;
        for (final e in d.listSync()) {
          final n = p.basename(e.path).toLowerCase();
          if (n.endsWith('.app') && _hints.any(n.contains)) found.add(e.path);
        }
      }
    } else {
      final names = Platform.isWindows
          ? ['eden.exe', 'citron.exe', 'Ryujinx.exe', 'sudachi.exe', 'suyu.exe']
          : ['eden', 'citron', 'Ryujinx', 'ryujinx', 'sudachi', 'suyu'];
      final sep = Platform.isWindows ? ';' : ':';
      for (final dir in (Platform.environment['PATH'] ?? '').split(sep)) {
        for (final n in names) {
          final f = File(p.join(dir, n));
          if (f.existsSync()) found.add(f.path);
        }
      }
      if (Platform.isLinux) {
        // AppImages are usually just downloaded somewhere in $HOME.
        for (final dir in ['Applications', 'Downloads', '.local/bin', 'bin', '']) {
          final d = Directory(p.join(home, dir));
          if (!d.existsSync()) continue;
          for (final e in d.listSync().whereType<File>()) {
            final n = p.basename(e.path).toLowerCase();
            if (n.endsWith('.appimage') && _hints.any(n.contains)) found.add(e.path);
          }
        }
      }
    }
    return [for (final path in found) _choiceForPath(path)];
  }

  EmulatorChoice _choiceForPath(String path) {
    var label = p.basenameWithoutExtension(path);
    final lower = label.toLowerCase();
    for (final h in _hints) {
      if (lower.contains(h)) {
        label = h == 'ryubing' ? 'Ryujinx (Ryubing)' : '${h[0].toUpperCase()}${h.substring(1)}';
        break;
      }
    }
    return EmulatorChoice(path, label, detail: path);
  }

  @override
  Future<EmulatorChoice?> browse() async {
    if (!_desktop) return null;
    final picked = await FilePicker.pickFiles(
      dialogTitle: 'Choose your Switch emulator (Eden, Citron, Ryujinx…)',
      type: FileType.any,
    );
    final path = picked.isEmpty ? null : picked.first.path;
    if (path == null) return null;
    return _choiceForPath(path);
  }

  // ---- games ----

  @override
  Future<List<Game>> addGames() async {
    if (_android) {
      final list = await _channel.invokeListMethod<Map>('pickGames') ?? const [];
      final files = [for (final m in list) (name: m['name'] as String, path: m['uri'] as String)];
      // Release access to anything picked by mistake (not a Switch file).
      for (final f in files.where((f) => !_isSwitchFile(f.name))) {
        await _channel.invokeMethod('release', {'uri': f.path});
      }
      return _link(files);
    }
    final picked = await FilePicker.pickFiles(dialogTitle: 'Add Switch games', type: FileType.any);
    return linkFiles([for (final f in picked) if (f.path != null) f.path!]);
  }

  @override
  Future<List<Game>> linkFiles(List<String> paths) =>
      _link([for (final path in paths) (name: p.basename(path), path: path)]);

  @override
  Future<List<Game>> linkedGames() async => [for (final e in _entries(await _load())) _toGame(e)];

  @override
  Future<void> forget(Game game) async {
    final s = await _load();
    s['games'] = _entries(s)..removeWhere((e) => e['path'] == game.path);
    await _save();
    if (_android && game.path != null) {
      await _channel.invokeMethod('release', {'uri': game.path});
    }
  }

  // ---- launching ----

  @override
  Future<void> launch(Game game) async {
    if (!supported) throw SwitchLaunchException(unsupportedReason);
    final emu = await current();
    if (emu == null) {
      throw SwitchLaunchException('Choose your Switch emulator in Settings first.', needsSetup: true);
    }
    final path = game.path!;

    if (_android) {
      try {
        await _channel.invokeMethod('launch', {'package': emu.id, 'uri': path});
      } on PlatformException catch (e) {
        throw SwitchLaunchException(e.message ?? 'Could not open ${emu.label}.', needsSetup: e.code == 'not_found');
      }
      return;
    }

    if (!File(path).existsSync()) {
      throw SwitchLaunchException('Can\'t find ${game.fileName}. It may have been moved, or its drive is disconnected.');
    }
    final exe = emu.id;
    final isApp = Platform.isMacOS && exe.toLowerCase().endsWith('.app');
    if (!(isApp ? Directory(exe).existsSync() : File(exe).existsSync())) {
      throw SwitchLaunchException('${emu.label} isn\'t at $exe anymore. Choose it again in Settings.', needsSetup: true);
    }
    final args = switchLaunchArgs(exe, path);
    try {
      // Freshly downloaded AppImages aren't executable yet.
      if (Platform.isLinux && File(exe).statSync().mode & 0x49 == 0) {
        await Process.run('chmod', ['+x', exe]);
      }
      if (isApp) {
        await Process.start('open', ['-n', '-a', exe, '--args', ...args], mode: ProcessStartMode.detached);
      } else {
        await Process.start(exe, args, mode: ProcessStartMode.detached, workingDirectory: p.dirname(exe));
      }
    } on ProcessException catch (e) {
      throw SwitchLaunchException('Could not start ${emu.label}: ${e.message}');
    }
  }
}
