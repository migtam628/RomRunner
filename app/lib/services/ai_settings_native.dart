import 'dart:io';

import 'package:path/path.dart' as p;

import 'paths.dart';

/// `<app support>/ai.json`. The API key is stored as entered, readable only by
/// RomRunner (app-private storage on Android and iOS).
Future<File> _file() async => File(p.join((await AppPaths.get()).root, 'ai.json'));

Future<String?> readAiSettings() async {
  final f = await _file();
  return f.existsSync() ? f.readAsString() : null;
}

Future<void> writeAiSettings(String json) async {
  final f = await _file();
  await f.parent.create(recursive: true);
  await f.writeAsString(json);
}
