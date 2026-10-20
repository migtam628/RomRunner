import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../models/console.dart';
import '../models/control_layout.dart';

/// Multi-touch on-screen gamepad drawn from a [ControlLayout]. In play mode
/// it reports pressed RetroPad buttons as a bitmask ([Pad.bit]); in edit
/// mode, dragging moves the selected control (or the game screen) and
/// pinching resizes it.
class TouchController extends StatefulWidget {
  const TouchController({
    super.key,
    required this.console,
    required this.layout,
    required this.screenRect,
    required this.onChanged,
    this.onMenu,
    this.onFastForward,
    this.fastForward = false,
    this.editing = false,
    this.selected,
    this.onSelect,
    this.onEdited,
  });

  final Console console;
  final ControlLayout layout;
  final Rect screenRect;
  final ValueChanged<int> onChanged;
  final VoidCallback? onMenu;
  final VoidCallback? onFastForward;
  final bool fastForward;

  final bool editing;
  final String? selected;
  final ValueChanged<String?>? onSelect;
  final VoidCallback? onEdited;

  @override
  State<TouchController> createState() => _TouchControllerState();
}

class _TouchControllerState extends State<TouchController> {
  final Map<int, Offset> _pointers = {};
  final Set<int> _consumed = {}; // pointers that hit an icon button
  int _mask = 0;
  Size _area = Size.zero;

  // Edit-mode pinch.
  double? _pinchStart;
  double _scaleStart = 1;

  late List<ControlSpec> _specs = controlSpecs(widget.console);

  @override
  void didUpdateWidget(TouchController old) {
    super.didUpdateWidget(old);
    if (old.console != widget.console) _specs = controlSpecs(widget.console);
    if (widget.editing && !old.editing) _release();
  }

  ControlLayout get _l => widget.layout;

  ControlSpec? _spec(String? id) => _specs.where((s) => s.id == id).firstOrNull;

  // ---- play mode ----

  int _maskFor(Offset pos) {
    var mask = 0;
    final u = _l.unit(_area);
    for (final s in _specs) {
      if (s.kind == ControlKind.icon || _l.hiddenOf(s.id)) continue;
      final c = _l.centerOf(s, widget.console, _area);
      final k = u * _l.scaleOf(s.id);
      if (s.kind == ControlKind.dpad) {
        final d = pos - c;
        final arm = k * 1.35;
        if (d.distance < arm * 1.4 && d.distance > arm * 0.16) {
          // 8-way: each direction owns a 135° wedge so diagonals press two.
          final angle = math.atan2(d.dy, d.dx);
          const wedge = math.pi * 3 / 8;
          if (angle.abs() < wedge) mask |= Pad.right.bit;
          if ((angle.abs() - math.pi).abs() < wedge) mask |= Pad.left.bit;
          if ((angle - math.pi / 2).abs() < wedge) mask |= Pad.down.bit;
          if ((angle + math.pi / 2).abs() < wedge) mask |= Pad.up.bit;
          return mask;
        }
      } else if (s.kind == ControlKind.face) {
        if ((pos - c).distance < k * 0.52 * 1.3) mask |= s.mask;
      } else {
        if (_l.boundsOf(s, widget.console, _area).inflate(k * 0.22).contains(pos)) mask |= s.mask;
      }
    }
    return mask;
  }

  ControlSpec? _iconAt(Offset pos) {
    for (final s in _specs) {
      if (s.kind != ControlKind.icon || _l.hiddenOf(s.id)) continue;
      if (_l.boundsOf(s, widget.console, _area).inflate(10).contains(pos)) return s;
    }
    return null;
  }

  void _update() {
    var mask = 0;
    for (final e in _pointers.entries) {
      if (!_consumed.contains(e.key)) mask |= _maskFor(e.value);
    }
    if (mask != _mask) {
      if (mask & ~_mask != 0 && _l.haptics) HapticFeedback.selectionClick();
      setState(() => _mask = mask);
      widget.onChanged(mask);
    }
  }

  void _release() {
    _pointers.clear();
    _consumed.clear();
    if (_mask != 0) {
      _mask = 0;
      widget.onChanged(0);
    }
  }

  void _down(PointerDownEvent e) {
    if (widget.editing) return _editDown(e);
    _pointers[e.pointer] = e.localPosition;
    final icon = _iconAt(e.localPosition);
    if (icon != null) {
      _consumed.add(e.pointer);
      if (_l.haptics) HapticFeedback.lightImpact();
      (icon.id == menuId ? widget.onMenu : widget.onFastForward)?.call();
    }
    _update();
  }

  void _move(PointerMoveEvent e) {
    if (widget.editing) return _editMove(e);
    _pointers[e.pointer] = e.localPosition;
    _update();
  }

  void _up(PointerEvent e) {
    _pointers.remove(e.pointer);
    _consumed.remove(e.pointer);
    if (widget.editing) {
      _pinchStart = null;
      return;
    }
    _update();
  }

  // ---- edit mode ----

  String? _hitEdit(Offset pos) {
    ControlSpec? best;
    var bestArea = double.infinity;
    for (final s in _specs) {
      final r = _l.boundsOf(s, widget.console, _area).inflate(12);
      if (r.contains(pos) && r.width * r.height < bestArea) {
        best = s;
        bestArea = r.width * r.height;
      }
    }
    if (best != null) return best.id;
    return widget.screenRect.contains(pos) ? screenId : null;
  }

  double _scaleOfSelected() => widget.selected == screenId ? _l.screenScale : _l.scaleOf(widget.selected!);

  void _editDown(PointerDownEvent e) {
    _pointers[e.pointer] = e.localPosition;
    if (_pointers.length == 1) {
      final id = _hitEdit(e.localPosition);
      final s = _spec(id);
      if (s != null) _l.pin(s, widget.console, _area);
      widget.onSelect?.call(id);
    } else if (_pointers.length == 2 && widget.selected != null) {
      final pts = _pointers.values.toList();
      _pinchStart = (pts[0] - pts[1]).distance;
      _scaleStart = _scaleOfSelected();
    }
  }

  void _editMove(PointerMoveEvent e) {
    final prev = _pointers[e.pointer];
    _pointers[e.pointer] = e.localPosition;
    final id = widget.selected;
    if (id == null || prev == null) return;
    if (_pointers.length == 1) {
      final d = e.localPosition - prev;
      if (id == screenId) {
        _l.moveScreen(d, _area, widget.screenRect);
      } else {
        final p = _l.items[id];
        if (p == null) return;
        p.x = (p.x + d.dx / _area.width).clamp(0.0, 1.0);
        p.y = (p.y + d.dy / _area.height).clamp(0.0, 1.0);
      }
      widget.onEdited?.call();
    } else if (_pointers.length == 2 && _pinchStart != null && _pinchStart! > 0) {
      final pts = _pointers.values.toList();
      final ratio = (pts[0] - pts[1]).distance / _pinchStart!;
      if (id == screenId) {
        _l.fit = ScreenFit.fit;
        _l.screenScale = (_scaleStart * ratio).clamp(ControlLayout.minScreenScale, 1.0);
      } else {
        _l.items[id]?.scale = (_scaleStart * ratio).clamp(0.5, 2.2);
      }
      widget.onEdited?.call();
    }
  }

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(builder: (context, box) {
      _area = box.biggest;
      return Listener(
        behavior: widget.editing ? HitTestBehavior.opaque : HitTestBehavior.translucent,
        onPointerDown: _down,
        onPointerMove: _move,
        onPointerUp: _up,
        onPointerCancel: _up,
        child: CustomPaint(
          size: _area,
          painter: PadPainter(
            console: widget.console,
            layout: _l,
            specs: _specs,
            mask: _mask,
            fastForward: widget.fastForward,
            editing: widget.editing,
            selected: widget.selected,
            screenRect: widget.screenRect,
            // Edits mutate the layout in place; repaint on every build then.
            revision: widget.editing ? Object() : null,
          ),
        ),
      );
    });
  }
}

/// Button colours of the original pads (Super Famicom / PlayStation).
Color? _classicColor(String label) => switch (label) {
      'A' => const Color(0xFFE5484D),
      'B' => const Color(0xFFF5C518),
      'X' => const Color(0xFF3E8BFF),
      'Y' => const Color(0xFF3DBE6B),
      '△' => const Color(0xFF3CC9A6),
      '○' => const Color(0xFFFF5F67),
      '✕' => const Color(0xFF7AA7FF),
      '□' => const Color(0xFFF27CC0),
      _ => null,
    };

class PadPainter extends CustomPainter {
  PadPainter({
    required this.console,
    required this.layout,
    required this.specs,
    required this.mask,
    required this.fastForward,
    required this.editing,
    required this.selected,
    required this.screenRect,
    this.revision,
  });

  final Console console;
  final ControlLayout layout;
  final List<ControlSpec> specs;
  final int mask;
  final bool fastForward;
  final bool editing;
  final String? selected;
  final Rect screenRect;
  final Object? revision;

  double get _op => layout.opacity;

  Color _tint(ControlSpec s) {
    if (layout.colored && console.face.length == 4) return _classicColor(s.label) ?? console.color;
    return console.color;
  }

  @override
  void paint(Canvas canvas, Size size) {
    if (editing) _paintGuides(canvas, size);
    final u = layout.unit(size);
    for (final s in specs) {
      final hidden = layout.hiddenOf(s.id);
      if (hidden && !editing) continue;
      final c = layout.centerOf(s, console, size);
      final k = u * layout.scaleOf(s.id);
      canvas.saveLayer(null, Paint()..color = Color.fromRGBO(0, 0, 0, hidden ? 0.25 : 1));
      switch (s.kind) {
        case ControlKind.dpad:
          _paintDpad(canvas, c, k);
        case ControlKind.face:
          _paintRound(canvas, c, k * 0.52, s, mask & s.mask != 0);
        case ControlKind.shoulder:
        case ControlKind.small:
          _paintPill(canvas, layout.boundsOf(s, console, size), s, mask & s.mask != 0);
        case ControlKind.icon:
          _paintIcon(canvas, c, k * 0.34, s);
      }
      canvas.restore();
      if (editing) _paintOutline(canvas, layout.boundsOf(s, console, size).inflate(6), s.id == selected);
    }
    if (editing && selected == screenId) _paintOutline(canvas, screenRect.deflate(2), true);
  }

  void _paintGuides(Canvas canvas, Size size) {
    final g = Paint()
      ..color = Colors.white.withValues(alpha: 0.08)
      ..strokeWidth = 1;
    canvas.drawLine(Offset(size.width / 2, 0), Offset(size.width / 2, size.height), g);
    canvas.drawLine(Offset(0, size.height / 2), Offset(size.width, size.height / 2), g);
  }

  void _paintOutline(Canvas canvas, Rect r, bool on) {
    final rr = RRect.fromRectAndRadius(r, const Radius.circular(10));
    if (on) {
      canvas.drawRRect(rr, Paint()..color = console.color.withValues(alpha: 0.12));
      canvas.drawRRect(
          rr,
          Paint()
            ..style = PaintingStyle.stroke
            ..strokeWidth = 2
            ..color = console.color);
      final h = Paint()..color = Colors.white;
      for (final p in [r.topLeft, r.topRight, r.bottomLeft, r.bottomRight]) {
        canvas.drawCircle(p, 4, h);
      }
    } else {
      // Dashed outline so every control is visibly movable.
      final paint = Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1
        ..color = Colors.white.withValues(alpha: 0.3);
      final path = Path()..addRRect(rr);
      for (final m in path.computeMetrics()) {
        for (var d = 0.0; d < m.length; d += 9) {
          canvas.drawPath(m.extractPath(d, math.min(d + 5, m.length)), paint);
        }
      }
    }
  }

  Paint _body(Rect r, {required bool pressed, Color? tint}) {
    if (pressed) {
      final t = tint ?? console.color;
      return Paint()
        ..shader = RadialGradient(
          center: const Alignment(-0.3, -0.4),
          colors: [Color.lerp(t, Colors.white, 0.25)!.withValues(alpha: 0.95 * _op), t.withValues(alpha: 0.85 * _op)],
        ).createShader(r);
    }
    return Paint()
      ..shader = RadialGradient(
        center: const Alignment(-0.35, -0.45),
        radius: 1.1,
        colors: [
          const Color(0xFF4A4E5A).withValues(alpha: 0.72 * _op),
          const Color(0xFF1A1C22).withValues(alpha: 0.72 * _op),
        ],
      ).createShader(r);
  }

  Paint get _rim => Paint()
    ..style = PaintingStyle.stroke
    ..strokeWidth = 1.2
    ..color = Colors.white.withValues(alpha: 0.22 * _op);

  void _shadow(Canvas canvas, Path path, double blur) {
    canvas.drawPath(
        path.shift(Offset(0, blur * 0.5)),
        Paint()
          ..color = Colors.black.withValues(alpha: 0.4 * _op)
          ..maskFilter = MaskFilter.blur(BlurStyle.normal, blur));
  }

  void _glow(Canvas canvas, Path path, Color c) {
    canvas.drawPath(
        path,
        Paint()
          ..color = c.withValues(alpha: 0.45 * _op)
          ..maskFilter = const MaskFilter.blur(BlurStyle.normal, 12));
  }

  void _label(Canvas canvas, Offset c, String text, double fontSize, Color color, {double spacing = 0}) {
    final tp = TextPainter(
      text: TextSpan(
        text: text,
        style: TextStyle(color: color, fontSize: fontSize, fontWeight: FontWeight.w800, letterSpacing: spacing),
      ),
      textDirection: TextDirection.ltr,
    )..layout();
    tp.paint(canvas, c - Offset(tp.width / 2, tp.height / 2));
  }

  void _paintRound(Canvas canvas, Offset c, double r, ControlSpec s, bool on) {
    final tint = _tint(s);
    final rr = on ? r * 0.95 : r;
    final rect = Rect.fromCircle(center: c, radius: rr);
    final path = Path()..addOval(rect);
    if (on) {
      _glow(canvas, path, tint);
    } else {
      _shadow(canvas, path, 5);
    }
    canvas.drawPath(path, _body(rect, pressed: on, tint: tint));
    canvas.drawPath(path, _rim);
    if (layout.colored && !on) {
      canvas.drawCircle(
          c,
          rr - 2.5,
          Paint()
            ..style = PaintingStyle.stroke
            ..strokeWidth = 1.6
            ..color = tint.withValues(alpha: 0.55 * _op));
    }
    final small = s.label.length > 2;
    final color = on ? Colors.white : (layout.colored ? Color.lerp(tint, Colors.white, 0.35)! : Colors.white);
    _label(canvas, c, s.label, small ? r * 0.42 : r * 0.78, color.withValues(alpha: 0.92 * _op));
  }

  void _paintPill(Canvas canvas, Rect r, ControlSpec s, bool on) {
    final rr = RRect.fromRectAndRadius(on ? r.deflate(1) : r, Radius.circular(r.height / 2));
    final path = Path()..addRRect(rr);
    if (on) {
      _glow(canvas, path, console.color);
    } else {
      _shadow(canvas, path, 4);
    }
    canvas.drawPath(path, _body(r, pressed: on));
    canvas.drawPath(path, _rim);
    final small = s.kind == ControlKind.small;
    _label(canvas, r.center, s.label, r.height * (small ? 0.42 : 0.5), Colors.white.withValues(alpha: 0.88 * _op),
        spacing: small ? 1 : 0.5);
  }

  void _paintIcon(Canvas canvas, Offset c, double r, ControlSpec s) {
    final ff = s.id == fastForwardId;
    final on = ff && fastForward;
    final rect = Rect.fromCircle(center: c, radius: r);
    final path = Path()..addOval(rect);
    if (on) _glow(canvas, path, console.color);
    canvas.drawPath(path, _body(rect, pressed: on));
    canvas.drawPath(path, _rim);
    final icon = ff ? Icons.fast_forward_rounded : Icons.pause_rounded;
    final tp = TextPainter(
      text: TextSpan(
        text: String.fromCharCode(icon.codePoint),
        style: TextStyle(
          fontFamily: icon.fontFamily,
          package: icon.fontPackage,
          fontSize: r * 1.15,
          color: Colors.white.withValues(alpha: 0.9 * _op),
        ),
      ),
      textDirection: TextDirection.ltr,
    )..layout();
    tp.paint(canvas, c - Offset(tp.width / 2, tp.height / 2));
  }

  void _paintDpad(Canvas canvas, Offset c, double k) {
    final arm = k * 1.35;
    final w = arm * 0.66;
    final radius = Radius.circular(w * 0.22);
    final vertical = RRect.fromRectAndRadius(Rect.fromCenter(center: c, width: w, height: arm * 2), radius);
    final horizontal = RRect.fromRectAndRadius(Rect.fromCenter(center: c, width: arm * 2, height: w), radius);
    final cross = Path.combine(PathOperation.union, Path()..addRRect(vertical), Path()..addRRect(horizontal));
    final bounds = Rect.fromCircle(center: c, radius: arm);

    // Soft base disc so the d-pad reads as one control.
    canvas.drawCircle(c, arm * 1.12, Paint()..color = Colors.black.withValues(alpha: 0.18 * _op));
    _shadow(canvas, cross, 6);
    canvas.drawPath(cross, _body(bounds, pressed: false));

    final arms = {
      Pad.up: Rect.fromLTRB(c.dx - w / 2, c.dy - arm, c.dx + w / 2, c.dy - w / 2),
      Pad.down: Rect.fromLTRB(c.dx - w / 2, c.dy + w / 2, c.dx + w / 2, c.dy + arm),
      Pad.left: Rect.fromLTRB(c.dx - arm, c.dy - w / 2, c.dx - w / 2, c.dy + w / 2),
      Pad.right: Rect.fromLTRB(c.dx + w / 2, c.dy - w / 2, c.dx + arm, c.dy + w / 2),
    };
    canvas.save();
    canvas.clipPath(cross);
    for (final MapEntry(key: pad, value: r) in arms.entries) {
      if (mask & pad.bit != 0) canvas.drawRect(r.inflate(1), _body(r, pressed: true));
    }
    canvas.restore();
    canvas.drawPath(cross, _rim);

    // Centre dimple and arrows.
    canvas.drawCircle(c, w * 0.22, Paint()..color = Colors.black.withValues(alpha: 0.25 * _op));
    final arrow = Paint()..color = Colors.white.withValues(alpha: 0.55 * _op);
    final a = w * 0.16;
    for (final (dir, rot) in [(Offset(0, -1), 0.0), (Offset(1, 0), math.pi / 2), (Offset(0, 1), math.pi), (Offset(-1, 0), -math.pi / 2)]) {
      final tip = c + dir * (arm * 0.72);
      canvas.save();
      canvas.translate(tip.dx, tip.dy);
      canvas.rotate(rot);
      canvas.drawPath(
          Path()
            ..moveTo(0, -a)
            ..lineTo(a * 1.1, a * 0.6)
            ..lineTo(-a * 1.1, a * 0.6)
            ..close(),
          arrow);
      canvas.restore();
    }
  }

  @override
  bool shouldRepaint(PadPainter old) =>
      revision != null ||
      old.revision != null ||
      old.mask != mask ||
      old.fastForward != fastForward ||
      old.layout != layout ||
      old.selected != selected ||
      old.editing != editing ||
      old.screenRect != screenRect;
}
