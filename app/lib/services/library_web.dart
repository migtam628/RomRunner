import 'package:file_picker/file_picker.dart';

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
    if (classified.values.any((c) => c.external)) {
      notes.add('Switch games can\'t run in a browser');
      classified.removeWhere((_, c) => c.external);
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
}
