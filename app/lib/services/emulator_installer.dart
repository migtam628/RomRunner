// Downloads and installs emulator apps from their official releases
// (native only). Desktop: unpacked into <app support>/emulators/<id>/ and
// registered with ExternalSupport. Android: the APK is downloaded and the
// system installer is opened (the user taps Install once).
import 'dart:ffi' show Abi;
import 'dart:io';

import 'package:archive/archive_io.dart';
import 'package:flutter/services.dart';
import 'package:http/http.dart' as http;
import 'package:path/path.dart' as p;

import '../models/console.dart';
import 'emulator_sources.dart';
import 'external_catalog.dart';
import 'external_support.dart';
import 'paths.dart';

class InstallException implements Exception {
  InstallException(this.message);
  final String message;
  @override
  String toString() => message;
}

class EmulatorInstaller {
  EmulatorInstaller._();
  static final instance = EmulatorInstaller._();

  static const _channel = MethodChannel('romrunner/external');

  Os? get _os => Platform.isWindows
      ? Os.windows
      : Platform.isMacOS
          ? Os.macos
          : Platform.isLinux
              ? Os.linux
              : Platform.isAndroid
                  ? Os.android
                  : null;

  Arch get _arch => const {Abi.macosArm64, Abi.linuxArm64, Abi.windowsArm64, Abi.androidArm64}.contains(Abi.current())
      ? Arch.arm64
      : Arch.x64;

  /// Whether RomRunner can download [e] for this platform.
  bool canInstall(ExternalEmulator e) {
    final os = _os;
    return os != null && emulatorSources[e.id]?.sources[os] != null;
  }

  /// The first emulator for [c] that can be installed automatically.
  ExternalEmulator? installableFor(Console c) {
    final host = ExternalSupport.instance.host;
    if (host == null) return null;
    for (final e in emulatorsFor(c, host)) {
      if (canInstall(e)) return e;
    }
    return null;
  }

  String homepage(ExternalEmulator e) => emulatorSources[e.id]?.homepage ?? '';

  /// Downloads and installs [e]. Returns the registered choice on desktop,
  /// or null on Android (the system installer takes over; re-detect after).
  /// [onProgress] gets (stage, fraction or null).
  Future<EmulatorChoice?> install(ExternalEmulator e, Console c, {void Function(String, double?)? onProgress}) async {
    final os = _os;
    final src = emulatorSources[e.id];
    final source = src?.sources[os];
    if (os == null || src == null || source == null) throw InstallException('${e.name} can\'t be installed automatically here.');

    onProgress?.call('Finding the latest ${e.name}…', null);
    final client = http.Client();
    try {
      final assets = await source.assets(client);
      final asset = pickAsset(assets, os, _arch, include: src.include[os], exclude: src.exclude);
      if (asset == null) {
        throw InstallException('No ${e.name} download for this platform right now. Get it from ${src.homepage}.');
      }

      final root = Directory(p.join((await AppPaths.get()).root, 'emulators', e.id));
      final file = File(p.join(root.path, asset.name));
      root.createSync(recursive: true);
      await _download(client, asset, file, (f) => onProgress?.call('Downloading ${e.name} ${asset.version}…', f));

      if (os == Os.android) {
        onProgress?.call('Opening the installer…', null);
        await _channel.invokeMethod('installApk', {'path': file.path});
        return null;
      }

      onProgress?.call('Unpacking…', null);
      final dest = Directory(p.join(root.path, 'app'));
      if (dest.existsSync()) dest.deleteSync(recursive: true);
      dest.createSync(recursive: true);
      await _unpack(file, dest, archiveType(asset.name)!);
      if (archiveType(asset.name) != 'appimage') file.deleteSync();

      final exe = _findExecutable(e, dest, os);
      if (exe == null) throw InstallException('Installed ${e.name}, but couldn\'t find its program file in ${dest.path}.');
      if (os != Os.windows && !exe.endsWith('.app')) await Process.run('chmod', ['+x', exe]);

      final choice = EmulatorChoice(exe, e.name, detail: 'Installed by RomRunner (${asset.version})', emulator: e.id);
      await ExternalSupport.instance.choose(c, choice);
      return choice;
    } on InstallException {
      rethrow;
    } catch (err) {
      throw InstallException('Couldn\'t install ${e.name}: $err');
    } finally {
      client.close();
    }
  }

  Future<void> _download(http.Client client, ReleaseAsset a, File to, void Function(double?) progress) async {
    final res = await client.send(http.Request('GET', Uri.parse(a.url))..headers['User-Agent'] = 'RomRunner');
    if (res.statusCode != 200) throw InstallException('Download failed (HTTP ${res.statusCode}).');
    final total = res.contentLength ?? a.size;
    final sink = to.openWrite();
    var got = 0;
    await for (final chunk in res.stream) {
      sink.add(chunk);
      got += chunk.length;
      progress(total > 0 ? got / total : null);
    }
    await sink.close();
  }

  Future<void> _unpack(File file, Directory dest, String type) async {
    Future<void> run(String exe, List<String> args) async {
      final r = await Process.run(exe, args);
      if (r.exitCode != 0) throw InstallException('$exe failed: ${r.stderr}'.trim());
    }

    switch (type) {
      case 'appimage':
        await file.rename(p.join(dest.path, p.basename(file.path)));
      case 'zip' when Platform.isMacOS:
        await run('ditto', ['-x', '-k', file.path, dest.path]); // keeps .app symlinks and permissions
      case 'zip':
        await extractFileToDisk(file.path, dest.path);
      case 'tar':
        await run('tar', ['-xf', file.path, '-C', dest.path]);
      case '7z':
        // bsdtar (macOS, Windows 10+) reads 7z; else try 7-Zip if installed.
        try {
          await run('tar', ['-xf', file.path, '-C', dest.path]);
        } catch (_) {
          final sevenZip = [
            r'C:\Program Files\7-Zip\7z.exe',
            '7z',
            '7za',
          ].firstWhere((x) => x.contains(Platform.pathSeparator) ? File(x).existsSync() : true);
          await run(sevenZip, ['x', '-y', '-o${dest.path}', file.path]);
        }
      case 'dmg':
        final mount = Directory.systemTemp.createTempSync('romrunner-dmg');
        await run('hdiutil', ['attach', '-nobrowse', '-readonly', '-mountpoint', mount.path, file.path]);
        try {
          for (final app in mount.listSync().where((x) => x.path.endsWith('.app'))) {
            await run('cp', ['-R', app.path, dest.path]);
          }
        } finally {
          await Process.run('hdiutil', ['detach', mount.path, '-force']);
        }
      default:
        throw InstallException('Unsupported download format: $type');
    }
  }

  /// The emulator's program inside [dir] (searched a few levels deep).
  String? _findExecutable(ExternalEmulator e, Directory dir, Os os) {
    final names = switch (os) {
      Os.windows => e.windows.map((n) => n.toLowerCase()).toSet(),
      Os.linux => e.linux.map((n) => n.toLowerCase()).toSet(),
      Os.macos => e.macApps.map((n) => n.toLowerCase()).toSet(),
      Os.android => <String>{},
    };
    final candidates = <String>[];
    void walk(Directory d, int depth) {
      if (depth > 5) return;
      for (final entry in d.listSync(followLinks: false)) {
        final n = p.basename(entry.path).toLowerCase();
        if (os == Os.macos && entry is Directory && n.endsWith('.app')) {
          if (names.contains(n)) {
            candidates.insert(0, entry.path);
          } else {
            candidates.add(entry.path);
          }
          continue; // don't look inside bundles
        }
        if (entry is Directory) {
          walk(entry, depth + 1);
        } else if (entry is File) {
          if (names.contains(n)) candidates.insert(0, entry.path);
          if (os == Os.linux && n.endsWith('.appimage')) candidates.add(entry.path);
        }
      }
    }

    walk(dir, 0);
    return candidates.isEmpty ? null : candidates.first;
  }
}

/// What a system needs from the user's own console besides the emulator.
/// Shown when installing; RomRunner can't (and won't) download these.
String? consoleFilesNote(Console c) => switch (c) {
      Console.ps2 => 'PCSX2 needs a PS2 BIOS dumped from your console; it asks for it on first start.',
      Console.xbox => 'xemu needs the MCPX boot ROM, flash BIOS and hard-disk image from your Xbox.',
      Console.nswitch => 'Switch emulators need prod.keys and firmware dumped from your Switch.',
      Console.ps3 => 'RPCS3 needs the PS3 system software, which Sony offers free on playstation.com; RPCS3 prompts you to install it.',
      Console.wiiu => 'Encrypted Wii U games need keys.txt from your console; decrypted games work without it.',
      Console.n3ds => '3DS games must be decrypted dumps from your own console.',
      Console.vita => 'Vita3K needs the Vita firmware (free from Sony) and games installed into Vita3K.',
      _ => null,
    };
