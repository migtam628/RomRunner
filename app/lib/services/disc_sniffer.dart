// Tells disc images apart by reading a few KB of them (native only).
//
// .iso/.bin/.img are shared by many systems. Instead of asking, look at
// what's on the disc:
//   GameCube / Wii   magic words in the disc header (0x1C / 0x18)
//   Xbox / Xbox 360  XDVDFS "MICROSOFT*XBOX*MEDIA"; default.xbe vs default.xex
//   ISO 9660 discs   PSP ("PSP GAME" / PSP_GAME), PS3 (PS3_GAME),
//                    PS2 (SYSTEM.CNF with BOOT2), PS1 (SYSTEM.CNF with BOOT)
//   Sega CD          "SEGADISCSYSTEM" in the first sector
//   .pbp             PS1 eboot (PSISOIMG) vs PSP
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import '../models/console.dart';

const _sniffable = {'iso', 'bin', 'img', 'pbp', 'ciso', 'cue'};

bool canSniff(String ext) => _sniffable.contains(ext);

final _cache = <String, Console?>{};

/// The system [path] belongs to, or null if it can't be told.
Future<Console?> sniffDisc(String path) async {
  final file = File(path);
  final FileStat stat;
  try {
    stat = file.statSync();
  } catch (_) {
    return null;
  }
  final key = '$path|${stat.size}|${stat.modified.millisecondsSinceEpoch}';
  if (_cache.containsKey(key)) return _cache[key];
  Console? result;
  try {
    final ext = path.split('.').last.toLowerCase();
    if (ext == 'cue') {
      result = await _sniffCue(file);
    } else {
      final raf = await file.open();
      try {
        result = ext == 'pbp' ? await _sniffPbp(raf) : await _sniffImage(raf, stat.size);
      } finally {
        await raf.close();
      }
    }
  } catch (_) {
    result = null;
  }
  return _cache[key] = result;
}

Future<Uint8List> _read(RandomAccessFile f, int offset, int length) async {
  await f.setPosition(offset);
  return f.read(length);
}

int _u32le(Uint8List b, int o) => b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24;
int _u32be(Uint8List b, int o) => b[o] << 24 | b[o + 1] << 16 | b[o + 2] << 8 | b[o + 3];

bool _has(Uint8List b, int o, String ascii) {
  if (o + ascii.length > b.length) return false;
  for (var i = 0; i < ascii.length; i++) {
    if (b[o + i] != ascii.codeUnitAt(i)) return false;
  }
  return true;
}

/// A .cue points at track files; sniff the first one.
Future<Console?> _sniffCue(File cue) async {
  final text = await cue.readAsString(encoding: latin1);
  final m = RegExp(r'FILE\s+"([^"]+)"', caseSensitive: false).firstMatch(text);
  if (m == null) return null;
  final track = File('${cue.parent.path}${Platform.pathSeparator}${m.group(1)}');
  if (!track.existsSync()) return null;
  final raf = await track.open();
  try {
    return await _sniffImage(raf, track.lengthSync());
  } finally {
    await raf.close();
  }
}

Future<Console?> _sniffPbp(RandomAccessFile f) async {
  final h = await _read(f, 0, 0x28);
  if (h.length < 0x28 || !_has(h, 1, 'PBP')) return null;
  final psar = await _read(f, _u32le(h, 0x24), 16);
  if (_has(psar, 0, 'PSISOIMG') || _has(psar, 0, 'PSTITLEIMG')) return Console.ps1;
  return Console.psp;
}

Future<Console?> _sniffImage(RandomAccessFile f, int size) async {
  final head = await _read(f, 0, 0x40);
  if (head.length < 0x20) return null;

  // GameCube / Wii disc header.
  if (_u32be(head, 0x1C) == 0xC2339F3D || _u32be(head, 0x18) == 0x5D1C9EA3) return Console.gcwii;

  // Sega CD: "SEGADISCSYSTEM" at the start of user data.
  if (_has(head, 0, 'SEGADISCSYSTEM') || _has(head, 16, 'SEGADISCSYSTEM')) return Console.segacd;

  // Xbox / Xbox 360 (XDVDFS): plain XISO, or a full dump with a video partition.
  for (final base in const [0x0, 0x18300000, 0x0FD90000, 0x02080000]) {
    final xbox = await _sniffXdvdfs(f, base, size);
    if (xbox != null) return xbox;
  }

  // ISO 9660, either 2048-byte sectors or raw 2352-byte CD sectors.
  final layouts = <({int sector, int data})>[(sector: 2048, data: 0)];
  final sync = head.length >= 16 && head[0] == 0 && head.sublist(1, 11).every((b) => b == 0xFF) && head[11] == 0;
  if (sync) layouts.insert(0, (sector: 2352, data: head[15] == 2 ? 24 : 16));
  for (final l in layouts) {
    final c = await _sniffIso9660(f, l.sector, l.data);
    if (c != null) return c;
  }
  return null;
}

Future<Console?> _sniffXdvdfs(RandomAccessFile f, int base, int size) async {
  if (base + 0x10000 + 0x800 > size) return null;
  final vd = await _read(f, base + 0x10000, 0x20);
  if (!_has(vd, 0, 'MICROSOFT*XBOX*MEDIA')) return null;
  final rootSector = _u32le(vd, 0x14);
  final rootSize = _u32le(vd, 0x18).clamp(0, 0x10000);
  final dir = await _read(f, base + rootSector * 2048, rootSize);
  final names = latin1.decode(dir, allowInvalid: true).toLowerCase();
  if (names.contains('default.xex')) return Console.xbox360;
  if (names.contains('default.xbe')) return Console.xbox;
  // Dumps with a video partition (XGD2/XGD3) are Xbox 360 discs.
  return base == 0x18300000 || base == 0 ? Console.xbox : Console.xbox360;
}

Future<Console?> _sniffIso9660(RandomAccessFile f, int sector, int data) async {
  Future<Uint8List> readSector(int lba, int count) async {
    if (sector == 2048) return _read(f, lba * 2048, count * 2048);
    final out = BytesBuilder();
    for (var i = 0; i < count; i++) {
      out.add(await _read(f, (lba + i) * sector + data, 2048));
    }
    return out.toBytes();
  }

  final pvd = await readSector(16, 1);
  if (pvd.length < 2048 || pvd[0] != 1 || !_has(pvd, 1, 'CD001')) return null;
  final systemId = latin1.decode(pvd.sublist(8, 40)).trim();
  if (systemId.startsWith('PSP GAME')) return Console.psp;

  // Root directory record at offset 156 of the PVD.
  final rootLba = _u32le(pvd, 156 + 2);
  final rootLen = _u32le(pvd, 156 + 10).clamp(0, 64 * 1024);
  final dir = await readSector(rootLba, (rootLen + 2047) ~/ 2048);

  final entries = <String, ({int lba, int size})>{};
  var o = 0;
  while (o < dir.length) {
    final len = dir[o];
    if (len == 0) {
      o = (o ~/ 2048 + 1) * 2048; // records don't cross sector boundaries
      continue;
    }
    if (o + 33 > dir.length) break;
    final nameLen = dir[o + 32];
    if (o + 33 + nameLen > dir.length) break;
    final name = latin1.decode(dir.sublist(o + 33, o + 33 + nameLen)).toUpperCase().split(';').first;
    entries[name] = (lba: _u32le(dir, o + 2), size: _u32le(dir, o + 10));
    o += len;
  }

  if (entries.containsKey('PSP_GAME') || entries.containsKey('UMD_DATA.BIN')) return Console.psp;
  if (entries.containsKey('PS3_GAME') || entries.containsKey('PS3_DISC.SFB')) return Console.ps3;
  final cnf = entries['SYSTEM.CNF'];
  if (cnf != null) {
    final text = latin1.decode(await readSector(cnf.lba, 1), allowInvalid: true).toUpperCase();
    if (text.contains('BOOT2')) return Console.ps2;
    if (text.contains('BOOT')) return Console.ps1;
  }
  if (systemId.startsWith('PLAYSTATION')) return sector == 2048 ? Console.ps2 : Console.ps1;
  return null;
}

/// Whether [dir] is an extracted PS3 disc or PSN game folder.
bool isPs3GameFolder(String dir) {
  final sep = Platform.pathSeparator;
  return File('$dir${sep}PS3_GAME${sep}USRDIR${sep}EBOOT.BIN').existsSync() ||
      File('$dir${sep}USRDIR${sep}EBOOT.BIN').existsSync() && File('$dir${sep}PARAM.SFO').existsSync();
}
