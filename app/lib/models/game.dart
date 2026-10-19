import 'dart:typed_data';

import 'console.dart';

class Game {
  Game({
    required this.title,
    required this.console,
    required this.fileName,
    this.path,
    this.bytes,
  });

  final String title;
  final Console console;
  final String fileName;

  /// Absolute path on native platforms.
  final String? path;

  /// ROM contents on web (kept in memory for the session).
  final Uint8List? bytes;

  String get id => '${console.name}/$fileName';

  static String titleFromFile(String fileName) {
    final dot = fileName.lastIndexOf('.');
    final base = dot > 0 ? fileName.substring(0, dot) : fileName;
    // Strip dump tags like "(USA)" or "[!]" for display.
    final cleaned = base
        .replaceAll(RegExp(r'\s*[\(\[][^\)\]]*[\)\]]'), '')
        .replaceAll('_', ' ')
        .trim();
    return cleaned.isEmpty ? base : cleaned;
  }
}
