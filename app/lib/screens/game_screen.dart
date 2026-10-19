// Native builds drive the libretro bridge; web builds embed EmulatorJS.
export 'game_screen_native.dart' if (dart.library.js_interop) 'game_screen_web.dart';
