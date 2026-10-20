import 'dart:convert';
import 'dart:io';

import 'package:path/path.dart' as p;

import '../models/console.dart';
import '../models/control_layout.dart';
import 'paths.dart';

/// Saves touch layouts in `<app support>/controls.json`, one per system and
/// orientation ("GBA|portrait").
class ControlStore {
  ControlStore._();
  static final instance = ControlStore._();

  Map<String, dynamic>? _all;

  Future<File> _file() async => File(p.join((await AppPaths.get()).root, 'controls.json'));

  static String _key(Console c, bool portrait) => '${c.name}|${portrait ? 'portrait' : 'landscape'}';

  Future<Map<String, dynamic>> _load() async {
    if (_all != null) return _all!;
    try {
      final f = await _file();
      if (f.existsSync()) _all = (jsonDecode(await f.readAsString()) as Map).cast<String, dynamic>();
    } catch (_) {}
    return _all ??= {};
  }

  Future<ControlLayout> load(Console c, bool portrait) async {
    final j = (await _load())[_key(c, portrait)];
    return j is Map ? ControlLayout.fromJson(j) : ControlLayout();
  }

  Future<void> save(Console c, bool portrait, ControlLayout l) async {
    final all = await _load();
    all[_key(c, portrait)] = l.toJson();
    try {
      await (await _file()).writeAsString(jsonEncode(all));
    } catch (_) {}
  }
}
