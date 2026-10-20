import 'dart:convert';

import 'package:http/http.dart' as http;
import 'package:web/web.dart' as web;

import '../models/game.dart';
import 'cover_art.dart';
import 'cover_names.dart';

CoverArt createCoverArt() => _WebCoverArt();

/// Browser: thumbnails.libretro.com images load fine in <img> tags but its
/// listings can't be fetched (no CORS), so listings come from the
/// libretro-thumbnails GitHub repos, which allow it. If GitHub is
/// unavailable, fall back to the exact No-Intro name.
class _WebCoverArt extends CoverArt {
  final _lists = <String, Future<Set<String>?>>{};

  @override
  Future<bool?> readEnabled() async {
    final v = web.window.localStorage.getItem('romrunner.coverArt');
    return v == null ? null : v == '1';
  }

  @override
  Future<void> writeEnabled(bool on) async => web.window.localStorage.setItem('romrunner.coverArt', on ? '1' : '0');

  static String _repo(String system) => system.replaceAll(' ', '_');

  @override
  Future<Cover?> find(Game game) async {
    final systems = thumbnailSystems(game.console, game.fileName);
    if (systems.isEmpty) return null;
    for (final system in systems) {
      final names = await (_lists[system] ??= _listing(system));
      if (names == null) {
        // No listing: try the file name as-is; the image widget hides failures.
        return Cover(url: CoverArt.imageUrl(system, thumbnailFileName(gameBaseName(game.fileName))));
      }
      final name = pickCover(game.fileName, names);
      if (name != null) return Cover(url: CoverArt.imageUrl(system, name));
    }
    return null;
  }

  Future<Set<String>?> _listing(String system) async {
    try {
      final res = await http
          .get(Uri.parse('https://api.github.com/repos/libretro-thumbnails/${_repo(system)}/git/trees/master?recursive=1'))
          .timeout(const Duration(seconds: 30));
      if (res.statusCode != 200) return null;
      final tree = (jsonDecode(res.body) as Map)['tree'] as List;
      return {
        for (final e in tree.cast<Map>())
          if ((e['path'] as String).startsWith('Named_Boxarts/') && (e['path'] as String).endsWith('.png'))
            (e['path'] as String).substring('Named_Boxarts/'.length, (e['path'] as String).length - 4),
      };
    } catch (_) {
      return null;
    }
  }
}
