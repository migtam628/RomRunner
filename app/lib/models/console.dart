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

enum Console {
  nes(
    name: 'NES',
    fullName: 'Nintendo Entertainment System',
    extensions: ['nes', 'unf', 'unif', 'fds'],
    core: 'fceumm',
    ejsCore: 'nes',
    color: Color(0xFFE5484D),
    face: [PadButton('B', Pad.b), PadButton('A', Pad.a)],
  ),
  gb(
    name: 'Game Boy',
    fullName: 'Game Boy / Game Boy Color',
    extensions: ['gb', 'gbc', 'dmg', 'cgb', 'sgb'],
    core: 'gambatte',
    ejsCore: 'gb',
    color: Color(0xFF8BB33A),
    face: [PadButton('B', Pad.b), PadButton('A', Pad.a)],
  ),
  gba(
    name: 'GBA',
    fullName: 'Game Boy Advance',
    extensions: ['gba', 'agb'],
    core: 'mgba',
    ejsCore: 'gba',
    color: Color(0xFF7C5CFF),
    face: [PadButton('B', Pad.b), PadButton('A', Pad.a)],
    shoulders: [PadButton('L', Pad.l), PadButton('R', Pad.r)],
  ),
  snes(
    name: 'SNES',
    fullName: 'Super Nintendo',
    extensions: ['sfc', 'smc', 'swc', 'fig'],
    core: 'snes9x',
    ejsCore: 'snes',
    color: Color(0xFF9B8FD9),
    face: [PadButton('Y', Pad.y), PadButton('X', Pad.x), PadButton('B', Pad.b), PadButton('A', Pad.a)],
    shoulders: [PadButton('L', Pad.l), PadButton('R', Pad.r)],
  ),
  genesis(
    name: 'Genesis',
    fullName: 'Genesis / Mega Drive / Master System / Game Gear',
    extensions: ['md', 'gen', 'smd', 'sms', 'gg', 'sg'],
    core: 'genesis_plus_gx',
    ejsCore: 'segaMD',
    color: Color(0xFF3B82F6),
    // Genesis Plus GX default 3-button mapping: A=Y, B=B, C=A.
    face: [PadButton('A', Pad.y), PadButton('B', Pad.b), PadButton('C', Pad.a)],
  ),
  ps1(
    name: 'PS1',
    fullName: 'PlayStation',
    extensions: ['cue', 'chd', 'pbp', 'm3u', 'ccd', 'iso'],
    core: 'pcsx_rearmed',
    ejsCore: 'psx',
    color: Color(0xFF94A3B8),
    face: [PadButton('□', Pad.y), PadButton('△', Pad.x), PadButton('✕', Pad.b), PadButton('○', Pad.a)],
    shoulders: [PadButton('L1', Pad.l), PadButton('R1', Pad.r), PadButton('L2', Pad.l2), PadButton('R2', Pad.r2)],
  ),
  // Not emulated by RomRunner: games are handed to an installed Switch
  // emulator (Eden, Citron, Ryujinx...). See services/switch_support.dart.
  nswitch(
    name: 'Switch',
    fullName: 'Nintendo Switch (opens in your Switch emulator)',
    extensions: ['nsp', 'xci', 'nro'],
    core: '',
    ejsCore: '',
    color: Color(0xFF14B8C8),
    face: [],
    external: true,
  );

  const Console({
    required this.name,
    required this.fullName,
    required this.extensions,
    required this.core,
    required this.ejsCore,
    required this.color,
    required this.face,
    this.shoulders = const [],
    this.external = false,
  });

  final String name;
  final String fullName;
  final List<String> extensions;

  /// libretro core base name, e.g. "fceumm" -> fceumm_libretro.so
  final String core;

  /// EmulatorJS core id used by the web build.
  final String ejsCore;
  final Color color;
  final List<PadButton> face;
  final List<PadButton> shoulders;

  /// True when games are launched in another app instead of a libretro core.
  final bool external;

  /// Systems RomRunner emulates itself.
  static List<Console> get builtIn => values.where((c) => !c.external).toList();

  /// Extensions that could belong to more than one console (asked on import).
  static const ambiguous = {'bin', 'zip', '7z', 'img'};

  /// Companion files that are imported but never listed as games.
  static const companions = {'bin', 'img', 'sub', 'sbi'};

  static Console? fromExtension(String ext) {
    final e = ext.toLowerCase();
    for (final c in Console.values) {
      if (c.extensions.contains(e)) return c;
    }
    return null;
  }

  static List<String> get allExtensions => [
        for (final c in Console.values) ...c.extensions,
        ...ambiguous,
      ];
}
