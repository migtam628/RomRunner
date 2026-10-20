import 'dart:async';

import '../models/game.dart';
import 'cover_art_native.dart' if (dart.library.js_interop) 'cover_art_web.dart' as impl;

/// A game's box art: a cached file on native platforms, a URL on the web.
class Cover {
  const Cover({this.filePath, this.url});
  final String? filePath;
  final String? url;
}

/// Box art from the libretro-thumbnails collection
/// (https://thumbnails.libretro.com), matched by game name.
abstract class CoverArt {
  static final CoverArt instance = impl.createCoverArt();

  static const base = 'https://thumbnails.libretro.com';

  bool _enabled = true;
  bool _loaded = false;

  bool get enabled => _enabled;

  Future<void> load() async {
    if (_loaded) return;
    _loaded = true;
    try {
      _enabled = await readEnabled() ?? true;
    } catch (_) {
      // Setting unreadable: keep the default.
    }
  }

  Future<void> setEnabled(bool on) async {
    _enabled = on;
    try {
      await writeEnabled(on);
    } catch (_) {}
  }

  /// Persisted on/off setting (a file natively, browser storage on web).
  Future<bool?> readEnabled();
  Future<void> writeEnabled(bool on);

  static String imageUrl(String system, String name) =>
      '$base/${Uri.encodeComponent(system)}/Named_Boxarts/${Uri.encodeComponent('$name.png')}';

  /// The cover for [game], or null when there's none (or it's turned off).
  Future<Cover?> coverFor(Game game) async {
    await load();
    if (!_enabled) return null;
    return find(game);
  }

  Future<Cover?> find(Game game);
}

/// Runs at most [max] tasks at once (keeps the network polite).
class Pool {
  Pool(this.max);
  final int max;
  int _running = 0;
  final _waiting = <void Function()>[];

  Future<T> run<T>(Future<T> Function() task) async {
    if (_running >= max) {
      final ready = Completer<void>();
      _waiting.add(ready.complete);
      await ready.future;
    }
    _running++;
    try {
      return await task();
    } finally {
      _running--;
      if (_waiting.isNotEmpty) _waiting.removeAt(0)();
    }
  }
}
