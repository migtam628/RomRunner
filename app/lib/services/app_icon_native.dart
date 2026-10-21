import 'dart:convert';
import 'dart:io';

import 'package:flutter/services.dart';
import 'package:path/path.dart' as p;

import 'app_icon.dart';
import 'paths.dart';

AppIcons createAppIcons() => _NativeAppIcons();

class _NativeAppIcons extends AppIcons {
  static const _channel = MethodChannel('romrunner/icon');
  bool _loaded = false;

  bool get _desktop => Platform.isMacOS || Platform.isWindows || Platform.isLinux;

  Future<File> _file() async => File(p.join((await AppPaths.get()).root, 'app_icon.json'));

  @override
  String get explanation {
    if (Platform.isAndroid) return 'Changes the icon in your app drawer. Your launcher may take a few seconds to update.';
    if (Platform.isIOS) return 'Changes the icon on your Home Screen.';
    return Platform.isMacOS
        ? 'Changes the Dock icon while RomRunner is open.'
        : 'Changes the window and taskbar icon while RomRunner is open.';
  }

  @override
  Future<void> load() async {
    if (_loaded) return;
    _loaded = true;
    try {
      final f = await _file();
      if (f.existsSync()) current = appIconById((jsonDecode(await f.readAsString()) as Map)['icon'] as String?);
    } catch (_) {}
    if (_desktop) await _applyWindowIcon();
  }

  @override
  Future<String?> set(AppIconOption icon) async {
    final previous = current;
    current = icon;
    try {
      if (Platform.isAndroid) {
        await _channel.invokeMethod('setIcon', {
          'alias': icon.androidAlias,
          'all': [for (final i in appIcons) i.androidAlias],
        });
      } else if (Platform.isIOS) {
        await _channel.invokeMethod('setIcon', {'name': icon.iosName});
      } else {
        await _applyWindowIcon();
      }
    } on PlatformException catch (e) {
      current = previous;
      return 'Couldn\'t change the icon: ${e.message ?? e.code}';
    } on MissingPluginException {
      // Older build without the icon channel: remember the choice anyway.
    }
    try {
      await (await _file()).writeAsString(jsonEncode({'icon': icon.id}));
    } catch (_) {}
    return null;
  }

  Future<void> _applyWindowIcon() async {
    try {
      final data = await rootBundle.load(current.asset);
      await _channel.invokeMethod('setWindowIcon', data.buffer.asUint8List(data.offsetInBytes, data.lengthInBytes));
    } catch (_) {
      // No icon channel on this build: keep the built-in icon.
    }
  }
}
