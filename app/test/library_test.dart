import 'dart:io';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:romrunner/models/console.dart';
import 'package:romrunner/models/game.dart';
import 'package:romrunner/services/disc_sniffer.dart';
import 'package:romrunner/services/external_catalog.dart';
import 'package:romrunner/services/library.dart';

/// A 2048-byte-sector ISO 9660 image with the given root entries.
Uint8List iso9660({String systemId = '', Map<String, String> files = const {}}) {
  final img = Uint8List(2048 * 24);
  final pvd = 16 * 2048;
  img[pvd] = 1;
  img.setAll(pvd + 1, 'CD001'.codeUnits);
  img.setAll(pvd + 8, systemId.padRight(32).codeUnits);
  const rootLba = 20;
  void u32(int o, int v) => img.buffer.asByteData().setUint32(o, v, Endian.little);
  u32(pvd + 156 + 2, rootLba);
  u32(pvd + 156 + 10, 2048);
  var o = rootLba * 2048;
  var dataLba = 21;
  for (final MapEntry(key: name, value: content) in files.entries) {
    final n = '$name;1'.codeUnits;
    final len = 33 + n.length + (n.length.isEven ? 1 : 0);
    img[o] = len;
    u32(o + 2, dataLba);
    u32(o + 10, content.length);
    img[o + 32] = n.length;
    img.setAll(o + 33, n);
    img.setAll(dataLba * 2048, content.codeUnits);
    o += len;
    dataLba++;
  }
  return img;
}

void main() {
  test('unique extensions map to systems; shared ones do not', () {
    expect(Console.fromExtension('NES'), Console.nes);
    expect(Console.fromExtension('nds'), Console.nds);
    expect(Console.fromExtension('a26'), Console.a2600);
    expect(Console.fromExtension('rvz'), Console.gcwii);
    expect(Console.fromExtension('xex'), Console.xbox360);
    expect(Console.fromExtension('chd'), Console.ps1);
    expect(Console.fromExtension('iso'), isNull);
    expect(Console.fromExtension('bin'), isNull);
  });

  test('which systems are built in vs external', () {
    expect(Console.ps2.launchesExternally, isTrue);
    expect(Console.n64.launchesExternally, isTrue);
    expect(Console.nds.launchesExternally, isFalse);
    expect(Console.builtIn, contains(Console.arcade));
    expect(Console.builtIn, isNot(contains(Console.nswitch)));
  });

  test('folder names pick the system', () async {
    expect(consoleFromFolderName('/roms/Sega Genesis/Sonic.bin'), Console.genesis);
    expect(consoleFromFolderName(r'D:\Games\Mega Drive\USA\Sonic.bin'), Console.genesis);
    expect(consoleFromFolderName('/roms/PlayStation 2/GT4.iso'), Console.ps2);
    expect(consoleFromFolderName('/home/me/Downloads/Sonic.bin'), isNull);
    expect(await identify('/roms/PS2/game.chd'), Console.ps2); // hint beats the PS1 default
    expect(await identify('/roms/SNES/oops.nes'), Console.nes); // hint must accept the extension
  });

  test('cue sheets keep their tracks; lone bins ask', () async {
    final asked = <String>[];
    final result = await classifyFiles(
      ['Game (USA).cue', 'Game (USA) (Track 1).bin', 'Game (USA) (Track 2).bin'],
      (name) async {
        asked.add(name);
        return null;
      },
    );
    expect(asked, isEmpty);
    expect(result.values.toSet(), {Console.ps1});
    final lone = await classifyFiles(['Sonic.bin'], (name) async => Console.genesis);
    expect(lone, {'Sonic.bin': Console.genesis});
  });

  test('folder scan: sheets hide tracks, hints place .bin, arcade zips only in arcade folders', () async {
    final scan = await classifyFolderPaths([
      '/r/NES/Zelda (USA).nes',
      '/r/PS1/Crash/Crash.cue',
      '/r/PS1/Crash/Crash (Track 1).bin',
      '/r/Genesis/Sonic.bin',
      '/r/misc/Unknown.bin',
      '/r/misc/Pack.zip',
      '/r/Arcade/sf2.zip',
      '/r/Arcade/neogeo.zip',
      '/r/misc/readme.txt',
      '/r/misc/._Zelda.nes',
      '/r/bios/scph5501.bin',
      '/r/Switch/Game [0100][v0].nsp',
      '/r/PS2/Gran Turismo 4.iso',
    ]);
    expect(scan.games, {
      '/r/NES/Zelda (USA).nes': Console.nes,
      '/r/PS1/Crash/Crash.cue': Console.ps1,
      '/r/Genesis/Sonic.bin': Console.genesis,
      '/r/Arcade/sf2.zip': Console.arcade,
      '/r/Switch/Game [0100][v0].nsp': Console.nswitch,
      '/r/PS2/Gran Turismo 4.iso': Console.ps2,
    });
    expect(scan.skipped, 2); // Unknown.bin + Pack.zip
  });

  group('disc sniffing', () {
    late Directory dir;
    setUp(() => dir = Directory.systemTemp.createTempSync('sniff'));
    tearDown(() => dir.deleteSync(recursive: true));

    Future<Console?> sniff(String name, List<int> bytes) async {
      final f = File('${dir.path}/$name')..writeAsBytesSync(bytes);
      return sniffDisc(f.path);
    }

    test('PS2 / PS1 / PSP / PS3 ISO 9660', () async {
      expect(await sniff('a.iso', iso9660(systemId: 'PLAYSTATION', files: {'SYSTEM.CNF': 'BOOT2 = cdrom0:\\SLUS_200.62;1'})), Console.ps2);
      expect(await sniff('b.iso', iso9660(systemId: 'PLAYSTATION', files: {'SYSTEM.CNF': 'BOOT = cdrom:\\SCUS_944.26;1'})), Console.ps1);
      expect(await sniff('c.iso', iso9660(systemId: 'PSP GAME')), Console.psp);
      expect(await sniff('d.iso', iso9660(files: {'PS3_DISC.SFB': 'x'})), Console.ps3);
      expect(await sniff('e.iso', iso9660(files: {'README.TXT': 'hi'})), isNull);
    });

    test('GameCube / Wii headers', () async {
      final gc = Uint8List(0x10000)..buffer.asByteData().setUint32(0x1C, 0xC2339F3D);
      final wii = Uint8List(0x10000)..buffer.asByteData().setUint32(0x18, 0x5D1C9EA3);
      expect(await sniff('g.iso', gc), Console.gcwii);
      expect(await sniff('w.iso', wii), Console.gcwii);
    });

    test('Xbox vs Xbox 360 XISO', () async {
      Uint8List xiso(String exe) {
        final img = Uint8List(0x12000);
        img.setAll(0x10000, 'MICROSOFT*XBOX*MEDIA'.codeUnits);
        img.buffer.asByteData()
          ..setUint32(0x10014, 33, Endian.little)
          ..setUint32(0x10018, 2048, Endian.little);
        img.setAll(33 * 2048 + 14, exe.codeUnits);
        return img;
      }

      expect(await sniff('x.iso', xiso('default.xbe')), Console.xbox);
      expect(await sniff('y.iso', xiso('default.xex')), Console.xbox360);
    });

    test('Sega CD and PBP', () async {
      expect(await sniff('s.iso', [...'SEGADISCSYSTEM  '.codeUnits, ...List.filled(64, 0)]), Console.segacd);
      Uint8List pbp(String psar) {
        final b = Uint8List(0x200);
        b.setAll(0, [0, ...'PBP'.codeUnits]);
        b.buffer.asByteData().setUint32(0x24, 0x100, Endian.little);
        b.setAll(0x100, psar.codeUnits);
        return b;
      }

      expect(await sniff('ps1.pbp', pbp('PSISOIMG0000')), Console.ps1);
      expect(await sniff('psp.pbp', pbp('NPUMDIMG')), Console.psp);
    });
  });

  test('emulator command lines come from the catalog', () {
    ExternalEmulator e(String id) => emulatorById(id)!;
    expect(e('pcsx2').argsFor(HostOs.windows, rom: 'G.iso'), ['-batch', '-fullscreen', 'G.iso']);
    expect(e('dolphin').argsFor(HostOs.linux, rom: 'G.rvz'), ['-b', '-e', 'G.rvz']);
    expect(e('cemu').argsFor(HostOs.windows, rom: 'G.wua'), ['-f', '-g', 'G.wua']);
    expect(e('cemu').argsFor(HostOs.linux, rom: 'G.wua'), ['-g', 'G.wua']);
    expect(e('eden').argsFor(HostOs.macos, rom: 'G.nsp'), ['-f', '-g', 'G.nsp']);
    expect(e('ryujinx').argsFor(HostOs.linux, rom: 'G.nsp'), ['G.nsp', '--fullscreen']);
    expect(e('vita3k').argsFor(HostOs.windows, rom: 'x', injected: 'PCSE00000'), ['-r', 'PCSE00000']);
    expect(emulatorsFor(Console.xbox360, HostOs.windows).map((x) => x.id), contains('xenia'));
    expect(emulatorsFor(Console.n64, HostOs.android).map((x) => x.id), ['m64plusfz']);
    expect(guessEmulator(Console.ps2, '/home/me/pcsx2-v2.2.0-linux-appimage-x64-Qt.AppImage')?.id, 'pcsx2');
    expect(guessEmulator(Console.nswitch, r'C:\Emu\Ryujinx.exe')?.id, 'ryujinx');
  });

  test('BIOS names are recognised', () {
    expect(biosPattern.hasMatch('SCPH5501.BIN'), isTrue);
    expect(biosPattern.hasMatch('lynxboot.img'), isTrue);
    expect(biosPattern.hasMatch('bios7.bin'), isTrue);
    expect(biosPattern.hasMatch('Super Game.bin'), isFalse);
  });

  test('titles drop dump tags', () {
    expect(Game.titleFromFile('Super Mario Bros. 3 (USA) [!].nes'), 'Super Mario Bros. 3');
  });

  test('pad bits match libretro joypad ids', () {
    expect(Pad.b.bit, 1 << 0);
    expect(Pad.a.bit, 1 << 8);
  });

  test('Android manifest can see every catalog emulator package', () {
    final manifest = File('android/app/src/main/AndroidManifest.xml').readAsStringSync();
    for (final pkg in allAndroidPackages) {
      expect(manifest, contains('<package android:name="$pkg" />'), reason: pkg);
    }
  });
}
