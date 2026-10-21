import 'package:flutter/foundation.dart' show kIsWeb;
import 'package:flutter/material.dart';

/// RetroPad button ids (mirrors RETRO_DEVICE_ID_JOYPAD_*). Kept here so the
/// UI never imports dart:ffi and still compiles for web.
enum Pad {
  b, y, select, start, up, down, left, right, a, x, l, r, l2, r2, l3, r3;

  int get bit => 1 << index;
}

/// A face/shoulder button shown on the touch controller.
class PadButton {
  const PadButton(this.label, this.pad);
  final String label;
  final Pad pad;
}

const _ba = [PadButton('B', Pad.b), PadButton('A', Pad.a)];
const _yxba = [PadButton('Y', Pad.y), PadButton('X', Pad.x), PadButton('B', Pad.b), PadButton('A', Pad.a)];
const _lr = [PadButton('L', Pad.l), PadButton('R', Pad.r)];
const _sega = [PadButton('A', Pad.y), PadButton('B', Pad.b), PadButton('C', Pad.a)];

/// Every system RomRunner knows about.
///
/// A system is either emulated by RomRunner itself through a libretro
/// [core] (and on the web through EmulatorJS, [ejsCore]), or [external]:
/// handed to an emulator app the user installed (see
/// services/external_catalog.dart). N64 is both: external on native
/// platforms, EmulatorJS on the web.
enum Console {
  nes(
    name: 'NES',
    fullName: 'Nintendo Entertainment System',
    extensions: ['nes', 'unf', 'unif', 'fds'],
    core: 'fceumm',
    ejsCore: 'nes',
    color: Color(0xFFE5484D),
    face: _ba,
    hints: ['nes', 'famicom', 'fds', 'nintendoentertainmentsystem'],
  ),
  gb(
    name: 'Game Boy',
    fullName: 'Game Boy / Game Boy Color',
    extensions: ['gb', 'gbc', 'dmg', 'cgb', 'sgb'],
    core: 'gambatte',
    ejsCore: 'gb',
    color: Color(0xFF8BB33A),
    face: _ba,
    hints: ['gb', 'gbc', 'gameboy', 'gameboycolor', 'gameboycolour'],
  ),
  gba(
    name: 'GBA',
    fullName: 'Game Boy Advance',
    extensions: ['gba', 'agb'],
    core: 'mgba',
    ejsCore: 'gba',
    color: Color(0xFF7C5CFF),
    face: _ba,
    shoulders: _lr,
    hints: ['gba', 'gameboyadvance'],
  ),
  nds(
    name: 'DS',
    fullName: 'Nintendo DS',
    extensions: ['nds', 'dsi'],
    core: 'melondsds',
    ejsCore: 'nds',
    color: Color(0xFFB0B7C3),
    face: _yxba,
    shoulders: _lr,
    touchscreen: true,
    hints: ['nds', 'ds', 'nintendods'],
  ),
  snes(
    name: 'SNES',
    fullName: 'Super Nintendo',
    extensions: ['sfc', 'smc', 'swc', 'fig'],
    core: 'snes9x',
    ejsCore: 'snes',
    color: Color(0xFF9B8FD9),
    face: _yxba,
    shoulders: _lr,
    hints: ['snes', 'sfc', 'supernintendo', 'superfamicom', 'supernes'],
  ),
  vb(
    name: 'Virtual Boy',
    fullName: 'Nintendo Virtual Boy',
    extensions: ['vb', 'vboy'],
    core: 'mednafen_vb',
    ejsCore: 'vb',
    color: Color(0xFFC2185B),
    face: _ba,
    shoulders: _lr,
    hints: ['vb', 'virtualboy'],
  ),
  genesis(
    name: 'Genesis',
    fullName: 'Genesis / Mega Drive / Master System / Game Gear',
    extensions: ['md', 'gen', 'smd', 'sms', 'gg', 'sg'],
    also: ['bin'],
    core: 'genesis_plus_gx',
    ejsCore: 'segaMD',
    color: Color(0xFF3B82F6),
    // Genesis Plus GX default 3-button mapping: A=Y, B=B, C=A.
    face: _sega,
    hints: [
      'genesis', 'megadrive', 'md', 'segagenesis', 'segamegadrive', 'mastersystem', 'sms',
      'segamastersystem', 'gamegear', 'gg', 'segagamegear', 'sega',
    ],
  ),
  segacd(
    name: 'Sega CD',
    fullName: 'Sega CD / Mega-CD (needs BIOS)',
    extensions: [],
    also: ['cue', 'chd', 'iso', 'bin', 'm3u'],
    core: 'genesis_plus_gx',
    ejsCore: 'segaCD',
    color: Color(0xFF2563EB),
    face: _sega,
    hints: ['segacd', 'megacd', 'scd'],
  ),
  s32x(
    name: '32X',
    fullName: 'Sega 32X',
    extensions: ['32x'],
    core: 'picodrive',
    ejsCore: 'sega32x',
    color: Color(0xFF1E40AF),
    face: _sega,
    hints: ['32x', 'sega32x'],
  ),
  pce(
    name: 'PC Engine',
    fullName: 'PC Engine / TurboGrafx-16',
    extensions: ['pce'],
    core: 'mednafen_pce_fast',
    ejsCore: 'pce',
    color: Color(0xFFF97316),
    face: [PadButton('II', Pad.b), PadButton('I', Pad.a)],
    hints: ['pce', 'pcengine', 'turbografx', 'turbografx16', 'tg16'],
  ),
  ngp(
    name: 'Neo Geo Pocket',
    fullName: 'Neo Geo Pocket / Color',
    extensions: ['ngp', 'ngc'],
    core: 'mednafen_ngp',
    ejsCore: 'ngp',
    color: Color(0xFF0EA5E9),
    face: _ba,
    hints: ['ngp', 'ngpc', 'neogeopocket', 'neogeopocketcolor'],
  ),
  ws(
    name: 'WonderSwan',
    fullName: 'WonderSwan / Color',
    extensions: ['ws', 'wsc'],
    core: 'mednafen_wswan',
    ejsCore: 'ws',
    color: Color(0xFF14B8A6),
    face: _ba,
    hints: ['ws', 'wsc', 'wonderswan', 'wonderswancolor'],
  ),
  a2600(
    name: 'Atari 2600',
    fullName: 'Atari 2600',
    extensions: ['a26'],
    core: 'stella',
    ejsCore: 'atari2600',
    color: Color(0xFFB45309),
    face: [PadButton('FIRE', Pad.b)],
    hints: ['atari2600', '2600', 'vcs'],
  ),
  a7800(
    name: 'Atari 7800',
    fullName: 'Atari 7800',
    extensions: ['a78'],
    core: 'prosystem',
    ejsCore: 'atari7800',
    color: Color(0xFFD97706),
    face: _ba,
    hints: ['atari7800', '7800'],
  ),
  lynx(
    name: 'Lynx',
    fullName: 'Atari Lynx (needs BIOS)',
    extensions: ['lnx'],
    core: 'handy',
    ejsCore: 'lynx',
    color: Color(0xFFF59E0B),
    face: _ba,
    shoulders: _lr,
    hints: ['lynx', 'atarilynx'],
  ),
  arcade(
    name: 'Arcade',
    fullName: 'Arcade (FinalBurn Neo romsets)',
    extensions: [],
    also: ['zip'],
    core: 'fbneo',
    ejsCore: 'arcade',
    color: Color(0xFFEAB308),
    face: _yxba,
    shoulders: _lr,
    hints: ['arcade', 'fbneo', 'fba', 'mame', 'neogeo', 'cps1', 'cps2', 'cps3'],
  ),
  ps1(
    name: 'PS1',
    fullName: 'PlayStation',
    extensions: ['cue', 'chd', 'pbp', 'm3u', 'ccd'],
    also: ['iso', 'bin', 'img'],
    core: 'pcsx_rearmed',
    ejsCore: 'psx',
    color: Color(0xFF94A3B8),
    face: [PadButton('□', Pad.y), PadButton('△', Pad.x), PadButton('✕', Pad.b), PadButton('○', Pad.a)],
    shoulders: [PadButton('L1', Pad.l), PadButton('R1', Pad.r), PadButton('L2', Pad.l2), PadButton('R2', Pad.r2)],
    hints: ['ps1', 'psx', 'playstation', 'ps', 'sonyplaystation', 'playstation1'],
  ),

  // ---- Opened in an emulator app the user installs ----
  n64(
    name: 'N64',
    fullName: 'Nintendo 64',
    extensions: ['z64', 'n64', 'v64'],
    ejsCore: 'n64',
    inHouse: 'romrunner_n64',
    color: Color(0xFF16A34A),
    // Touch pad for the built-in core (see cores/n64/README.md for the mapping).
    face: [PadButton('B', Pad.y), PadButton('C▲', Pad.x), PadButton('A', Pad.b), PadButton('C▶', Pad.a)],
    shoulders: [PadButton('L', Pad.l), PadButton('R', Pad.r), PadButton('Z', Pad.l2)],
    external: true,
    hints: ['n64', 'nintendo64'],
  ),
  ps2(
    name: 'PS2',
    fullName: 'PlayStation 2',
    extensions: ['zso'],
    also: ['iso', 'chd', 'cso', 'bin', 'img', 'gz', 'mdf', 'nrg', 'isz'],
    color: Color(0xFF1D4ED8),
    external: true,
    hints: ['ps2', 'playstation2', 'sonyplaystation2'],
  ),
  psp(
    name: 'PSP',
    fullName: 'PlayStation Portable',
    extensions: ['cso'],
    also: ['iso', 'chd', 'pbp'],
    color: Color(0xFF475569),
    external: true,
    hints: ['psp', 'playstationportable'],
  ),
  gcwii(
    name: 'GameCube / Wii',
    fullName: 'GameCube / Wii',
    extensions: ['gcm', 'gcz', 'rvz', 'wbfs', 'wia', 'tgc'],
    also: ['iso', 'ciso'],
    color: Color(0xFF6D28D9),
    external: true,
    hints: ['gc', 'gamecube', 'ngc', 'wii', 'nintendogamecube', 'nintendowii'],
  ),
  wiiu(
    name: 'Wii U',
    fullName: 'Wii U',
    extensions: ['wua', 'wud', 'wux', 'rpx', 'wuhb'],
    color: Color(0xFF0891B2),
    external: true,
    hints: ['wiiu', 'nintendowiiu'],
  ),
  n3ds(
    name: '3DS',
    fullName: 'Nintendo 3DS',
    extensions: ['3ds', 'cci', 'cxi', '3dsx', 'z3dsx', 'zcci', 'zcxi'],
    color: Color(0xFFDC2626),
    external: true,
    hints: ['3ds', 'n3ds', 'nintendo3ds'],
  ),
  nswitch(
    name: 'Switch',
    fullName: 'Nintendo Switch',
    extensions: ['nsp', 'xci', 'nro'],
    color: Color(0xFF14B8C8),
    external: true,
    hints: ['switch', 'nsw', 'nintendoswitch'],
  ),
  xbox(
    name: 'Xbox',
    fullName: 'Xbox',
    extensions: ['xiso'],
    also: ['iso'],
    color: Color(0xFF65A30D),
    external: true,
    hints: ['xbox', 'microsoftxbox', 'originalxbox'],
  ),
  xbox360(
    name: 'Xbox 360',
    fullName: 'Xbox 360',
    extensions: ['xex', 'zar'],
    also: ['iso'],
    color: Color(0xFF22C55E),
    external: true,
    hints: ['xbox360', 'x360', '360', 'microsoftxbox360'],
  ),
  ps3(
    name: 'PS3',
    fullName: 'PlayStation 3',
    extensions: ['ps3'],
    also: ['iso'],
    color: Color(0xFF111827),
    external: true,
    hints: ['ps3', 'playstation3', 'sonyplaystation3'],
  ),
  vita(
    name: 'Vita',
    fullName: 'PlayStation Vita (installed in Vita3K)',
    extensions: ['psvita'],
    color: Color(0xFF3730A3),
    external: true,
    hints: ['psvita', 'vita', 'playstationvita'],
  );

  const Console({
    required this.name,
    required this.fullName,
    required this.extensions,
    this.also = const [],
    this.core = '',
    this.ejsCore = '',
    this.inHouse = '',
    required this.color,
    this.face = const [],
    this.shoulders = const [],
    this.external = false,
    this.touchscreen = false,
    this.hints = const [],
  });

  final String name;
  final String fullName;

  /// Extensions that identify this system on their own.
  final List<String> extensions;

  /// Shared extensions this system also uses (".iso", ".bin" ...). A file
  /// with one of these is assigned by disc sniffing or by folder name.
  final List<String> also;

  /// libretro core base name, e.g. "fceumm" -> fceumm_libretro.so; empty
  /// when RomRunner can't emulate the system itself.
  final String core;

  /// EmulatorJS core id used by the web build; empty if unsupported there.
  final String ejsCore;

  /// RomRunner's own core for a system that otherwise opens in another app
  /// (cores/ in the repo), offered as an experimental choice.
  final String inHouse;
  final Color color;
  final List<PadButton> face;
  final List<PadButton> shoulders;

  /// Native platforms hand these games to an installed emulator app.
  final bool external;

  /// Has a touchscreen the user taps on the game image (DS).
  final bool touchscreen;

  /// Normalised folder names that mean "this system" (see [fromFolderName]).
  final List<String> hints;

  /// Short id safe for folder and file names.
  String get slug => name.toLowerCase().replaceAll(RegExp(r'[^a-z0-9]+'), '_');

  bool accepts(String ext) => extensions.contains(ext) || also.contains(ext);

  /// Whether this build plays the system itself.
  bool get playsHere => kIsWeb ? ejsCore.isNotEmpty : core.isNotEmpty;

  /// Whether this build can offer RomRunner's own core for the system.
  bool get hasInHouse => !kIsWeb && inHouse.isNotEmpty;

  /// Whether this build opens the system in another app.
  bool get launchesExternally => !kIsWeb && external && core.isEmpty;

  /// Systems with a libretro core (they get a folder under roms/).
  static List<Console> get builtIn => values.where((c) => c.core.isNotEmpty).toList();

  /// Extensions that could belong to more than one system (asked on import
  /// when sniffing and folder names don't settle it).
  static const ambiguous = {'bin', 'zip', '7z', 'img', 'iso', 'ciso'};

  /// Companion files that are imported but never listed as games.
  static const companions = {'bin', 'img', 'sub', 'sbi'};

  /// The one system an extension belongs to, if it isn't shared.
  static Console? fromExtension(String ext) {
    final e = ext.toLowerCase();
    for (final c in Console.values) {
      if (c.extensions.contains(e)) return c;
    }
    return null;
  }

  /// The system named by a folder ("Mega Drive", "PSX", "Switch" ...).
  static Console? fromFolderName(String folder) {
    final n = folder.toLowerCase().replaceAll(RegExp(r'[^a-z0-9]'), '');
    if (n.isEmpty) return null;
    for (final c in Console.values) {
      if (c.hints.contains(n)) return c;
    }
    return null;
  }

  static List<String> get allExtensions => [
        for (final c in Console.values) ...c.extensions,
        ...ambiguous,
      ];
}
