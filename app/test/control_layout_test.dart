import 'dart:ui';

import 'package:flutter_test/flutter_test.dart';
import 'package:romrunner/models/console.dart';
import 'package:romrunner/models/control_layout.dart';

void main() {
  const phonePortrait = Size(412, 860);
  const phoneLandscape = Size(860, 412);

  test('default controls stay on screen and do not overlap', () {
    for (final c in [...Console.builtIn, Console.n64]) {
      for (final area in [phonePortrait, phoneLandscape]) {
        final l = ControlLayout();
        final specs = controlSpecs(c);
        final rects = {for (final s in specs) s.id: l.boundsOf(s, c, area)};
        for (final MapEntry(key: id, value: r) in rects.entries) {
          expect((Offset.zero & area).inflate(2).contains(r.topLeft) && (Offset.zero & area).inflate(2).contains(r.bottomRight),
              isTrue, reason: '${c.name} $id $area $r');
        }
        final ids = rects.keys.toList();
        for (var i = 0; i < ids.length; i++) {
          for (var j = i + 1; j < ids.length; j++) {
            final a = rects[ids[i]]!.deflate(2), b = rects[ids[j]]!.deflate(2);
            expect(a.overlaps(b), isFalse, reason: '${c.name} ${ids[i]} vs ${ids[j]} $area');
          }
        }
      }
    }
  });

  test('screen: fits width at the top in portrait, centred in landscape', () {
    final l = ControlLayout();
    final p = l.screenRect(phonePortrait, 4 / 3, 240);
    expect(p.width, 412);
    expect(p.top, 0);
    final land = l.screenRect(phoneLandscape, 4 / 3, 240);
    expect(land.height, 412);
    expect(land.center.dx, closeTo(430, 0.01));
    expect(l.screenRect(phonePortrait, 4 / 3, 240, centered: true).center.dy, closeTo(430, 0.01));
  });

  test('screen: resize, move, integer and stretch', () {
    final l = ControlLayout()..screenScale = 0.5;
    var r = l.screenRect(phonePortrait, 4 / 3, 240);
    expect(r.width, 206);
    l.moveScreen(const Offset(1000, 0), phonePortrait, r);
    r = l.screenRect(phonePortrait, 4 / 3, 240);
    expect(r.right, closeTo(412, 0.01));
    l.fit = ScreenFit.integer;
    r = l.screenRect(phonePortrait, 4 / 3, 224); // SNES: 224 lines
    expect(r.height, 224); // 412 / (224 * 4/3) = 1.38 -> 1x
    l.fit = ScreenFit.stretch;
    expect(l.screenRect(phonePortrait, 4 / 3, 224), Offset.zero & phonePortrait);
  });

  test('json round trip keeps edits', () {
    final c = Console.snes;
    final l = ControlLayout()
      ..opacity = 0.5
      ..fit = ScreenFit.integer
      ..screenY = 0.3;
    final s = controlSpecs(c).firstWhere((s) => s.id == 'face_a');
    l.pin(s, c, phonePortrait)
      ..x = 0.5
      ..scale = 1.4
      ..hidden = true;
    final back = ControlLayout.fromJson(l.toJson());
    expect(back.opacity, 0.5);
    expect(back.fit, ScreenFit.integer);
    expect(back.screenY, 0.3);
    expect(back.items['face_a']!.x, 0.5);
    expect(back.scaleOf('face_a'), 1.4);
    expect(back.hiddenOf('face_a'), isTrue);
    expect(ControlLayout.fromJson({'opacity': 9, 'items': {'bad': 1}}).opacity, 1.0);
  });
}
