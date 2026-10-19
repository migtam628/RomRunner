import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../models/console.dart';

/// Multi-touch on-screen gamepad. Reports the pressed RetroPad buttons as a
/// bitmask (see [Pad.bit]) every time it changes.
class TouchController extends StatefulWidget {
  const TouchController({super.key, required this.console, required this.onChanged});

  final Console console;
  final ValueChanged<int> onChanged;

  @override
  State<TouchController> createState() => _TouchControllerState();
}

class _Hit {
  _Hit(this.center, this.radius, this.mask, this.label);
  final Offset center;
  final double radius;
  final int mask;
  final String label;
}

class _Layout {
  _Layout(this.dpad, this.dpadRadius, this.buttons);
  final Offset dpad;
  final double dpadRadius;
  final List<_Hit> buttons;
}

class _TouchControllerState extends State<TouchController> {
  final Map<int, Offset> _pointers = {};
  int _mask = 0;
  _Layout? _layout;

  _Layout _buildLayout(Size size) {
    final unit = math.min(size.shortestSide / 7.5, 72.0);
    final bottom = size.height - unit * 1.9;
    final dpad = Offset(unit * 1.9, bottom);
    final buttons = <_Hit>[];

    // Face buttons: 2 in a diagonal, 3 in an arc, 4 in a diamond.
    final face = widget.console.face;
    final fc = Offset(size.width - unit * 1.9, bottom);
    final r = unit * 0.52;
    List<Offset> spots;
    switch (face.length) {
      case 2:
        spots = [fc + Offset(-unit * 0.6, unit * 0.35), fc + Offset(unit * 0.6, -unit * 0.35)];
      case 3:
        spots = [fc + Offset(-unit * 1.15, unit * 0.3), fc + Offset(0, 0), fc + Offset(unit * 1.15, -unit * 0.3)];
      default:
        // Order in Console.face for 4 buttons: left, top, bottom, right.
        spots = [fc + Offset(-unit, 0), fc + Offset(0, -unit), fc + Offset(0, unit), fc + Offset(unit, 0)];
    }
    for (var i = 0; i < face.length && i < spots.length; i++) {
      buttons.add(_Hit(spots[i], r, face[i].pad.bit, face[i].label));
    }

    // Start / Select at the bottom center.
    final sy = size.height - unit * 0.55;
    buttons.add(_Hit(Offset(size.width / 2 - unit * 0.75, sy), unit * 0.32, Pad.select.bit, 'SELECT'));
    buttons.add(_Hit(Offset(size.width / 2 + unit * 0.75, sy), unit * 0.32, Pad.start.bit, 'START'));

    // Shoulders above each cluster.
    final sh = widget.console.shoulders;
    final shY = bottom - unit * 2.3;
    for (var i = 0; i < sh.length; i++) {
      final left = i.isEven;
      final row = i ~/ 2;
      final x = left ? unit * (1.0 + row * 1.3) : size.width - unit * (1.0 + row * 1.3);
      buttons.add(_Hit(Offset(x, shY), unit * 0.42, sh[i].pad.bit, sh[i].label));
    }
    return _Layout(dpad, unit * 1.35, buttons);
  }

  int _maskFor(Offset pos) {
    final l = _layout!;
    var mask = 0;
    final d = pos - l.dpad;
    if (d.distance < l.dpadRadius * 1.35 && d.distance > l.dpadRadius * 0.18) {
      // 8-way: each direction owns a 135° wedge so diagonals press two.
      final angle = math.atan2(d.dy, d.dx); // 0 = right, +y = down
      const wedge = math.pi * 3 / 8;
      if ((angle).abs() < wedge) mask |= Pad.right.bit;
      if ((angle.abs() - math.pi).abs() < wedge) mask |= Pad.left.bit;
      if ((angle - math.pi / 2).abs() < wedge) mask |= Pad.down.bit;
      if ((angle + math.pi / 2).abs() < wedge) mask |= Pad.up.bit;
      return mask;
    }
    for (final b in l.buttons) {
      if ((pos - b.center).distance < b.radius * 1.3) mask |= b.mask;
    }
    return mask;
  }

  void _update() {
    var mask = 0;
    for (final pos in _pointers.values) {
      mask |= _maskFor(pos);
    }
    if (mask != _mask) {
      if (mask & ~_mask != 0) HapticFeedback.selectionClick();
      setState(() => _mask = mask);
      widget.onChanged(mask);
    }
  }

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(builder: (context, box) {
      _layout = _buildLayout(box.biggest);
      return Listener(
        behavior: HitTestBehavior.translucent,
        onPointerDown: (e) {
          _pointers[e.pointer] = e.localPosition;
          _update();
        },
        onPointerMove: (e) {
          _pointers[e.pointer] = e.localPosition;
          _update();
        },
        onPointerUp: (e) {
          _pointers.remove(e.pointer);
          _update();
        },
        onPointerCancel: (e) {
          _pointers.remove(e.pointer);
          _update();
        },
        child: CustomPaint(
          size: box.biggest,
          painter: _PadPainter(_layout!, _mask, widget.console.color),
        ),
      );
    });
  }
}

class _PadPainter extends CustomPainter {
  _PadPainter(this.layout, this.mask, this.accent);
  final _Layout layout;
  final int mask;
  final Color accent;

  @override
  void paint(Canvas canvas, Size size) {
    final idle = Paint()..color = Colors.white.withValues(alpha: 0.14);
    final edge = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1.5
      ..color = Colors.white.withValues(alpha: 0.35);
    final active = Paint()..color = accent.withValues(alpha: 0.55);

    // D-pad: a plus shape.
    final c = layout.dpad;
    final arm = layout.dpadRadius;
    final w = arm * 0.62;
    final dirs = {
      Pad.up: Rect.fromCenter(center: c + Offset(0, -arm / 2), width: w, height: arm),
      Pad.down: Rect.fromCenter(center: c + Offset(0, arm / 2), width: w, height: arm),
      Pad.left: Rect.fromCenter(center: c + Offset(-arm / 2, 0), width: arm, height: w),
      Pad.right: Rect.fromCenter(center: c + Offset(arm / 2, 0), width: arm, height: w),
    };
    for (final MapEntry(key: pad, value: rect) in dirs.entries) {
      final rr = RRect.fromRectAndRadius(rect, const Radius.circular(8));
      canvas.drawRRect(rr, mask & pad.bit != 0 ? active : idle);
      canvas.drawRRect(rr, edge);
    }

    for (final b in layout.buttons) {
      final on = mask & b.mask != 0;
      canvas.drawCircle(b.center, b.radius, on ? active : idle);
      canvas.drawCircle(b.center, b.radius, edge);
      final small = b.label.length > 2;
      final tp = TextPainter(
        text: TextSpan(
          text: b.label,
          style: TextStyle(
            color: Colors.white.withValues(alpha: 0.85),
            fontSize: small ? b.radius * 0.42 : b.radius * 0.8,
            fontWeight: FontWeight.w700,
            letterSpacing: small ? 0.5 : 0,
          ),
        ),
        textDirection: TextDirection.ltr,
      )..layout();
      tp.paint(canvas, b.center - Offset(tp.width / 2, tp.height / 2));
    }
  }

  @override
  bool shouldRepaint(_PadPainter old) => old.mask != mask || old.layout != layout;
}
