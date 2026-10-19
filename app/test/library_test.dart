import 'package:flutter_test/flutter_test.dart';
import 'package:romrunner/models/console.dart';
import 'package:romrunner/models/game.dart';
import 'package:romrunner/services/library.dart';

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
}
