// Matching games to box art in the libretro-thumbnails collection
// (https://thumbnails.libretro.com, one folder per system, files named after
// No-Intro / Redump game names). Pure Dart so it can be tested.
import '../models/console.dart';

/// libretro-thumbnails folders for a game, best guess first (e.g. a .gbc
/// file checks Game Boy Color before Game Boy).
List<String> thumbnailSystems(Console c, String fileName) {
  final ext = fileName.contains('.') ? fileName.split('.').last.toLowerCase() : '';
  return switch (c) {
    Console.nes => ['Nintendo - Nintendo Entertainment System'],
    Console.gb => ext == 'gbc'
        ? ['Nintendo - Game Boy Color', 'Nintendo - Game Boy']
        : ['Nintendo - Game Boy', 'Nintendo - Game Boy Color'],
    Console.gba => ['Nintendo - Game Boy Advance'],
    Console.nds => ['Nintendo - Nintendo DS'],
    Console.snes => ['Nintendo - Super Nintendo Entertainment System'],
    Console.vb => ['Nintendo - Virtual Boy'],
    Console.genesis => switch (ext) {
        'sms' => ['Sega - Master System - Mark III'],
        'gg' => ['Sega - Game Gear'],
        _ => ['Sega - Mega Drive - Genesis'],
      },
    Console.segacd => ['Sega - Mega-CD - Sega CD'],
    Console.s32x => ['Sega - 32X'],
    Console.pce => ['NEC - PC Engine - TurboGrafx 16'],
    Console.ngp => ext == 'ngc'
        ? ['SNK - Neo Geo Pocket Color', 'SNK - Neo Geo Pocket']
        : ['SNK - Neo Geo Pocket', 'SNK - Neo Geo Pocket Color'],
    Console.ws => ext == 'wsc' ? ['Bandai - WonderSwan Color', 'Bandai - WonderSwan'] : ['Bandai - WonderSwan', 'Bandai - WonderSwan Color'],
    Console.a2600 => ['Atari - 2600'],
    Console.a7800 => ['Atari - 7800'],
    Console.lynx => ['Atari - Lynx'],
    Console.arcade => ['FBNeo - Arcade Games'],
    Console.ps1 => ['Sony - PlayStation'],
    Console.n64 => ['Nintendo - Nintendo 64'],
    Console.ps2 => ['Sony - PlayStation 2'],
    Console.psp => ['Sony - PlayStation Portable'],
    Console.gcwii => ext == 'wbfs' ? ['Nintendo - Wii', 'Nintendo - GameCube'] : ['Nintendo - GameCube', 'Nintendo - Wii'],
    Console.wiiu => ['Nintendo - Wii U'],
    Console.n3ds => ['Nintendo - Nintendo 3DS'],
    Console.xbox => ['Microsoft - Xbox'],
    Console.xbox360 => ['Microsoft - Xbox 360'],
    Console.ps3 => ['Sony - PlayStation 3'],
    Console.vita => ['Sony - PlayStation Vita'],
    Console.nswitch => [], // not in libretro-thumbnails
  };
}

/// libretro-thumbnails replaces these characters with "_" in file names.
String thumbnailFileName(String gameName) => gameName.replaceAll(RegExp(r'[&*/:`<>?\\|"]'), '_');

/// The game's name without extension (a PS3 folder's name as-is).
String gameBaseName(String fileName) {
  final dot = fileName.lastIndexOf('.');
  return dot > 0 && fileName.length - dot <= 6 ? fileName.substring(0, dot) : fileName;
}

/// "The Legend of Zelda - Link's Awakening (USA) [!]" -> "legendofzeldalinksawakening".
String normalizeTitle(String name) {
  var s = name.toLowerCase();
  s = s.replaceAll(RegExp(r'\s*[\(\[][^\)\]]*[\)\]]'), ''); // (USA), [!], (Rev 1)
  s = s.replaceAll('&', ' and ').replaceAll('+', ' and ');
  s = s.replaceAll(RegExp(r',\s*the\b'), ''); // "Zelda, The"
  s = s.replaceAll(RegExp(r'^the\s+'), '');
  s = s.replaceAll(RegExp(r"['’]"), '');
  return s.replaceAll(RegExp(r'[^a-z0-9]'), '');
}

Set<String> _words(String name) {
  final s = name.toLowerCase().replaceAll(RegExp(r'\s*[\(\[][^\)\]]*[\)\]]'), '').replaceAll(RegExp(r"['’]"), '');
  return s.split(RegExp(r'[^a-z0-9]+')).where((w) => w.isNotEmpty && w != 'the').toSet();
}

int _regionRank(String name) {
  final n = name.toLowerCase();
  if (n.contains('(usa)') || n.contains('(usa,')) return 0;
  if (n.contains('(world)')) return 1;
  if (n.contains('europe')) return 2;
  if (n.contains('(japan)')) return 4;
  return 3;
}

/// The best box-art entry (without ".png") in [available] for [fileName].
/// Exact No-Intro name first; then same title ignoring tags (USA, then
/// World, Europe, others, Japan); then a close word match. Null if none.
String? pickCover(String fileName, Iterable<String> available) {
  final base = thumbnailFileName(gameBaseName(fileName));
  final names = available.toList();
  if (names.contains(base)) return base;

  final wanted = normalizeTitle(base);
  if (wanted.isEmpty) return null;
  final same = [for (final n in names) if (normalizeTitle(n) == wanted) n];
  if (same.isNotEmpty) {
    same.sort((a, b) {
      final r = _regionRank(a).compareTo(_regionRank(b));
      return r != 0 ? r : a.length.compareTo(b.length); // fewer tags = the plain release
    });
    return same.first;
  }

  // Close match: every word of the file name appears in the entry, with
  // the same first word (e.g. "Pokemon Red" -> "Pokemon - Red Version").
  // Highest word overlap wins; ties go to the better region.
  final w = _words(base);
  if (w.isEmpty) return null;
  final first = _words(base).isEmpty ? null : _words(base.split(RegExp(r'\s')).first).firstOrNull;
  String? best;
  var bestScore = 0.0;
  for (final n in names) {
    final nw = _words(n);
    if (!nw.containsAll(w) || (first != null && !nw.contains(first))) continue;
    final score = w.length / nw.length;
    final better = score > bestScore || (score == bestScore && best != null && _regionRank(n) < _regionRank(best));
    if (better) {
      best = n;
      bestScore = score;
    }
  }
  return bestScore >= 0.5 ? best : null;
}
