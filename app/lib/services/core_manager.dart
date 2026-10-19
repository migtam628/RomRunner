import 'dart:ffi' show Abi;
import 'dart:io';

import 'package:archive/archive.dart';
import 'package:http/http.dart' as http;
import 'package:path/path.dart' as p;

import '../models/console.dart';
import 'paths.dart';

/// Finds (and on desktop, downloads) the libretro core for a console.
///
/// - Android: cores are packaged in the APK (tool/fetch_cores.sh puts them in
///   android/app/src/main/jniLibs) and opened by library name.
/// - iOS: cores are embedded in Runner.app/Frameworks by an Xcode build phase.
/// - macOS / Windows / Linux: downloaded on first use from the libretro
///   buildbot into the app support folder, or picked up from ROMRUNNER_CORES.
class CoreManager {
  CoreManager._();
  static final instance = CoreManager._();

  static const _buildbot = 'https://buildbot.libretro.com/nightly';

  bool get canDownload => Platform.isMacOS || Platform.isWindows || Platform.isLinux;

  String fileName(Console c) {
    if (Platform.isAndroid) return 'lib${c.core}_libretro_android.so';
    if (Platform.isIOS) return c == Console.gba ? 'mgba_libretro.dylib' : '${c.core}_libretro_ios.dylib';
    if (Platform.isMacOS) return '${c.core}_libretro.dylib';
    if (Platform.isWindows) return '${c.core}_libretro.dll';
    return '${c.core}_libretro.so';
  }

  Uri? downloadUri(Console c) {
    final name = fileName(c);
    final abi = Abi.current();
    String? dir;
    if (Platform.isMacOS) {
      dir = abi == Abi.macosArm64 ? 'apple/osx/arm64' : 'apple/osx/x86_64';
    } else if (Platform.isWindows) {
      dir = 'windows/x86_64';
    } else if (Platform.isLinux) {
      dir = abi == Abi.linuxArm64 ? 'linux/aarch64' : 'linux/x86_64';
    }
    return dir == null ? null : Uri.parse('$_buildbot/$dir/latest/$name.zip');
  }

  /// Path or library name to pass to the bridge, or null if the core is
  /// not available yet (desktop: call [download]).
  Future<String?> locate(Console c) async {
    final name = fileName(c);
    if (Platform.isAndroid) return name; // resolved by the dynamic linker
    if (Platform.isIOS) {
      final fw = p.join(File(Platform.resolvedExecutable).parent.path, 'Frameworks', name);
      return File(fw).existsSync() ? fw : null;
    }
    final candidates = <String>[
      if (Platform.environment['ROMRUNNER_CORES'] case final env?) p.join(env, name),
      p.join(File(Platform.resolvedExecutable).parent.path, 'cores', name),
      p.join((await AppPaths.get()).cores, name),
    ];
    for (final path in candidates) {
      if (File(path).existsSync()) return path;
    }
    return null;
  }

  /// Downloads and unpacks a core. [onProgress] gets 0..1 when size is known.
  Future<String> download(Console c, {void Function(double)? onProgress}) async {
    final uri = downloadUri(c);
    if (uri == null) throw StateError('No download available for this platform');
    final client = http.Client();
    try {
      final res = await client.send(http.Request('GET', uri));
      if (res.statusCode != 200) {
        throw HttpException('Core download failed (${res.statusCode})', uri: uri);
      }
      final total = res.contentLength ?? 0;
      final bytes = <int>[];
      await for (final chunk in res.stream) {
        bytes.addAll(chunk);
        if (total > 0) onProgress?.call(bytes.length / total);
      }
      final archive = ZipDecoder().decodeBytes(bytes);
      final name = fileName(c);
      final entry = archive.files.firstWhere(
        (f) => f.isFile && p.basename(f.name) == name,
        orElse: () => throw StateError('$name not found in download'),
      );
      final dest = File(p.join((await AppPaths.get()).cores, name));
      await dest.writeAsBytes(entry.content as List<int>, flush: true);
      return dest.path;
    } finally {
      client.close();
    }
  }
}
