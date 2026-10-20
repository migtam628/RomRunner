import 'package:flutter_test/flutter_test.dart';
import 'package:romrunner/services/emulator_sources.dart';

/// Real asset names seen on the projects' release pages (via CI).
List<ReleaseAsset> rel(String version, List<String> names) =>
    [for (final n in names) ReleaseAsset(n, 'https://example/$n', version: version)];

String? pick(List<ReleaseAsset> a, Os os, [Arch arch = Arch.x64, String? include, String? exclude]) =>
    pickAsset(a, os, arch, include: include, exclude: exclude)?.name;

void main() {
  test('PCSX2', () {
    final a = rel('v2.9.131', [
      'pcsx2-v2.9.131-linux-appimage-x64-Qt.AppImage',
      'pcsx2-v2.9.131-linux-flatpak-x64-Qt.flatpak',
      'pcsx2-v2.9.131-macos-Qt.tar.xz',
      'pcsx2-v2.9.131-windows-x64-Qt-symbols.7z',
      'pcsx2-v2.9.131-windows-x64-Qt.7z',
    ]);
    expect(pick(a, Os.windows, Arch.x64, 'qt', 'avx2|sse4'), 'pcsx2-v2.9.131-windows-x64-Qt.7z');
    expect(pick(a, Os.linux), 'pcsx2-v2.9.131-linux-appimage-x64-Qt.AppImage');
    expect(pick(a, Os.macos, Arch.arm64), 'pcsx2-v2.9.131-macos-Qt.tar.xz');
    expect(pick(a, Os.linux, Arch.arm64), isNull);
  });

  test('Vita3K continuous build', () {
    final a = rel('continuous', [
      'android-latest.apk', 'macos-arm64-latest.dmg', 'macos-latest.dmg', 'ubuntu-aarch64-latest.zip',
      'ubuntu-latest.zip', 'Vita3K-aarch64.AppImage', 'Vita3K-aarch64.AppImage.zsync', 'Vita3K-x86_64.AppImage',
      'Vita3K-x86_64.AppImage.zsync', 'windows-arm64-latest.zip', 'windows-latest.zip',
    ]);
    expect(pick(a, Os.windows), 'windows-latest.zip');
    expect(pick(a, Os.macos, Arch.arm64), 'macos-arm64-latest.dmg');
    expect(pick(a, Os.macos, Arch.x64), 'macos-latest.dmg');
    expect(pick(a, Os.linux), 'Vita3K-x86_64.AppImage');
    expect(pick(a, Os.linux, Arch.arm64), 'Vita3K-aarch64.AppImage');
    expect(pick(a, Os.android, Arch.arm64), 'android-latest.apk');
  });

  test('Cemu, RMG, Xenia, xemu, RPCS3', () {
    final cemu = rel('v2.6', ['cemu-2.6-macos-12-x64.dmg', 'cemu-2.6-ubuntu-22.04-x64.zip', 'cemu-2.6-windows-x64.zip', 'Cemu-2.6-x86_64.AppImage']);
    expect(pick(cemu, Os.windows), 'cemu-2.6-windows-x64.zip');
    expect(pick(cemu, Os.linux), 'Cemu-2.6-x86_64.AppImage');
    expect(pick(cemu, Os.macos), 'cemu-2.6-macos-12-x64.dmg');

    final rmg = rel('v0.9.0', ['RMG-Portable-Linux64-v0.9.0.AppImage', 'RMG-Portable-Windows64-v0.9.0.zip', 'RMG-Setup-Windows64-v0.9.0.exe']);
    expect(pick(rmg, Os.windows, Arch.x64, 'portable'), 'RMG-Portable-Windows64-v0.9.0.zip');
    expect(pick(rmg, Os.linux), 'RMG-Portable-Linux64-v0.9.0.AppImage');

    final xenia = rel('abc', ['xenia_canary_linux.tar.xz', 'xenia_canary_windows.zip']);
    expect(pick(xenia, Os.windows), 'xenia_canary_windows.zip');
    expect(pick(xenia, Os.linux), 'xenia_canary_linux.tar.xz');

    final xemu = rel('pre-release', ['xemu-0.8.137-x86_64.AppImage', 'xemu-0.8.137-windows-x86_64.zip', 'xemu-0.8.137-windows-aarch64.zip']);
    expect(pick(xemu, Os.windows), 'xemu-0.8.137-windows-x86_64.zip');
    expect(pick(xemu, Os.linux), 'xemu-0.8.137-x86_64.AppImage');

    final rpcs3 = rel('build-x', ['rpcs3-v0.0.43-20291-ffba2bf0_win64.7z', 'rpcs3-v0.0.43-20291-ffba2bf0_win64.7z.sha256']);
    expect(pick(rpcs3, Os.windows), 'rpcs3-v0.0.43-20291-ffba2bf0_win64.7z');
  });

  test('Azahar skips libretro and Play Store bundles', () {
    final a = rel('2127.0-alpha3', [
      'azahar-android-googleplay-2127.0-alpha3.aab',
      'azahar-android-googleplay-2127.0-alpha3.apk',
      'azahar-android-vanilla-2127.0-alpha3.aab',
      'azahar-android-vanilla-2127.0-alpha3.apk',
      'azahar-libretro-android-arm64-v8a-2127.0-alpha3.zip',
      'azahar-libretro-linux-x86_64-2127.0-alpha3.zip',
    ]);
    expect(pick(a, Os.android, Arch.arm64, 'vanilla'), 'azahar-android-vanilla-2127.0-alpha3.apk');
    expect(pick(a, Os.linux), isNull);
  });

  test('newest release with a match wins', () {
    final a = [
      ...rel('v3', ['tool-v3-linux.AppImage']),
      ...rel('v2', ['tool-v2-windows-x64.zip', 'tool-v2-linux.AppImage']),
    ];
    expect(pick(a, Os.linux), 'tool-v3-linux.AppImage');
    expect(pick(a, Os.windows), 'tool-v2-windows-x64.zip');
  });
}
