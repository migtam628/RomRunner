import 'dart:convert';

import 'package:http/http.dart' as http;
import 'package:package_info_plus/package_info_plus.dart';

import 'updater_native.dart' if (dart.library.js_interop) 'updater_web.dart' as impl;

/// In-app updates from the project's GitHub releases.
///
/// Checks the latest (non-pre-release) release, compares it with this
/// build's version and picks the file for this device: the APK for its CPU
/// on Android (installed through Android's installer, over the current app,
/// since releases share a signing key), the zip / tarball on desktop, the
/// IPA on iOS. The web build updates itself when the site is redeployed.
class Updater {
  Updater._();
  static final Updater instance = Updater._();

  static const repo = 'migtam628/RomRunner';
  static const releasesPage = 'https://github.com/$repo/releases/latest';

  /// Not more often than this when checking on its own.
  static const autoInterval = Duration(hours: 12);

  final UpdatePlatform platform = impl.platform();

  String? _current;
  Future<String> currentVersion() async => _current ??= (await PackageInfo.fromPlatform()).version;

  /// The newest release, or null when this build is up to date.
  Future<AppRelease?> check({http.Client? client}) async {
    final c = client ?? http.Client();
    try {
      final res = await c.get(
        Uri.parse('https://api.github.com/repos/$repo/releases/latest'),
        headers: {'Accept': 'application/vnd.github+json', 'User-Agent': 'RomRunner'},
      ).timeout(const Duration(seconds: 20));
      if (res.statusCode == 404) return null;   // no releases yet
      if (res.statusCode != 200) throw UpdateException('GitHub answered HTTP ${res.statusCode}.');
      final json = jsonDecode(utf8.decode(res.bodyBytes)) as Map<String, dynamic>;
      final release = AppRelease.fromJson(json);
      await impl.saveLastCheck(DateTime.now());
      final current = await currentVersion();
      return compareVersions(release.version, current) > 0 ? release : null;
    } on UpdateException {
      rethrow;
    } catch (e) {
      throw UpdateException('Couldn\'t check for updates: ${e.toString().split('\n').first}');
    } finally {
      if (client == null) c.close();
    }
  }

  /// Checks only if automatic checks are on and the last one was a while ago.
  Future<AppRelease?> checkIfDue() async {
    if (!platform.supported || !await impl.autoCheck()) return null;
    final last = await impl.lastCheck();
    if (last != null && DateTime.now().difference(last) < autoInterval) return null;
    try {
      final r = await check();
      if (r != null && await impl.skipped() == r.version) return null;   // "Skip this version"
      return r;
    } catch (_) {
      return null;   // quietly try again later
    }
  }

  Future<bool> autoCheck() => impl.autoCheck();
  Future<void> setAutoCheck(bool on) => impl.setAutoCheck(on);
  Future<void> skip(String version) => impl.setSkipped(version);

  /// The file for this device in [r], or null if there isn't one.
  ReleaseFile? fileFor(AppRelease r) => pickFile(r.files, platform);

  /// Downloads and installs (Android) or downloads and opens (desktop) [f].
  /// [onProgress] gets 0..1 when the size is known.
  Future<UpdateOutcome> install(ReleaseFile f, {void Function(double?)? onProgress}) =>
      impl.install(f, onProgress: onProgress);
}

/// Which build this is, for picking the right release file.
enum UpdatePlatform { androidArm64, androidArm, androidOther, ios, macos, windows, linux, web }

extension UpdatePlatformInfo on UpdatePlatform {
  bool get supported => this != UpdatePlatform.web;
  bool get isAndroid => this == UpdatePlatform.androidArm64 || this == UpdatePlatform.androidArm || this == UpdatePlatform.androidOther;
}

class ReleaseFile {
  const ReleaseFile(this.name, this.url, this.size);
  final String name;
  final String url;
  final int size;
}

class AppRelease {
  const AppRelease({required this.version, required this.tag, required this.notes, required this.page, required this.files});

  final String version;
  final String tag;
  final String notes;

  /// What changed, for the update dialog: the "What's new" section the
  /// release workflow writes, without the install table and links that
  /// follow it. Older releases have only the table: then empty.
  String get whatsNew {
    final i = notes.indexOf("## What's new");
    if (i >= 0) {
      var rest = notes.substring(i + "## What's new".length);
      final end = RegExp(r'\n\s*(## |\*\*Full Changelog)').firstMatch(rest);
      if (end != null) rest = rest.substring(0, end.start);
      return rest.trim();
    }
    final kept = notes
        .split('\n')
        .where((l) => !l.trimLeft().startsWith('|') && !l.contains('Full Changelog') && !l.startsWith('Bring your own') && !l.startsWith('Switch games open'))
        .join('\n')
        .trim();
    return kept;
  }
  final String page;
  final List<ReleaseFile> files;

  factory AppRelease.fromJson(Map<String, dynamic> j) {
    final tag = j['tag_name'] as String? ?? '';
    return AppRelease(
      tag: tag,
      version: tag.startsWith('v') ? tag.substring(1) : tag,
      notes: (j['body'] as String? ?? '').trim(),
      page: j['html_url'] as String? ?? Updater.releasesPage,
      files: [
        for (final a in (j['assets'] as List? ?? const []).cast<Map<String, dynamic>>())
          ReleaseFile(a['name'] as String? ?? '', a['browser_download_url'] as String? ?? '', (a['size'] as num?)?.toInt() ?? 0),
      ],
    );
  }
}

enum UpdateOutcome {
  /// Android's installer is showing; the app restarts after Install.
  installerOpened,

  /// Android asked the user to allow RomRunner to install apps first.
  needsPermission,

  /// Desktop / iOS: the file was downloaded or opened in the browser.
  downloaded,
}

class UpdateException implements Exception {
  UpdateException(this.message);
  final String message;
  @override
  String toString() => message;
}

/// The release file for [platform], by the names the release workflow uses
/// (`RomRunner-<ver>-android-arm64.apk`, -android-armv7.apk, -android.apk,
/// -windows-x64.zip, -macos.zip, -linux-x64.tar.gz, -ios-unsigned.ipa).
ReleaseFile? pickFile(List<ReleaseFile> files, UpdatePlatform platform) {
  ReleaseFile? ending(String suffix) {
    for (final f in files) {
      if (f.name.endsWith(suffix)) return f;
    }
    return null;
  }

  return switch (platform) {
    UpdatePlatform.androidArm64 => ending('-android-arm64.apk') ?? ending('-android.apk'),
    UpdatePlatform.androidArm => ending('-android-armv7.apk') ?? ending('-android.apk'),
    UpdatePlatform.androidOther => ending('-android.apk'),
    UpdatePlatform.ios => ending('-ios-unsigned.ipa'),
    UpdatePlatform.macos => ending('-macos.zip'),
    UpdatePlatform.windows => ending('-windows-x64.zip'),
    UpdatePlatform.linux => ending('-linux-x64.tar.gz'),
    UpdatePlatform.web => null,
  };
}

/// Compares dotted versions with optional pre-release suffixes the way
/// semver does: 0.1.10 > 0.1.9, and 0.2.0 > 0.2.0-beta.1.
int compareVersions(String a, String b) {
  (List<int>, String?) parse(String v) {
    final plus = v.indexOf('+');
    if (plus >= 0) v = v.substring(0, plus);
    final dash = v.indexOf('-');
    final core = dash >= 0 ? v.substring(0, dash) : v;
    final pre = dash >= 0 ? v.substring(dash + 1) : null;
    return (core.split('.').map((s) => int.tryParse(s) ?? 0).toList(), pre);
  }

  final (ca, pa) = parse(a);
  final (cb, pb) = parse(b);
  for (var i = 0; i < 3; i++) {
    final x = i < ca.length ? ca[i] : 0, y = i < cb.length ? cb[i] : 0;
    if (x != y) return x.compareTo(y);
  }
  if (pa == null && pb == null) return 0;
  if (pa == null) return 1;    // a release beats its pre-releases
  if (pb == null) return -1;
  final xa = pa.split('.'), xb = pb.split('.');
  for (var i = 0; i < xa.length && i < xb.length; i++) {
    final na = int.tryParse(xa[i]), nb = int.tryParse(xb[i]);
    final c = na != null && nb != null ? na.compareTo(nb) : xa[i].compareTo(xb[i]);
    if (c != 0) return c;
  }
  return xa.length.compareTo(xb.length);
}
