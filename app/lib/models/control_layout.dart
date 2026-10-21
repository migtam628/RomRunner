// On-screen controls and game-screen placement, customisable per system and
// orientation. Positions are stored as fractions of the play area so a layout
// survives rotation of the same device and small size changes. Pure Dart
// (dart:ui geometry only) so it can be unit tested.
import 'dart:math' as math;
import 'dart:ui';

import 'console.dart';

enum ControlKind { dpad, face, shoulder, small, icon }

/// What a touch control is: its id in a layout, how it's drawn and which
/// RetroPad buttons it presses (icons trigger actions instead).
class ControlSpec {
  const ControlSpec(this.id, this.kind, this.label, this.mask, this.name);
  final String id;
  final ControlKind kind;
  final String label;
  final int mask;

  /// Shown in the editor ("A button").
  final String name;
}

const menuId = 'menu';
const fastForwardId = 'ff';
const screenId = 'screen';

final _dpadMask = Pad.up.bit | Pad.down.bit | Pad.left.bit | Pad.right.bit;

List<ControlSpec> controlSpecs(Console c) => [
      ControlSpec('dpad', ControlKind.dpad, '', _dpadMask, 'D-pad'),
      for (final b in c.face) ControlSpec('face_${b.pad.name}', ControlKind.face, b.label, b.pad.bit, '${b.label} button'),
      for (final b in c.shoulders)
        ControlSpec('sh_${b.pad.name}', ControlKind.shoulder, b.label, b.pad.bit, '${b.label} button'),
      c == Console.n64
          ? ControlSpec('select', ControlKind.small, 'C▼', Pad.select.bit, 'C-Down') /* the N64 core maps Select to C-Down */
          : ControlSpec('select', ControlKind.small, 'SELECT', Pad.select.bit, 'Select'),
      ControlSpec('start', ControlKind.small, 'START', Pad.start.bit, 'Start'),
      const ControlSpec(menuId, ControlKind.icon, 'menu', 0, 'Menu button'),
      const ControlSpec(fastForwardId, ControlKind.icon, 'ff', 0, 'Fast-forward button'),
    ];

/// The size unit every control is drawn from (≈ one face button's diameter).
double baseUnit(Size area) => math.min(area.shortestSide / 7.5, 72.0);

/// Half-extent of a control, in multiples of the unit.
Size controlExtent(ControlKind k) => switch (k) {
      ControlKind.dpad => const Size(1.45, 1.45),
      ControlKind.face => const Size(0.52, 0.52),
      ControlKind.shoulder => const Size(0.8, 0.31),
      ControlKind.small => const Size(0.56, 0.2),
      ControlKind.icon => const Size(0.34, 0.34),
    };

/// Where a control sits before the user moves it, in pixels.
Offset defaultCenter(ControlSpec s, Console c, Size area, double unit) {
  final w = area.width, h = area.height, u = unit;
  final portrait = h > w;
  final bottom = portrait ? h - u * 2.25 : h - u * 1.95;
  switch (s.kind) {
    case ControlKind.dpad:
      return Offset(u * 1.95, bottom);
    case ControlKind.face:
      final face = c.face;
      final i = face.indexWhere((b) => 'face_${b.pad.name}' == s.id);
      final fc = Offset(w - u * 1.95, bottom);
      final spots = switch (face.length) {
        1 => [fc],
        2 => [fc + Offset(-u * 0.62, u * 0.36), fc + Offset(u * 0.62, -u * 0.36)],
        3 => [fc + Offset(-u * 1.15, u * 0.32), fc, fc + Offset(u * 1.15, -u * 0.32)],
        // Console.face order for 4 buttons: left, top, bottom, right.
        _ => [fc + Offset(-u * 1.02, 0), fc + Offset(0, -u * 1.02), fc + Offset(0, u * 1.02), fc + Offset(u * 1.02, 0)],
      };
      return spots[i.clamp(0, spots.length - 1)];
    case ControlKind.shoulder:
      final i = c.shoulders.indexWhere((b) => 'sh_${b.pad.name}' == s.id);
      final left = i.isEven;
      final row = i ~/ 2;
      final y = bottom - u * (2.45 + row * 0.85);
      return Offset(left ? u * 1.25 : w - u * 1.25, y);
    case ControlKind.small:
      final y = h - u * (portrait ? 0.42 : 0.4);
      return Offset(w / 2 + (s.id == 'start' ? u * 0.72 : -u * 0.72), y);
    case ControlKind.icon:
      final ff = s.id == fastForwardId;
      if (portrait) return Offset(w / 2 + (ff ? u * 0.5 : -u * 0.5), bottom - u * 1.9);
      return Offset(w - u * (ff ? 1.4 : 0.55), u * 0.55);
  }
}

class ControlPos {
  ControlPos(this.x, this.y, {this.scale = 1, this.hidden = false});
  double x, y; // fractions of the play area
  double scale;
  bool hidden;

  Map<String, Object> toJson() => {'x': x, 'y': y, 'scale': scale, if (hidden) 'hidden': true};
  static ControlPos fromJson(Map j) => ControlPos((j['x'] as num).toDouble(), (j['y'] as num).toDouble(),
      scale: (j['scale'] as num?)?.toDouble() ?? 1, hidden: j['hidden'] == true);
}

/// How the game picture fills its rectangle.
enum ScreenFit { fit, integer, stretch }

class ControlLayout {
  ControlLayout();

  /// Controls the user moved, resized or hid. Others use [defaultCenter].
  final Map<String, ControlPos> items = {};
  double opacity = 0.85;
  double size = 1.0;
  bool haptics = true;
  bool colored = true;

  /// Game picture: size relative to the largest that fits, and where it sits
  /// (-1..1 on each axis, like Flutter's Alignment).
  double screenScale = 1.0;
  double? screenX, screenY; // null = default for the orientation
  ScreenFit fit = ScreenFit.fit;
  bool smooth = false;

  static const minScreenScale = 0.35;

  Map<String, Object?> toJson() => {
        'items': {for (final e in items.entries) e.key: e.value.toJson()},
        'opacity': opacity,
        'size': size,
        'haptics': haptics,
        'colored': colored,
        'screenScale': screenScale,
        'screenX': screenX,
        'screenY': screenY,
        'fit': fit.name,
        'smooth': smooth,
      };

  static ControlLayout fromJson(Map j) {
    final l = ControlLayout();
    final items = j['items'];
    if (items is Map) {
      for (final e in items.entries) {
        try {
          l.items[e.key as String] = ControlPos.fromJson(e.value as Map);
        } catch (_) {}
      }
    }
    double? d(String k) => (j[k] as num?)?.toDouble();
    l.opacity = (d('opacity') ?? l.opacity).clamp(0.15, 1.0);
    l.size = (d('size') ?? l.size).clamp(0.6, 1.6);
    l.haptics = j['haptics'] as bool? ?? l.haptics;
    l.colored = j['colored'] as bool? ?? l.colored;
    l.screenScale = (d('screenScale') ?? 1).clamp(minScreenScale, 1.0);
    l.screenX = d('screenX');
    l.screenY = d('screenY');
    l.fit = ScreenFit.values.asNameMap()[j['fit']] ?? ScreenFit.fit;
    l.smooth = j['smooth'] as bool? ?? false;
    return l;
  }

  ControlLayout copy() => fromJson(toJson());

  void resetScreen() {
    screenScale = 1;
    screenX = screenY = null;
    fit = ScreenFit.fit;
  }

  /// Unit for this layout in an area (global size applied).
  double unit(Size area) => baseUnit(area) * size;

  /// Centre of [s] in pixels.
  Offset centerOf(ControlSpec s, Console c, Size area) {
    final p = items[s.id];
    if (p != null) return Offset(p.x * area.width, p.y * area.height);
    return defaultCenter(s, c, area, unit(area));
  }

  double scaleOf(String id) => items[id]?.scale ?? 1;
  bool hiddenOf(String id) => items[id]?.hidden ?? false;

  /// Make [id] customised (so it can be moved/resized), keeping its place.
  ControlPos pin(ControlSpec s, Console c, Size area) => items[s.id] ??= () {
        final o = defaultCenter(s, c, area, unit(area));
        return ControlPos(o.dx / area.width, o.dy / area.height);
      }();

  /// The rectangle a control occupies (for drawing and hit tests).
  Rect boundsOf(ControlSpec s, Console c, Size area) {
    final e = controlExtent(s.kind);
    final u = unit(area) * scaleOf(s.id);
    return Rect.fromCenter(center: centerOf(s, c, area), width: e.width * 2 * u, height: e.height * 2 * u);
  }

  /// Default picture alignment: top in portrait (controls below), centred in landscape.
  /// [centered] ignores the portrait default (no touch controls showing).
  Offset screenAlign(Size area, {bool centered = false}) =>
      Offset(screenX ?? 0, screenY ?? (area.height > area.width && !centered ? -1 : 0));

  /// Where the game picture goes. [frameHeight] is the core's native height
  /// (for integer scaling); [aspect] its display aspect ratio.
  Rect screenRect(Size area, double aspect, int frameHeight, {bool centered = false}) {
    double w, h;
    switch (fit) {
      case ScreenFit.stretch:
        return Offset.zero & area;
      case ScreenFit.integer when frameHeight > 0:
        final k = math.min(area.width / (frameHeight * aspect), area.height / frameHeight).floorToDouble();
        if (k >= 1) {
          h = frameHeight * k;
          w = h * aspect;
          break;
        }
        continue fitted;
      fitted:
      case ScreenFit.fit:
      case ScreenFit.integer:
        w = math.min(area.width, area.height * aspect) * screenScale;
        h = w / aspect;
    }
    final a = screenAlign(area, centered: centered);
    final left = (area.width - w) / 2 * (1 + a.dx);
    final top = (area.height - h) / 2 * (1 + a.dy);
    return Rect.fromLTWH(left, top, w, h);
  }

  /// Move the picture by [delta] pixels.
  void moveScreen(Offset delta, Size area, Rect current) {
    final a = screenAlign(area);
    final freeW = area.width - current.width, freeH = area.height - current.height;
    screenX = freeW > 1 ? (a.dx + 2 * delta.dx / freeW).clamp(-1.0, 1.0) : 0;
    screenY = freeH > 1 ? (a.dy + 2 * delta.dy / freeH).clamp(-1.0, 1.0) : 0;
  }
}
