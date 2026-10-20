import 'package:path/path.dart' as p;

import '../models/console.dart';
import '../models/game.dart';
import 'library_native.dart' if (dart.library.js_interop) 'library_web.dart' as impl;

/// Asks the user which console an ambiguous file (e.g. ".bin") belongs to.
typedef ConsoleResolver = Future<Console?> Function(String fileName);

abstract class GameLibrary {
  /// One shared library (the web build keeps its games in memory).
  static final GameLibrary instance = impl.createLibrary();

  Future<List<Game>> load();

  /// Opens the file picker and imports the chosen ROMs (and BIOS files).
  Future<ImportResult> importFiles(ConsoleResolver resolve);

  Future<void> remove(Game game);

  /// Human-readable storage info for the settings sheet.
  Future<String> describeStorage();

  /// Whether [addFolder] is available here.
  bool get supportsFolders;

  /// True when folders stay linked and are rescanned (desktop, Android);
  /// false when picking a folder is a one-time import (web).
  bool get foldersAreLinked;

  /// Lets the user pick a folder of games. [askPath] is a fallback for
  /// devices without a folder picker (some Fire TV models): it should ask
  /// the user to type a path.
  Future<FolderResult> addFolder({required Future<String?> Function() askPath});

  /// Linked folders (empty on platforms where folders aren't linked).
  Future<List<String>> folders();
  Future<void> removeFolder(String path);
}

class FolderResult {
  const FolderResult({this.folder, this.found = 0, this.skipped = 0, this.error});

  /// The folder that was added; null if cancelled or failed.
  final String? folder;
  final int found;

  /// Files that look like games but couldn't be assigned to a system.
  final int skipped;
  final String? error;
}

class ImportResult {
  const ImportResult({this.games = const [], this.bios = const [], this.notes = const []});
  final List<Game> games;

  /// BIOS files copied into the system folder.
  final List<String> bios;

  /// Extra messages for the user (e.g. files that need another route).
  final List<String> notes;
}

/// Well-known BIOS file names, installed into the system folder on import.
final biosPattern = RegExp(
  r'^(scph\d+[a-z]?|psxonpsp660|ps1_rom|gba_bios|gb_bios|gbc_bios|sgb_bios|bios_cd_[eju]|bios_md|disksys)\.(bin|rom)$',
  caseSensitive: false,
);

/// Shared helper: groups picked file names into playable entries.
/// Returns map of file name -> console, skipping companion files that belong
/// to a cue sheet in the same pick.
Future<Map<String, Console>> classifyFiles(List<String> names, ConsoleResolver resolve) async {
  final result = <String, Console>{};
  final hasDiscSheet = names.any((n) {
    final e = n.split('.').last.toLowerCase();
    return e == 'cue' || e == 'ccd' || e == 'm3u';
  });
  for (final name in names) {
    final ext = name.contains('.') ? name.split('.').last.toLowerCase() : '';
    final known = Console.fromExtension(ext);
    if (known != null) {
      result[name] = known;
    } else if (Console.companions.contains(ext) && hasDiscSheet) {
      result[name] = Console.ps1; // track file for a cue sheet
    } else if (Console.ambiguous.contains(ext) || ext.isEmpty) {
      final chosen = await resolve(name);
      if (chosen != null) result[name] = chosen;
    }
  }
  return result;
}

/// Normalised folder names that identify a system (for ambiguous files
/// such as a Genesis ".bin" sitting in a folder called "Mega Drive").
const _folderHints = <Console, List<String>>{
  Console.nes: ['nes', 'famicom', 'fds', 'nintendoentertainmentsystem'],
  Console.gb: ['gb', 'gbc', 'gameboy', 'gameboycolor', 'gameboycolour'],
  Console.gba: ['gba', 'gameboyadvance'],
  Console.snes: ['snes', 'sfc', 'supernintendo', 'superfamicom', 'supernes'],
  Console.genesis: [
    'genesis', 'megadrive', 'md', 'segagenesis', 'segamegadrive', 'mastersystem', 'sms',
    'segamastersystem', 'gamegear', 'gg', 'segagamegear', 'sega',
  ],
  Console.ps1: ['ps1', 'psx', 'playstation', 'ps', 'sonyplaystation', 'playstation1'],
  Console.nswitch: ['switch', 'nsw', 'nintendoswitch'],
};

/// The system suggested by the nearest folder name in [path], if any.
Console? consoleFromFolderName(String path) {
  final parts = path.split(RegExp(r'[/\\]'));
  for (var i = parts.length - 2; i >= 0; i--) {
    final n = parts[i].toLowerCase().replaceAll(RegExp(r'[^a-z0-9]'), '');
    if (n.isEmpty) continue;
    for (final MapEntry(key: c, value: names) in _folderHints.entries) {
      if (names.contains(n)) return c;
    }
  }
  return null;
}

class FolderScan {
  FolderScan(this.games, this.skipped);

  /// path -> system, for every playable file found.
  final Map<String, Console> games;
  final int skipped;
}

/// Decides which files in a folder tree are games, without asking the user.
/// Cue-sheet tracks are hidden behind their sheet; ambiguous files (.bin,
/// .img) are assigned by folder name; archives are skipped (they must be
/// unpacked with Add ROMs).
FolderScan classifyFolderPaths(Iterable<String> paths) {
  final byDir = <String, List<String>>{};
  for (final path in paths) {
    byDir.putIfAbsent(p.dirname(path), () => []).add(path);
  }
  final games = <String, Console>{};
  var skipped = 0;
  for (final files in byDir.values) {
    String ext(String f) => p.extension(f).replaceFirst('.', '').toLowerCase();
    final hasSheet = files.any((f) => const {'cue', 'ccd', 'm3u'}.contains(ext(f)));
    for (final f in files) {
      final e = ext(f);
      if (p.basename(f).startsWith('.')) continue; // macOS "._" files etc.
      if (biosPattern.hasMatch(p.basename(f))) continue;
      if (hasSheet && Console.companions.contains(e)) continue;
      final known = Console.fromExtension(e);
      if (known != null) {
        games[f] = known;
      } else if (e == 'bin' || e == 'img') {
        final hinted = consoleFromFolderName(f);
        if (hinted != null && !hinted.external) {
          games[f] = hinted;
        } else {
          skipped++;
        }
      } else if (e == 'zip' || e == '7z') {
        skipped++;
      }
    }
  }
  return FolderScan(games, skipped);
}
