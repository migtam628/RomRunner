import 'package:flutter_test/flutter_test.dart';
import 'package:romrunner/models/console.dart';
import 'package:romrunner/services/cover_names.dart';

void main() {
  // Real entries from thumbnails.libretro.com/Nintendo - Game Boy/Named_Boxarts
  const gb = [
    'Tetris (Japan) (En)',
    'Tetris (World) (Rev 1)',
    'Tetris 2 (USA)',
    'Tetris 2 (USA, Europe) (Rev 1) (SGB Enhanced)',
    'Pokemon - Red Version (USA, Europe) (SGB Enhanced)',
    'Legend of Zelda, The - Link\'s Awakening (USA, Europe) (Rev 2)',
    'Super Mario Land (World) (Rev 1)',
  ];

  test('exact No-Intro names win', () {
    expect(pickCover('Tetris (World) (Rev 1).gb', gb), 'Tetris (World) (Rev 1)');
  });

  test('same title, best region', () {
    expect(pickCover('Tetris.gb', gb), 'Tetris (World) (Rev 1)');
    expect(pickCover('Tetris 2 (Europe).gb', gb), 'Tetris 2 (USA)');
    expect(pickCover('The Legend of Zelda - Links Awakening.gb', gb),
        'Legend of Zelda, The - Link\'s Awakening (USA, Europe) (Rev 2)');
    expect(pickCover('super_mario_land.gb'.replaceAll('_', ' '), gb), 'Super Mario Land (World) (Rev 1)');
  });

  test('close word match, but no false friends', () {
    expect(pickCover('Pokemon Red.gb', gb), 'Pokemon - Red Version (USA, Europe) (SGB Enhanced)');
    expect(pickCover('Pokemon Blue.gb', gb), isNull);
    expect(pickCover('cpu_instrs.gb', gb), isNull);
  });

  test('special characters are replaced like libretro does', () {
    expect(thumbnailFileName('Dr. Mario: The Game?'), 'Dr. Mario_ The Game_');
    expect(pickCover('Ratchet & Clank (USA).iso', ['Ratchet _ Clank (USA)']), 'Ratchet _ Clank (USA)');
  });

  test('PS3 folders and system folders', () {
    expect(gameBaseName("Demon's Souls [BLUS30443]"), "Demon's Souls [BLUS30443]");
    expect(pickCover("Demon's Souls [BLUS30443]", ["Demon's Souls (USA)"]), "Demon's Souls (USA)");
    expect(thumbnailSystems(Console.gb, 'x.gbc').first, 'Nintendo - Game Boy Color');
    expect(thumbnailSystems(Console.genesis, 'x.sms'), ['Sega - Master System - Mark III']);
    expect(thumbnailSystems(Console.nswitch, 'x.nsp'), isEmpty);
  });
}
