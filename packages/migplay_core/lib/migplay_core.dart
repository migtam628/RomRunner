/// Dart side of the MigPlay libretro bridge (see src/migplay_core.h).
///
/// The emulator runs on a native thread; Dart loads cores/games, sends
/// button state and copies the latest frame once per screen refresh.
library;

import 'dart:ffi';
import 'dart:io';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

/// RetroPad buttons, numbered like RETRO_DEVICE_ID_JOYPAD_*.
enum RetroButton {
  b, y, select, start, up, down, left, right, a, x, l, r, l2, r2, l3, r3;

  int get id => index;
}

class MigplayException implements Exception {
  MigplayException(this.message);
  final String message;
  @override
  String toString() => message;
}

/// A frame copied out of the emulator. [pixels] is RGBA8888 and is only
/// valid until the next [MigplayCore.copyFrame] call.
class RetroFrame {
  RetroFrame(this.counter, this.width, this.height, this.pixels);
  final int counter;
  final int width;
  final int height;
  final Uint8List pixels;
}

const String _libName = 'migplay_core';

final DynamicLibrary _lib = () {
  if (Platform.isMacOS || Platform.isIOS) {
    return DynamicLibrary.open('$_libName.framework/$_libName');
  }
  if (Platform.isAndroid || Platform.isLinux) {
    return DynamicLibrary.open('lib$_libName.so');
  }
  if (Platform.isWindows) {
    return DynamicLibrary.open('$_libName.dll');
  }
  throw UnsupportedError('Unsupported platform: ${Platform.operatingSystem}');
}();

typedef _VoidStrStrC = Void Function(Pointer<Utf8>, Pointer<Utf8>);
typedef _VoidStrStr = void Function(Pointer<Utf8>, Pointer<Utf8>);
typedef _IntStrC = Int32 Function(Pointer<Utf8>);
typedef _IntStr = int Function(Pointer<Utf8>);
typedef _IntVoidC = Int32 Function();
typedef _IntVoid = int Function();
typedef _VoidIntC = Void Function(Int32);
typedef _VoidInt = void Function(int);
typedef _VoidVoidC = Void Function();
typedef _VoidVoid = void Function();
typedef _DoubleVoidC = Double Function();
typedef _DoubleVoid = double Function();
typedef _StrVoidC = Pointer<Utf8> Function();
typedef _StrVoid = Pointer<Utf8> Function();

final _setDirectories = _lib.lookupFunction<_VoidStrStrC, _VoidStrStr>('mp_set_directories');
final _setOption = _lib.lookupFunction<_VoidStrStrC, _VoidStrStr>('mp_set_option');
final _loadCore = _lib.lookupFunction<_IntStrC, _IntStr>('mp_load_core');
final _loadGame = _lib.lookupFunction<_IntStrC, _IntStr>('mp_load_game');
final _start = _lib.lookupFunction<_IntVoidC, _IntVoid>('mp_start');
final _setPaused = _lib.lookupFunction<_VoidIntC, _VoidInt>('mp_set_paused');
final _setFastForward = _lib.lookupFunction<_VoidIntC, _VoidInt>('mp_set_fast_forward');
final _setAudioEnabled = _lib.lookupFunction<_VoidIntC, _VoidInt>('mp_set_audio_enabled');
final _reset = _lib.lookupFunction<_VoidVoidC, _VoidVoid>('mp_reset');
final _setButton = _lib.lookupFunction<Void Function(Int32, Int32, Int32), void Function(int, int, int)>('mp_set_button');
final _setButtons = _lib.lookupFunction<Void Function(Int32, Uint32), void Function(int, int)>('mp_set_buttons');
final _copyFrame = _lib.lookupFunction<
    Int64 Function(Pointer<Uint8>, Int32, Pointer<Int32>, Pointer<Int32>),
    int Function(Pointer<Uint8>, int, Pointer<Int32>, Pointer<Int32>)>('mp_copy_frame');
final _getFps = _lib.lookupFunction<_DoubleVoidC, _DoubleVoid>('mp_get_fps');
final _getAspect = _lib.lookupFunction<_DoubleVoidC, _DoubleVoid>('mp_get_aspect_ratio');
final _saveState = _lib.lookupFunction<_IntStrC, _IntStr>('mp_save_state');
final _loadState = _lib.lookupFunction<_IntStrC, _IntStr>('mp_load_state');
final _flushSram = _lib.lookupFunction<_IntVoidC, _IntVoid>('mp_flush_sram');
final _unload = _lib.lookupFunction<_VoidVoidC, _VoidVoid>('mp_unload');
final _lastError = _lib.lookupFunction<_StrVoidC, _StrVoid>('mp_last_error');
final _coreName = _lib.lookupFunction<_StrVoidC, _StrVoid>('mp_core_name');

/// Thin wrapper over the native bridge. Only one instance is meaningful
/// because the bridge hosts a single core.
class MigplayCore {
  MigplayCore._();
  static final MigplayCore instance = MigplayCore._();

  Pointer<Uint8> _buf = nullptr;
  int _cap = 0;
  final Pointer<Int32> _w = calloc<Int32>();
  final Pointer<Int32> _h = calloc<Int32>();

  T _withStr<T>(String s, T Function(Pointer<Utf8>) f) {
    final p = s.toNativeUtf8();
    try {
      return f(p);
    } finally {
      calloc.free(p);
    }
  }

  void _check(int rc) {
    if (rc != 0) throw MigplayException(lastError);
  }

  String get lastError => _lastError().toDartString();
  String get coreName => _coreName().toDartString();
  double get fps => _getFps();
  double get aspectRatio => _getAspect();

  void setDirectories({required String system, required String saves}) {
    final a = system.toNativeUtf8(), b = saves.toNativeUtf8();
    _setDirectories(a, b);
    calloc.free(a);
    calloc.free(b);
  }

  void setOption(String key, String value) {
    final a = key.toNativeUtf8(), b = value.toNativeUtf8();
    _setOption(a, b);
    calloc.free(a);
    calloc.free(b);
  }

  void loadCore(String path) => _check(_withStr(path, _loadCore));
  void loadGame(String path) => _check(_withStr(path, _loadGame));
  void start() => _check(_start());
  void unload() => _unload();

  set paused(bool v) => _setPaused(v ? 1 : 0);
  set fastForward(bool v) => _setFastForward(v ? 1 : 0);
  set audioEnabled(bool v) => _setAudioEnabled(v ? 1 : 0);
  void reset() => _reset();

  void setButton(RetroButton b, bool pressed, {int port = 0}) =>
      _setButton(port, b.id, pressed ? 1 : 0);
  void setButtons(int mask, {int port = 0}) => _setButtons(port, mask);

  void saveState(String path) => _check(_withStr(path, _saveState));
  void loadState(String path) => _check(_withStr(path, _loadState));
  void flushSram() => _flushSram();

  /// Copies the newest frame if its counter differs from [lastCounter].
  /// Returns null when there is nothing new.
  RetroFrame? copyFrame(int lastCounter) {
    var n = _copyFrame(_buf, _cap, _w, _h);
    if (n == -2) {
      // Grow the buffer to the reported size and try again.
      if (_buf != nullptr) malloc.free(_buf);
      _cap = _w.value * _h.value * 4;
      _buf = malloc<Uint8>(_cap);
      n = _copyFrame(_buf, _cap, _w, _h);
    }
    if (n < 0 || n == lastCounter) return null;
    final w = _w.value, h = _h.value;
    return RetroFrame(n, w, h, _buf.asTypedList(w * h * 4));
  }
}
