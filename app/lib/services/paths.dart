import 'dart:io';

import 'package:path/path.dart' as p;
import 'package:path_provider/path_provider.dart';

import '../models/console.dart';

/// Folders the native app uses, all under the app support directory.
class AppPaths {
  AppPaths._(this.root);
  final String root;

  static AppPaths? _cached;

  static Future<AppPaths> get() async {
    if (_cached != null) return _cached!;
    final base = await getApplicationSupportDirectory();
    final paths = AppPaths._(base.path);
    for (final dir in [paths.roms, paths.saves, paths.states, paths.system, paths.cores]) {
      Directory(dir).createSync(recursive: true);
    }
    for (final c in Console.values) {
      Directory(paths.romsFor(c)).createSync(recursive: true);
    }
    return _cached = paths;
  }

  String get roms => p.join(root, 'roms');
  String get saves => p.join(root, 'saves');
  String get states => p.join(root, 'states');

  /// BIOS files go here (e.g. scph5501.bin for PS1, gba_bios.bin).
  String get system => p.join(root, 'system');
  String get cores => p.join(root, 'cores');

  String romsFor(Console c) => p.join(roms, c.name.toLowerCase().replaceAll(' ', '_'));

  String statePath(String gameId, int slot) {
    final safe = gameId.replaceAll(RegExp(r'[^\w\-.]+'), '_');
    return p.join(states, '$safe.slot$slot.state');
  }
}
