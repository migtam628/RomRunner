import 'dart:convert';
import 'dart:io';

import 'package:http/http.dart' as http;
import 'package:path/path.dart' as p;

import '../models/game.dart';
import 'cover_art.dart';
import 'cover_names.dart';
import 'paths.dart';

CoverArt createCoverArt() => _NativeCoverArt();

/// Desktop / mobile: each system's box-art listing is fetched from
/// thumbnails.libretro.com once a month, matched images are downloaded
/// once into `<app support>/covers/`, and misses are remembered for a week.
class _NativeCoverArt extends CoverArt {
  final _client = http.Client();
  final _pool = Pool(3);
  final _lists = <String, Future<Set<String>?>>{};
  final _inFlight = <String, Future<Cover?>>{};
  Map<String, int>? _misses;

  static const _listMaxAge = Duration(days: 30);
  static const _missMaxAge = Duration(days: 7);

  Future<Directory> _root() async {
    final d = Directory(p.join((await AppPaths.get()).root, 'covers'));
    if (!d.existsSync()) d.createSync(recursive: true);
    return d;
  }

  Future<File> _settingFile() async => File(p.join((await AppPaths.get()).root, 'cover_art.json'));

  @override
  Future<bool?> readEnabled() async {
    final f = await _settingFile();
    if (!f.existsSync()) return null;
    return (jsonDecode(await f.readAsString()) as Map)['enabled'] as bool?;
  }

  @override
  Future<void> writeEnabled(bool on) async => (await _settingFile()).writeAsString(jsonEncode({'enabled': on}));

  static String _safe(String s) => s.replaceAll(RegExp(r'[^\w\-. ]'), '_');

  @override
  Future<Cover?> find(Game game) => _inFlight[game.id] ??= _find(game).whenComplete(() {
        // A block, not `=> remove(...)`: returning the removed future here
        // would make it wait on itself forever.
        _inFlight.remove(game.id);
      });

  Future<Cover?> _find(Game game) async {
    final root = await _root();
    final systems = thumbnailSystems(game.console, game.fileName);
    if (systems.isEmpty) return null;

    // Already downloaded?
    final key = _safe('${game.console.slug}-${gameBaseName(game.fileName)}');
    final cached = File(p.join(root.path, '$key.png'));
    if (cached.existsSync() && cached.lengthSync() > 0) return Cover(filePath: cached.path);

    final misses = await _loadMisses(root);
    final missedAt = misses[game.id];
    if (missedAt != null && DateTime.now().millisecondsSinceEpoch - missedAt < _missMaxAge.inMilliseconds) return null;

    for (final system in systems) {
      final names = await (_lists[system] ??= _listing(root, system));
      if (names == null) return null; // offline: try again next time
      final name = pickCover(game.fileName, names);
      if (name == null) continue;
      final ok = await _pool.run(() => _download(CoverArt.imageUrl(system, name), cached));
      if (ok) return Cover(filePath: cached.path);
    }
    misses[game.id] = DateTime.now().millisecondsSinceEpoch;
    await File(p.join(root.path, '_misses.json')).writeAsString(jsonEncode(misses));
    return null;
  }

  Future<Map<String, int>> _loadMisses(Directory root) async {
    if (_misses != null) return _misses!;
    try {
      final f = File(p.join(root.path, '_misses.json'));
      if (f.existsSync()) _misses = (jsonDecode(await f.readAsString()) as Map).cast<String, int>();
    } catch (_) {}
    return _misses ??= {};
  }

  /// Box-art names (without ".png") for [system], cached on disk.
  Future<Set<String>?> _listing(Directory root, String system) async {
    final file = File(p.join(root.path, '_lists', '${_safe(system)}.txt'));
    if (file.existsSync() && DateTime.now().difference(file.lastModifiedSync()) < _listMaxAge) {
      return (await file.readAsLines()).toSet();
    }
    try {
      final res = await _pool.run(() => _client
          .get(Uri.parse('${CoverArt.base}/${Uri.encodeComponent(system)}/Named_Boxarts/'))
          .timeout(const Duration(seconds: 30)));
      if (res.statusCode != 200) throw HttpException('HTTP ${res.statusCode}');
      final names = {
        for (final m in RegExp(r'href="([^"/?]+)\.png"').allMatches(res.body)) Uri.decodeComponent(m.group(1)!),
      };
      file.parent.createSync(recursive: true);
      await file.writeAsString(names.join('\n'));
      return names;
    } catch (_) {
      // Offline or the site is down: use a stale list if there is one.
      return file.existsSync() ? (await file.readAsLines()).toSet() : null;
    }
  }

  Future<bool> _download(String url, File to) async {
    try {
      final res = await _client.get(Uri.parse(url)).timeout(const Duration(seconds: 30));
      if (res.statusCode != 200 || res.bodyBytes.isEmpty) return false;
      await to.writeAsBytes(res.bodyBytes, flush: true);
      return true;
    } catch (_) {
      return false;
    }
  }
}
