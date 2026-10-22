import 'dart:convert';
import 'dart:ffi' show Abi;
import 'dart:io' as io;

import 'package:flutter/services.dart';
import 'package:http/http.dart' as http;
import 'package:path/path.dart' as p;
import 'package:url_launcher/url_launcher.dart';

import 'paths.dart';
import 'updater.dart';

const _channel = MethodChannel('romrunner/external');

UpdatePlatform platform() {
  if (io.Platform.isAndroid) {
    return switch (Abi.current()) {
      Abi.androidArm64 => UpdatePlatform.androidArm64,
      Abi.androidArm => UpdatePlatform.androidArm,
      _ => UpdatePlatform.androidOther,
    };
  }
  if (io.Platform.isIOS) return UpdatePlatform.ios;
  if (io.Platform.isMacOS) return UpdatePlatform.macos;
  if (io.Platform.isWindows) return UpdatePlatform.windows;
  return UpdatePlatform.linux;
}

// ---- settings: <app support>/updates.json ----

Future<io.File> _file() async => io.File(p.join((await AppPaths.get()).root, 'updates.json'));

Future<Map<String, dynamic>> _load() async {
  try {
    final f = await _file();
    if (f.existsSync()) return jsonDecode(await f.readAsString()) as Map<String, dynamic>;
  } catch (_) {}
  return {};
}

Future<void> _save(Map<String, dynamic> m) async {
  final f = await _file();
  await f.parent.create(recursive: true);
  await f.writeAsString(jsonEncode(m));
}

Future<bool> autoCheck() async => (await _load())['auto'] as bool? ?? true;
Future<void> setAutoCheck(bool on) async => _save({...await _load(), 'auto': on});
Future<DateTime?> lastCheck() async => DateTime.tryParse((await _load())['last'] as String? ?? '');
Future<void> saveLastCheck(DateTime t) async => _save({...await _load(), 'last': t.toIso8601String()});
Future<String?> skipped() async => (await _load())['skip'] as String?;
Future<void> setSkipped(String v) async => _save({...await _load(), 'skip': v});

// ---- install ----

Future<UpdateOutcome> install(ReleaseFile f, {void Function(double?)? onProgress}) async {
  final plat = platform();
  if (!plat.isAndroid) {
    // Desktop and iOS: the browser downloads the new build; the user
    // replaces the app (unzip / sideload). Running apps can't swap their own
    // files safely on every platform.
    if (!await launchUrl(Uri.parse(f.url), mode: LaunchMode.externalApplication)) {
      throw UpdateException('Couldn\'t open the download. Get it from ${Updater.releasesPage}');
    }
    return UpdateOutcome.downloaded;
  }

  final dir = io.Directory(p.join((await AppPaths.get()).root, 'updates'));
  if (dir.existsSync()) dir.deleteSync(recursive: true);   // old downloads
  dir.createSync(recursive: true);
  final file = io.File(p.join(dir.path, f.name));
  final client = http.Client();
  try {
    final res = await client.send(http.Request('GET', Uri.parse(f.url))..headers['User-Agent'] = 'RomRunner');
    if (res.statusCode != 200) throw UpdateException('Download failed (HTTP ${res.statusCode}).');
    final total = res.contentLength ?? f.size;
    final sink = file.openWrite();
    var got = 0;
    try {
      await for (final chunk in res.stream) {
        sink.add(chunk);
        got += chunk.length;
        onProgress?.call(total > 0 ? got / total : null);
      }
    } finally {
      await sink.close();
    }
    if (f.size > 0 && got != f.size) throw UpdateException('The download was incomplete. Try again.');
  } on UpdateException {
    rethrow;
  } catch (e) {
    throw UpdateException('Download failed: ${e.toString().split('\n').first}');
  } finally {
    client.close();
  }

  try {
    await _channel.invokeMethod('installApk', {'path': file.path});
    return UpdateOutcome.installerOpened;
  } on PlatformException catch (e) {
    if (e.code == 'needs_permission') return UpdateOutcome.needsPermission;
    throw UpdateException(e.message ?? 'Couldn\'t open the installer.');
  }
}
