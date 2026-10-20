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
  r'^(scph\d+[a-z]?|psxonpsp660|ps1_rom|gba_bios|gb_bios|gbc_bios|sgb_bios|bios_cd_[eju]|bios_md|disksys|lynxboot'
  r'|bios7|bios9|firmware|dsi_bios7|dsi_bios9|dsi_firmware|dsi_nand)\.(bin|rom|img)$',
  caseSensitive: false,
);

/// Arcade BIOS romsets: kept next to the games (FinalBurn Neo looks there)
/// but not listed as games.
const arcadeBiosSets = {'neogeo', 'pgm', 'skns', 'nmk004', 'isgsm', 'decocass', 'cchip', 'ym2608', 'bubsys', 'qsound', 'namcoc69'};

/// Looks inside a disc image to name its system (native only).
typedef DiscSniffer = Future<Console?> Function(String path);

String _ext(String name) {
  final base = name.split(RegExp(r'[/\\]')).last;
  final dot = base.lastIndexOf('.');
  return dot <= 0 ? '' : base.substring(dot + 1).toLowerCase();
}

/// The system for one file, without asking: a folder name that names a
/// system accepting this extension wins, then an extension unique to one
/// system, then sniffing the disc. Null when it's still unknown.
Future<Console?> identify(String path, {DiscSniffer? sniff, bool useFolderName = true}) async {
  final ext = _ext(path);
  if (ext.isEmpty) return null;
  if (useFolderName) {
    final hinted = consoleFromFolderName(path);
    if (hinted != null && hinted.accepts(ext)) return hinted;
  }
  final known = Console.fromExtension(ext);
  // Unique extensions, except .chd/.pbp/.cue/.m3u which PS1 shares with
  // PS2/PSP/Sega CD: let the sniffer have a look at those first.
  if (known != null && !(known == Console.ps1 && sniff != null)) return known;
  if (sniff != null) {
    final sniffed = await sniff(path);
    if (sniffed != null) return sniffed;
  }
  return known;
}

/// Shared helper for Add ROMs: assigns picked files to systems, asking the
/// user only when nothing else can tell. Tracks of a cue sheet follow the
/// sheet. [paths] maps each name to its original path when there is one
/// (for sniffing). Returns file name -> console.
Future<Map<String, Console>> classifyFiles(
  List<String> names,
  ConsoleResolver resolve, {
  Map<String, String> paths = const {},
  DiscSniffer? sniff,
}) async {
  final result = <String, Console>{};
  final sheets = <String, Console>{};
  final companions = <String>[];
  final hasDiscSheet = names.any((n) => const {'cue', 'ccd', 'm3u'}.contains(_ext(n)));

  for (final name in names) {
    final ext = _ext(name);
    if (Console.companions.contains(ext) && hasDiscSheet) {
      companions.add(name); // decided after the sheets
      continue;
    }
    final source = paths[name];
    var console = await identify(source ?? name, sniff: source == null ? null : sniff);
    if (console == null && (Console.ambiguous.contains(ext) || ext.isEmpty)) {
      console = await resolve(name);
    }
    if (console == null) continue;
    result[name] = console;
    if (const {'cue', 'ccd', 'm3u'}.contains(ext)) sheets[name] = console;
  }

  for (final name in companions) {
    // "Game (Track 2).bin" belongs to "Game.cue"; fall back to any sheet.
    final base = name.split(' (Track').first.replaceAll(RegExp(r'\.[^.]+$'), '');
    final sheet = sheets.entries.firstWhere(
      (e) => e.key.replaceAll(RegExp(r'\.[^.]+$'), '') == base,
      orElse: () => sheets.entries.isEmpty ? const MapEntry('', Console.ps1) : sheets.entries.first,
    );
    result[name] = sheet.value;
  }
  return result;
}

/// The system suggested by the nearest folder name in [path], if any.
Console? consoleFromFolderName(String path) {
  final parts = path.split(RegExp(r'[/\\]'));
  for (var i = parts.length - 2; i >= 0; i--) {
    final c = Console.fromFolderName(parts[i]);
    if (c != null) return c;
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
/// Cue-sheet tracks are hidden behind their sheet; shared extensions (.iso,
/// .bin ...) are assigned by folder name or disc sniffing; zips count as
/// arcade romsets only inside an arcade-named folder; anything still
/// unknown is skipped and counted.
Future<FolderScan> classifyFolderPaths(Iterable<String> paths, {DiscSniffer? sniff}) async {
  final byDir = <String, List<String>>{};
  for (final path in paths) {
    byDir.putIfAbsent(p.dirname(path), () => []).add(path);
  }
  final games = <String, Console>{};
  var skipped = 0;
  for (final files in byDir.values) {
    final hasSheet = files.any((f) => const {'cue', 'ccd', 'm3u'}.contains(_ext(f)));
    for (final f in files) {
      final e = _ext(f);
      final base = p.basename(f);
      if (base.startsWith('.')) continue; // macOS "._" files etc.
      if (biosPattern.hasMatch(base)) continue;
      if (hasSheet && Console.companions.contains(e)) continue;
      if (e == 'zip' || e == '7z') {
        final hinted = consoleFromFolderName(f);
        if (hinted == Console.arcade && e == 'zip') {
          if (!arcadeBiosSets.contains(p.basenameWithoutExtension(f).toLowerCase())) games[f] = Console.arcade;
        } else {
          skipped++;
        }
        continue;
      }
      final c = await identify(f, sniff: sniff);
      if (c != null) {
        games[f] = c;
      } else if (Console.ambiguous.contains(e)) {
        skipped++;
      }
    }
  }
  return FolderScan(games, skipped);
}
