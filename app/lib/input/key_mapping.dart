import 'package:flutter/services.dart';

import '../models/console.dart';

/// What a key does in-game besides pressing a RetroPad button.
enum KeyAction { openMenu, toggleFastForward }

/// Keyboard, Android/Fire TV gamepad and TV remote bindings.
///
/// Physical layout follows RetroArch: the bottom face button of an
/// Xbox-style pad (Android BUTTON_A) is RetroPad B, the right one is A.
final Map<LogicalKeyboardKey, Pad> padKeys = {
  // D-pad, arrows and TV remote
  LogicalKeyboardKey.arrowUp: Pad.up,
  LogicalKeyboardKey.arrowDown: Pad.down,
  LogicalKeyboardKey.arrowLeft: Pad.left,
  LogicalKeyboardKey.arrowRight: Pad.right,
  // Keyboard
  LogicalKeyboardKey.keyX: Pad.a,
  LogicalKeyboardKey.keyZ: Pad.b,
  LogicalKeyboardKey.keyS: Pad.x,
  LogicalKeyboardKey.keyA: Pad.y,
  LogicalKeyboardKey.keyQ: Pad.l,
  LogicalKeyboardKey.keyW: Pad.r,
  LogicalKeyboardKey.digit1: Pad.l2,
  LogicalKeyboardKey.digit3: Pad.r2,
  LogicalKeyboardKey.enter: Pad.start,
  LogicalKeyboardKey.shiftRight: Pad.select,
  LogicalKeyboardKey.backspace: Pad.select,
  // Gamepads on Android / Fire TV (delivered as key events)
  LogicalKeyboardKey.gameButtonA: Pad.b,
  LogicalKeyboardKey.gameButtonB: Pad.a,
  LogicalKeyboardKey.gameButtonX: Pad.y,
  LogicalKeyboardKey.gameButtonY: Pad.x,
  LogicalKeyboardKey.gameButtonLeft1: Pad.l,
  LogicalKeyboardKey.gameButtonRight1: Pad.r,
  LogicalKeyboardKey.gameButtonLeft2: Pad.l2,
  LogicalKeyboardKey.gameButtonRight2: Pad.r2,
  LogicalKeyboardKey.gameButtonThumbLeft: Pad.l3,
  LogicalKeyboardKey.gameButtonThumbRight: Pad.r3,
  LogicalKeyboardKey.gameButtonStart: Pad.start,
  LogicalKeyboardKey.gameButtonSelect: Pad.select,
  // Fire TV remote: center = A, play/pause = Start, menu = Select
  LogicalKeyboardKey.select: Pad.a,
  LogicalKeyboardKey.mediaPlayPause: Pad.start,
  LogicalKeyboardKey.contextMenu: Pad.select,
};

final Map<LogicalKeyboardKey, KeyAction> actionKeys = {
  LogicalKeyboardKey.escape: KeyAction.openMenu,
  LogicalKeyboardKey.goBack: KeyAction.openMenu,
  LogicalKeyboardKey.gameButtonMode: KeyAction.openMenu,
  LogicalKeyboardKey.f1: KeyAction.openMenu,
  LogicalKeyboardKey.tab: KeyAction.toggleFastForward,
  LogicalKeyboardKey.mediaFastForward: KeyAction.toggleFastForward,
};

const keyboardHelp = 'Arrows move · X = A · Z = B · S = X · A = Y · Q/W = L/R · '
    'Enter = Start · Backspace = Select · Tab = fast-forward · Esc = menu';
