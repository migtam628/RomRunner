// Where to download each emulator from, and how to pick the right file.
//
// Pure Dart (no Flutter): used by the app's installer and by
// tool/check_downloads.dart, which CI runs to confirm every source still
// resolves to a real download.
//
// Downloads come only from each project's own release channel (its GitHub
// releases or official site). Nothing is mirrored by RomRunner.
import 'dart:convert';

import 'package:http/http.dart' as http;

enum Os { windows, macos, linux, android }

enum Arch { x64, arm64 }

class ReleaseAsset {
  const ReleaseAsset(this.name, this.url, {this.size = 0, this.version = ''});
  final String name;
  final String url;
  final int size;
  final String version;
  @override
  String toString() => '$name ($version) $url';
}

/// One place an emulator publishes builds.
sealed class Source {
  const Source();
  Future<List<ReleaseAsset>> assets(http.Client client);
}

/// GitHub releases (newest first). [prerelease]: accept pre-releases,
/// which some projects (PCSX2 nightly, Vita3K "continuous") use for all builds.
class GitHubReleases extends Source {
  const GitHubReleases(this.repo, {this.prerelease = true, this.latestOnly = false});
  final String repo;
  final bool prerelease;

  /// Use GitHub's "latest release" (for repos whose release list isn't
  /// newest-first, e.g. RPCS3's binary repos).
  final bool latestOnly;

  static String? token; // optional, for CI rate limits

  @override
  Future<List<ReleaseAsset>> assets(http.Client client) async {
    final res = await client.get(
      Uri.parse('https://api.github.com/repos/$repo/releases${latestOnly ? '/latest' : '?per_page=8'}'),
      headers: {
        'Accept': 'application/vnd.github+json',
        'User-Agent': 'RomRunner',
        if (token != null) 'Authorization': 'Bearer $token',
      },
    );
    if (res.statusCode != 200) throw StateError('GitHub $repo: HTTP ${res.statusCode}');
    final body = jsonDecode(res.body);
    final releases = latestOnly ? [body as Map<String, dynamic>] : (body as List).cast<Map<String, dynamic>>();
    final out = <ReleaseAsset>[];
    for (final r in releases) {
      if (r['draft'] == true || (r['prerelease'] == true && !prerelease)) continue;
      for (final a in (r['assets'] as List).cast<Map<String, dynamic>>()) {
        out.add(ReleaseAsset(a['name'] as String, a['browser_download_url'] as String,
            size: (a['size'] as num?)?.toInt() ?? 0, version: r['tag_name'] as String? ?? ''));
      }
    }
    return out;
  }
}

/// Forgejo / Gitea releases API (e.g. git.eden-emu.dev).
class ForgejoReleases extends Source {
  const ForgejoReleases(this.host, this.repo);
  final String host;
  final String repo;

  @override
  Future<List<ReleaseAsset>> assets(http.Client client) async {
    final res = await client.get(Uri.parse('https://$host/api/v1/repos/$repo/releases?limit=8'),
        headers: {'User-Agent': 'RomRunner'});
    if (res.statusCode != 200) throw StateError('$host $repo: HTTP ${res.statusCode}');
    return [
      for (final r in (jsonDecode(res.body) as List).cast<Map<String, dynamic>>())
        if (r['draft'] != true)
          for (final a in ((r['assets'] as List?) ?? const []).cast<Map<String, dynamic>>())
            ReleaseAsset(a['name'] as String, a['browser_download_url'] as String,
                size: (a['size'] as num?)?.toInt() ?? 0, version: r['tag_name'] as String? ?? ''),
    ];
  }
}

/// GitLab releases API (e.g. git.ryujinx.app).
class GitLabReleases extends Source {
  const GitLabReleases(this.host, this.project);
  final String host;
  final String project;

  @override
  Future<List<ReleaseAsset>> assets(http.Client client) async {
    final id = Uri.encodeComponent(project);
    final res = await client.get(Uri.parse('https://$host/api/v4/projects/$id/releases?per_page=8'),
        headers: {'User-Agent': 'RomRunner'});
    if (res.statusCode != 200) throw StateError('$host $project: HTTP ${res.statusCode}');
    return [
      for (final r in (jsonDecode(res.body) as List).cast<Map<String, dynamic>>())
        for (final l in (((r['assets'] as Map?)?['links'] as List?) ?? const []).cast<Map<String, dynamic>>())
          ReleaseAsset(l['name'] as String, (l['direct_asset_url'] ?? l['url']) as String,
              version: r['tag_name'] as String? ?? ''),
    ];
  }
}

/// Links scraped from an official download page (projects without a
/// release API, e.g. Dolphin, PPSSPP). [pattern] must match full URLs.
class PageLinks extends Source {
  const PageLinks(this.page, this.pattern);
  final String page;
  final String pattern;

  @override
  Future<List<ReleaseAsset>> assets(http.Client client) async {
    final res = await client.get(Uri.parse(page), headers: {'User-Agent': 'Mozilla/5.0 RomRunner'});
    if (res.statusCode != 200) throw StateError('$page: HTTP ${res.statusCode}');
    final base = Uri.parse(page);
    final seen = <String>{};
    final out = <ReleaseAsset>[];
    for (final m in RegExp(pattern, caseSensitive: false).allMatches(res.body)) {
      final url = base.resolve(m.group(0)!.replaceAll('&amp;', '&')).toString();
      if (seen.add(url)) out.add(ReleaseAsset(Uri.parse(url).pathSegments.last, url));
    }
    return out;
  }
}

/// How to find an emulator's build for each platform.
class EmulatorSource {
  const EmulatorSource(this.sources, {this.include = const {}, this.exclude, this.homepage = ''});

  /// Per platform; [Os] missing means "no automatic install there".
  final Map<Os, Source> sources;

  /// Optional per-platform regex an asset name must match.
  final Map<Os, String> include;

  /// Regex of asset names to never pick (debug symbols etc.).
  final String? exclude;
  final String homepage;
}

const _noise = r'(debug|symbols|\bpdb\b|\bdbg\b|source|\bsrc\b|\.sha\d*|\.sig$|\.asc$|\.md5$|\.txt$|\.json$|\.zsync$|-dev\b|\btests?\b|\.aab$|libretro)';

/// Picks the best asset for [os]/[arch] from [assets] (newest releases
/// first), or null.
ReleaseAsset? pickAsset(List<ReleaseAsset> assets, Os os, Arch arch, {String? include, String? exclude}) {
  int score(ReleaseAsset a) {
    final n = a.name.toLowerCase();
    if (RegExp(_noise).hasMatch(n)) return -1;
    if (exclude != null && RegExp(exclude, caseSensitive: false).hasMatch(n)) return -1;
    if (include != null && !RegExp(include, caseSensitive: false).hasMatch(n)) return -1;
    final ext = _archiveType(n);
    if (ext == null) return -1;

    final isWin = RegExp(r'win|msvc|mingw|\.exe$').hasMatch(n);
    final isMac = RegExp(r'mac|osx|darwin|apple|\.dmg$|\.app\.').hasMatch(n);
    final isLinux = RegExp(r'linux|appimage|ubuntu|flatpak').hasMatch(n);
    final isAndroid = n.endsWith('.apk');
    final isArm = RegExp(r'arm64|aarch64|armv8|arm\b').hasMatch(n);
    final isX86_32 = RegExp(r'(x86(?!_64)|i686|win32|32bit)').hasMatch(n) && !RegExp(r'x86_64|x64|amd64').hasMatch(n);

    var s = 0;
    switch (os) {
      case Os.android:
        if (!isAndroid) return -1;
        if (RegExp(r'x86|x64').hasMatch(n)) return -1;
        s += 10;
      case Os.windows:
        if (!isWin || isMac || isLinux || isAndroid || isX86_32) return -1;
        if (ext == 'exe') return -1; // installers need clicking through
        s += ext == 'zip' ? 6 : 4;
        if (isArm != (arch == Arch.arm64)) return -1;
      case Os.macos:
        if (!isMac || isWin || isAndroid) return -1;
        s += switch (ext) { 'zip' => 6, 'dmg' => 5, 'tar' => 5, _ => 3 };
        if (n.contains('universal')) s += 3;
        if (isArm && arch != Arch.arm64) return -1;
        if (RegExp(r'x86_64|x64|intel').hasMatch(n) && !n.contains('universal') && arch == Arch.arm64) s -= 4;
      case Os.linux:
        if (!isLinux || isWin || isMac || isAndroid) return -1;
        if (n.contains('flatpak')) return -1;
        s += ext == 'appimage' ? 7 : (ext == 'tar' ? 4 : 2);
        if (isArm != (arch == Arch.arm64)) return -1;
    }
    return s;
  }

  // Assets arrive newest release first: take the best match from the
  // newest release that has any match for this platform.
  final byVersion = <String, List<ReleaseAsset>>{};
  for (final a in assets) {
    byVersion.putIfAbsent(a.version, () => []).add(a);
  }
  for (final group in byVersion.values) {
    ReleaseAsset? best;
    var bestScore = 0;
    for (final a in group) {
      final s = score(a);
      if (s > bestScore) {
        best = a;
        bestScore = s;
      }
    }
    if (best != null) return best;
  }
  return null;
}

/// zip, tar (gz/xz/bz2), 7z, dmg, appimage, apk, exe — or null.
String? _archiveType(String name) {
  final n = name.toLowerCase();
  if (n.endsWith('.zip')) return 'zip';
  if (RegExp(r'\.(tar\.(gz|xz|bz2|zst)|tgz|txz)$').hasMatch(n)) return 'tar';
  if (n.endsWith('.7z')) return '7z';
  if (n.endsWith('.dmg')) return 'dmg';
  if (n.endsWith('.appimage')) return 'appimage';
  if (n.endsWith('.apk')) return 'apk';
  if (n.endsWith('.exe')) return 'exe';
  return null;
}

String? archiveType(String name) => _archiveType(name);

/// Release channels per emulator id (ids match external_catalog.dart).
const Map<String, EmulatorSource> emulatorSources = {
  'pcsx2': EmulatorSource(
    {Os.windows: GitHubReleases('PCSX2/pcsx2'), Os.linux: GitHubReleases('PCSX2/pcsx2'), Os.macos: GitHubReleases('PCSX2/pcsx2')},
    include: {Os.windows: r'qt'},
    exclude: r'avx2|sse4',
    homepage: 'https://pcsx2.net',
  ),
  'rpcs3': EmulatorSource({
    Os.windows: GitHubReleases('RPCS3/rpcs3-binaries-win', latestOnly: true),
    Os.linux: GitHubReleases('RPCS3/rpcs3-binaries-linux', latestOnly: true),
    Os.macos: GitHubReleases('RPCS3/rpcs3-binaries-mac', latestOnly: true),
  }, homepage: 'https://rpcs3.net'),
  'xemu': EmulatorSource(
    {Os.windows: GitHubReleases('xemu-project/xemu'), Os.linux: GitHubReleases('xemu-project/xemu'), Os.macos: GitHubReleases('xemu-project/xemu')},
    homepage: 'https://xemu.app',
  ),
  'xenia': EmulatorSource({
    Os.windows: GitHubReleases('xenia-canary/xenia-canary-releases'),
    Os.linux: GitHubReleases('xenia-canary/xenia-canary-releases'),
  }, homepage: 'https://xenia.jp'),
  'cemu': EmulatorSource(
    {Os.windows: GitHubReleases('cemu-project/Cemu'), Os.linux: GitHubReleases('cemu-project/Cemu'), Os.macos: GitHubReleases('cemu-project/Cemu')},
    homepage: 'https://cemu.info',
  ),
  'azahar': EmulatorSource({
    Os.windows: GitHubReleases('azahar-emu/azahar'),
    Os.linux: GitHubReleases('azahar-emu/azahar'),
    Os.macos: GitHubReleases('azahar-emu/azahar'),
    Os.android: GitHubReleases('azahar-emu/azahar'),
  }, include: {Os.android: r'vanilla'}, homepage: 'https://azahar-emu.org'),
  'simple64': EmulatorSource(
    {Os.windows: GitHubReleases('simple64/simple64')},
    homepage: 'https://simple64.github.io',
  ),
  'rmg': EmulatorSource(
    {Os.windows: GitHubReleases('Rosalie241/RMG'), Os.linux: GitHubReleases('Rosalie241/RMG')},
    include: {Os.windows: r'portable'},
    homepage: 'https://github.com/Rosalie241/RMG',
  ),
  'vita3k': EmulatorSource({
    Os.windows: GitHubReleases('Vita3K/Vita3K'),
    Os.linux: GitHubReleases('Vita3K/Vita3K'),
    Os.macos: GitHubReleases('Vita3K/Vita3K'),
    Os.android: GitHubReleases('Vita3K/Vita3K-Android'),
  }, homepage: 'https://vita3k.org'),
  'ppsspp': EmulatorSource({
    Os.windows: PageLinks('https://www.ppsspp.org/download/', r'[^"\s]*/files/[^"\s]*ppsspp_win[^"\s]*\.zip'),
    Os.macos: PageLinks('https://www.ppsspp.org/download/', r'[^"\s]*/files/[^"\s]*\.dmg'),
    Os.android: PageLinks('https://www.ppsspp.org/download/', r'[^"\s]*/files/[^"\s]*\.apk'),
  }, homepage: 'https://www.ppsspp.org'),
  'dolphin': EmulatorSource({
    Os.windows: PageLinks('https://dolphin-emu.org/download/', r'https://dl\.dolphin-emu\.org/releases/[^"\s]+-x64\.7z'),
    Os.macos: PageLinks('https://dolphin-emu.org/download/', r'https://dl\.dolphin-emu\.org/releases/[^"\s]+-universal\.dmg'),
    Os.android: PageLinks('https://dolphin-emu.org/download/', r'https://dl\.dolphin-emu\.org/releases/[^"\s]+\.apk'),
  }, include: {Os.windows: r'x64', Os.macos: r'universal'}, homepage: 'https://dolphin-emu.org'),
  'eden': EmulatorSource({
    Os.windows: ForgejoReleases('git.eden-emu.dev', 'eden-emu/eden'),
    Os.linux: ForgejoReleases('git.eden-emu.dev', 'eden-emu/eden'),
    Os.macos: ForgejoReleases('git.eden-emu.dev', 'eden-emu/eden'),
    Os.android: ForgejoReleases('git.eden-emu.dev', 'eden-emu/eden'),
  }, exclude: r'legacy|optimized|pgo', homepage: 'https://eden-emu.dev'),
  'ryujinx': EmulatorSource({
    Os.windows: GitLabReleases('git.ryujinx.app', 'ryubing/ryujinx'),
    Os.linux: GitLabReleases('git.ryujinx.app', 'ryubing/ryujinx'),
    Os.macos: GitLabReleases('git.ryujinx.app', 'ryubing/ryujinx'),
  }, exclude: r'headless|sdl2', homepage: 'https://ryujinx.app'),
  'armsx2': EmulatorSource({Os.android: GitHubReleases('ARMSX2/ARMSX2')}, homepage: 'https://github.com/ARMSX2/ARMSX2'),
};
