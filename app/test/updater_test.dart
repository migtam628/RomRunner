import 'dart:convert';

import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';
import 'package:romrunner/services/updater.dart';

const _assets = [
  'RomRunner-0.1.13-android-arm64.apk',
  'RomRunner-0.1.13-android-armv7.apk',
  'RomRunner-0.1.13-android.apk',
  'RomRunner-0.1.13-ios-unsigned.ipa',
  'RomRunner-0.1.13-linux-x64.tar.gz',
  'RomRunner-0.1.13-macos.zip',
  'RomRunner-0.1.13-web.zip',
  'RomRunner-0.1.13-windows-x64.zip',
  'SHA256SUMS.txt',
];

List<ReleaseFile> get _files => [for (final n in _assets) ReleaseFile(n, 'https://example/$n', 1)];

void main() {
  test('versions compare like semver', () {
    expect(compareVersions('0.1.10', '0.1.9'), greaterThan(0));
    expect(compareVersions('0.1.12', '0.1.12'), 0);
    expect(compareVersions('0.1.12', '0.1.13'), lessThan(0));
    expect(compareVersions('0.2.0', '0.2.0-beta.1'), greaterThan(0));
    expect(compareVersions('0.2.0-beta.2', '0.2.0-beta.10'), lessThan(0));
    expect(compareVersions('0.1.12', '0.0.0-dev.22'), greaterThan(0));
    expect(compareVersions('1.0.0', '0.9.9+9'), greaterThan(0));
    expect(compareVersions('0.1.7+7', '0.1.7'), 0);
  });

  test('picks the file for each device', () {
    String? pick(UpdatePlatform p) => pickFile(_files, p)?.name;
    expect(pick(UpdatePlatform.androidArm64), 'RomRunner-0.1.13-android-arm64.apk');
    expect(pick(UpdatePlatform.androidArm), 'RomRunner-0.1.13-android-armv7.apk');   // Fire TV sticks
    expect(pick(UpdatePlatform.androidOther), 'RomRunner-0.1.13-android.apk');
    expect(pick(UpdatePlatform.windows), 'RomRunner-0.1.13-windows-x64.zip');
    expect(pick(UpdatePlatform.macos), 'RomRunner-0.1.13-macos.zip');
    expect(pick(UpdatePlatform.linux), 'RomRunner-0.1.13-linux-x64.tar.gz');
    expect(pick(UpdatePlatform.ios), 'RomRunner-0.1.13-ios-unsigned.ipa');
    expect(pick(UpdatePlatform.web), isNull);
  });

  test('older releases without per-CPU APKs fall back to the universal one', () {
    final old = [const ReleaseFile('RomRunner-0.1.5-android.apk', 'u', 1)];
    expect(pickFile(old, UpdatePlatform.androidArm)?.name, 'RomRunner-0.1.5-android.apk');
  });

  test('parses a GitHub release', () {
    final r = AppRelease.fromJson({
      'tag_name': 'v0.1.13',
      'body': '## Fixes\n- N64 controls',
      'html_url': 'https://github.com/migtam628/RomRunner/releases/tag/v0.1.13',
      'assets': [
        {'name': 'RomRunner-0.1.13-android-armv7.apk', 'browser_download_url': 'https://dl/x.apk', 'size': 95461258},
      ],
    });
    expect(r.version, '0.1.13');
    expect(r.files.single.size, 95461258);
    expect(r.notes, contains('N64 controls'));
  });

  test('check() asks the GitHub API for the latest release', () async {
    late Uri asked;
    final client = MockClient((req) async {
      asked = req.url;
      return http.Response(jsonEncode({'tag_name': 'v999.0.0', 'assets': []}), 200);
    });
    // currentVersion() needs the platform plugin; the request itself is what's checked here.
    try {
      await Updater.instance.check(client: client);
    } catch (_) {}
    expect(asked.toString(), 'https://api.github.com/repos/migtam628/RomRunner/releases/latest');
  });

  test("the dialog shows only What's new", () {
    const body = "## What's new\n\n- N64 controls respond\n- Fire TV APK\n\n## Install\n\n| Platform | File |\n|---|---|\n\n**Full Changelog**: https://x";
    final r = AppRelease.fromJson({'tag_name': 'v0.1.13', 'body': body});
    expect(r.whatsNew, '- N64 controls respond\n- Fire TV APK');
    final old = AppRelease.fromJson({'tag_name': 'v0.1.12', 'body': '| a | b |\n|---|---|\n\nBring your own ROMs.\n\n**Full Changelog**: https://x'});
    expect(old.whatsNew, isEmpty);
  });
}
