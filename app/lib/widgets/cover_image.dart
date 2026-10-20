// File images exist only off the web; this indirection keeps library_screen
// free of dart:io imports.
export 'cover_image_native.dart' if (dart.library.js_interop) 'cover_image_web.dart';
