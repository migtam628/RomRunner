import '../models/console.dart';
import '../models/game.dart';
import 'library_native.dart' if (dart.library.js_interop) 'library_web.dart' as impl;

/// Asks the user which console an ambiguous file (e.g. ".bin") belongs to.
typedef ConsoleResolver = Future<Console?> Function(String fileName);

abstract class GameLibrary {
  static GameLibrary create() => impl.createLibrary();

  Future<List<Game>> load();

  /// Opens the file picker and imports the chosen ROMs (and BIOS files).
  Future<ImportResult> importFiles(ConsoleResolver resolve);

  Future<void> remove(Game game);

  /// Human-readable storage info for the settings sheet.
  Future<String> describeStorage();
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
