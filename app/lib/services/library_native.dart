import 'dart:io';

import 'package:archive/archive.dart';
import 'package:file_picker/file_picker.dart';
import 'package:path/path.dart' as p;

import '../models/console.dart';
import '../models/game.dart';
import 'library.dart';
import 'paths.dart';

GameLibrary createLibrary() => NativeLibrary();

/// ROMs live in `<app support>/roms/<console>/`. Importing copies files there,
/// so the library is just a folder scan (no database to get out of sync).
class NativeLibrary implements GameLibrary {
  @override
  Future<List<Game>> load() async {
    final paths = await AppPaths.get();
    final games = <Game>[];
    for (final c in Console.values) {
      final dir = Directory(paths.romsFor(c));
      if (!dir.existsSync()) continue;
      final files = dir.listSync().whereType<File>().toList();
      final hasSheet = files.any((f) => const {'.cue', '.ccd', '.m3u'}.contains(p.extension(f.path).toLowerCase()));
      for (final f in files) {
        final ext = p.extension(f.path).replaceFirst('.', '').toLowerCase();
        final listed = c.extensions.contains(ext) ||
            (Console.ambiguous.contains(ext) && !(c == Console.ps1 && hasSheet));
        if (!listed) continue;
        final name = p.basename(f.path);
        games.add(Game(title: Game.titleFromFile(name), console: c, fileName: name, path: f.path));
      }
    }
    games.sort((a, b) => a.title.toLowerCase().compareTo(b.title.toLowerCase()));
    return games;
  }

  @override
  Future<ImportResult> importFiles(ConsoleResolver resolve) async {
    final picked = await FilePicker.pickFiles(type: FileType.any);
    if (picked.isEmpty) return const ImportResult();
    final paths = await AppPaths.get();
    final added = <Game>[];

    // name -> writer that copies the file into place. Zips are unpacked so
    // their contents are classified like any other file; everything else is
    // streamed so large disc images never sit in memory.
    final writers = <String, Future<void> Function(File dest)>{};
    for (final f in picked) {
      if (p.extension(f.name).toLowerCase() == '.zip') {
        final archive = ZipDecoder().decodeBytes(await f.readAsBytes());
        for (final entry in archive.files.where((e) => e.isFile)) {
          writers[p.basename(entry.name)] = (dest) => dest.writeAsBytes(entry.content as List<int>, flush: true);
        }
      } else {
        writers[f.name] = (dest) async {
          final sink = dest.openWrite();
          await sink.addStream(f.readAsByteStream());
          await sink.close();
        };
      }
    }

    final bios = <String>[];
    for (final name in writers.keys.where(biosPattern.hasMatch).toList()) {
      await writers.remove(name)!(File(p.join(paths.system, name)));
      bios.add(name);
    }

    final classified = await classifyFiles(writers.keys.toList(), resolve);
    for (final MapEntry(key: name, value: console) in classified.entries) {
      final dest = File(p.join(paths.romsFor(console), name));
      await writers[name]!(dest);
      final ext = p.extension(name).replaceFirst('.', '').toLowerCase();
      if (!Console.companions.contains(ext) || console != Console.ps1) {
        added.add(Game(title: Game.titleFromFile(name), console: console, fileName: name, path: dest.path));
      }
    }
    await FilePicker.clearTemporaryFiles();
    return ImportResult(games: added, bios: bios);
  }

  @override
  Future<void> remove(Game game) async {
    final path = game.path;
    if (path == null) return;
    final file = File(path);
    // Also remove cue-sheet track files ("Game.bin", "Game (Track 2).bin").
    final base = p.basenameWithoutExtension(path);
    for (final f in file.parent.listSync().whereType<File>()) {
      final other = p.basenameWithoutExtension(f.path);
      if (other == base || other.startsWith('$base (Track')) f.deleteSync();
    }
  }

  @override
  Future<String> describeStorage() async {
    final paths = await AppPaths.get();
    return 'ROMs: ${paths.roms}\n'
        'BIOS (system): ${paths.system}\n'
        'Battery saves: ${paths.saves}\n'
        'Save states: ${paths.states}\n'
        'Downloaded cores: ${paths.cores}';
  }
}
