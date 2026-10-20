import 'dart:async';
import 'dart:js_interop';
import 'dart:typed_data';

import 'package:file_picker/file_picker.dart';
import 'package:web/web.dart' as web;

import '../models/console.dart';
import '../models/game.dart';
import 'library.dart';

GameLibrary createLibrary() => WebLibrary();

/// Web keeps ROMs in memory for the session; nothing is uploaded anywhere.
/// EmulatorJS stores its own saves in the browser's IndexedDB.
class WebLibrary implements GameLibrary {
  final List<Game> _games = [];

  @override
  Future<List<Game>> load() async => List.unmodifiable(_games);

  @override
  Future<ImportResult> importFiles(ConsoleResolver resolve) async {
    final picked = await FilePicker.pickFiles(type: FileType.any);
    if (picked.isEmpty) return const ImportResult();
    final byName = {for (final f in picked) f.name: f};
    final classified = await classifyFiles(byName.keys.toList(), resolve);
    final added = <Game>[];
    final notes = <String>[];
    final unplayable = {for (final c in classified.values) if (!c.playsHere) c.name};
    if (unplayable.isNotEmpty) {
      notes.add('${unplayable.join(', ')} games can\'t run in a browser');
      classified.removeWhere((_, c) => !c.playsHere);
    }
    for (final MapEntry(key: name, value: console) in classified.entries) {
      final ext = name.split('.').last.toLowerCase();
      if (Console.companions.contains(ext) && console == Console.ps1) continue;
      final bytes = await byName[name]!.readAsBytes();
      final game = Game(title: Game.titleFromFile(name), console: console, fileName: name, bytes: bytes);
      _games.removeWhere((g) => g.id == game.id);
      _games.add(game);
      added.add(game);
    }
    _games.sort((a, b) => a.title.toLowerCase().compareTo(b.title.toLowerCase()));
    return ImportResult(games: added, notes: notes);
  }

  @override
  Future<void> remove(Game game) async => _games.removeWhere((g) => g.id == game.id);

  @override
  Future<String> describeStorage() async =>
      'Web: ROMs stay in this tab\'s memory and are never uploaded.\n'
      'In-game saves are kept by the browser for this site.\n'
      'PS1 on web: use single-file .chd or .pbp images.';

  @override
  bool get supportsFolders => true;

  @override
  bool get foldersAreLinked => false;

  @override
  Future<List<String>> folders() async => const [];

  @override
  Future<void> removeFolder(String path) async {}

  /// Browsers can't keep a folder linked, so this imports every game in the
  /// chosen folder (and its subfolders) into this tab's memory.
  @override
  Future<FolderResult> addFolder({required Future<String?> Function() askPath}) async {
    final files = await _pickDirectory();
    if (files == null || files.isEmpty) return const FolderResult();
    final byPath = <String, web.File>{};
    for (final f in files) {
      final rel = f.webkitRelativePath.isEmpty ? f.name : f.webkitRelativePath;
      // Skip hidden folders like .Trash or .git (same rule as native scans).
      if (rel.split('/').any((s) => s.startsWith('.'))) continue;
      byPath[rel] = f;
    }
    if (byPath.isEmpty) return const FolderResult(folder: '', found: 0);
    final scan = await classifyFolderPaths(byPath.keys);
    var found = 0;
    for (final MapEntry(key: rel, value: console) in scan.games.entries) {
      if (!console.playsHere) continue; // e.g. PS2 or Switch can't run in a browser
      final f = byPath[rel]!;
      final buffer = await f.arrayBuffer().toDart;
      final game = Game(
        title: Game.titleFromFile(f.name),
        console: console,
        fileName: f.name,
        bytes: Uint8List.view(buffer.toDart),
      );
      _games.removeWhere((g) => g.id == game.id);
      _games.add(game);
      found++;
    }
    _games.sort((a, b) => a.title.toLowerCase().compareTo(b.title.toLowerCase()));
    final root = byPath.keys.first.split('/').first;
    return FolderResult(folder: root, found: found, skipped: scan.skipped);
  }

  Future<List<web.File>?> _pickDirectory() {
    final done = Completer<List<web.File>?>();
    final input = web.HTMLInputElement()
      ..type = 'file'
      ..multiple = true;
    input.setAttribute('webkitdirectory', '');
    input.addEventListener(
      'change',
      ((web.Event _) {
        final list = input.files;
        final out = <web.File>[];
        if (list != null) {
          for (var i = 0; i < list.length; i++) {
            final f = list.item(i);
            if (f != null) out.add(f);
          }
        }
        if (!done.isCompleted) done.complete(out);
      }).toJS,
    );
    input.addEventListener('cancel', ((web.Event _) {
      if (!done.isCompleted) done.complete(null);
    }).toJS);
    input.click();
    return done.future;
  }
}
