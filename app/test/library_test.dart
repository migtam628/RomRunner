import 'package:flutter_test/flutter_test.dart';
import 'package:romrunner/models/console.dart';
import 'package:romrunner/models/game.dart';
import 'package:romrunner/services/library.dart';
import 'package:romrunner/services/switch_support.dart';

void main() {
  test('extensions map to consoles', () {
    expect(Console.fromExtension('NES'), Console.nes);
    expect(Console.fromExtension('gbc'), Console.gb);
    expect(Console.fromExtension('sfc'), Console.snes);
    expect(Console.fromExtension('chd'), Console.ps1);
    expect(Console.fromExtension('bin'), isNull);
  });

  test('cue sheets claim their bin tracks; lone bins ask', () async {
    final asked = <String>[];
    final result = await classifyFiles(
      ['Game (USA).cue', 'Game (USA) (Track 1).bin', 'Game (USA) (Track 2).bin'],
      (name) async { asked.add(name); return null; },
    );
    expect(asked, isEmpty);
    expect(result.values.toSet(), {Console.ps1});

    final lone = await classifyFiles(['Sonic.bin'], (name) async => Console.genesis);
    expect(lone, {'Sonic.bin': Console.genesis});
  });

  test('BIOS names are recognised', () {
    expect(biosPattern.hasMatch('SCPH5501.BIN'), isTrue);
    expect(biosPattern.hasMatch('gba_bios.bin'), isTrue);
    expect(biosPattern.hasMatch('Super Game.bin'), isFalse);
  });

  test('titles drop dump tags', () {
    expect(Game.titleFromFile('Super Mario Bros. 3 (USA) [!].nes'), 'Super Mario Bros. 3');
    expect(Game.titleFromFile('cpu_instrs.gb'), 'cpu instrs');
  });

  test('pad bits match libretro joypad ids', () {
    expect(Pad.b.bit, 1 << 0);
    expect(Pad.a.bit, 1 << 8);
    expect(Pad.r3.bit, 1 << 15);
  });

  test('Switch files are recognised but not emulated', () {
    expect(Console.fromExtension('nsp'), Console.nswitch);
    expect(Console.fromExtension('XCI'), Console.nswitch);
    expect(Console.nswitch.external, isTrue);
    expect(Console.builtIn, isNot(contains(Console.nswitch)));
  });

  test('Switch launch arguments per emulator family', () {
    expect(switchLaunchArgs('/usr/bin/eden', '/g/Game.nsp'), ['-f', '-g', '/g/Game.nsp']);
    expect(switchLaunchArgs(r'C:\Emu\citron.exe', r'D:\Game.xci'), ['-f', '-g', r'D:\Game.xci']);
    expect(switchLaunchArgs('/Applications/Ryujinx.app', '/g/Game.nsp'), ['/g/Game.nsp', '--fullscreen']);
    expect(switchLaunchArgs('/home/me/Ryubing-1.3.AppImage', '/g/a.nsp'), ['/g/a.nsp', '--fullscreen']);
  });

  test('folder names pick the system for ambiguous files', () {
    expect(consoleFromFolderName('/roms/Sega Genesis/Sonic.bin'), Console.genesis);
    expect(consoleFromFolderName(r'D:\Games\Mega Drive\USA\Sonic.bin'), Console.genesis);
    expect(consoleFromFolderName('/roms/PSX/Crash/Crash.bin'), Console.ps1);
    expect(consoleFromFolderName('/home/me/Downloads/Sonic.bin'), isNull);
  });

  test('folder scan: sheets hide tracks, hints place .bin, archives skipped', () {
    final scan = classifyFolderPaths([
      '/r/NES/Zelda (USA).nes',
      '/r/PS1/Crash/Crash.cue',
      '/r/PS1/Crash/Crash (Track 1).bin',
      '/r/PS1/Crash/Crash (Track 2).bin',
      '/r/Genesis/Sonic.bin',
      '/r/misc/Unknown.bin',
      '/r/misc/Pack.zip',
      '/r/misc/readme.txt',
      '/r/misc/._Zelda.nes',
      '/r/bios/scph5501.bin',
      '/r/Switch/Game [0100][v0].nsp',
    ]);
    expect(scan.games, {
      '/r/NES/Zelda (USA).nes': Console.nes,
      '/r/PS1/Crash/Crash.cue': Console.ps1,
      '/r/Genesis/Sonic.bin': Console.genesis,
      '/r/Switch/Game [0100][v0].nsp': Console.nswitch,
    });
    expect(scan.skipped, 2); // Unknown.bin + Pack.zip
  });
}
