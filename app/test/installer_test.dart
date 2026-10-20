import 'dart:io';

import 'package:archive/archive_io.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:romrunner/services/emulator_installer.dart';
import 'package:romrunner/services/emulator_sources.dart';
import 'package:romrunner/services/external_catalog.dart';

void main() {
  late Directory tmp;
  setUp(() => tmp = Directory.systemTemp.createTempSync('inst'));
  tearDown(() => tmp.deleteSync(recursive: true));

  Directory dest() => Directory('${tmp.path}/out')..createSync();

  test('zip: finds the emulator binary in a subfolder', () async {
    final enc = ZipFileEncoder()..create('${tmp.path}/xemu.zip');
    final inner = File('${tmp.path}/xemu')..writeAsStringSync('#!/bin/sh');
    final readme = File('${tmp.path}/README.txt')..writeAsStringSync('hi');
    await enc.addFile(inner, 'xemu-0.8/xemu');
    await enc.addFile(readme, 'xemu-0.8/README.txt');
    await enc.close();
    final out = dest();
    await unpackArchive(File('${tmp.path}/xemu.zip'), out, 'zip');
    expect(findExecutable(emulatorById('xemu')!, out, Os.linux), endsWith('xemu-0.8/xemu'));
  });

  test('tar.xz via system tar', () async {
    final src = Directory('${tmp.path}/src/rpcs3')..createSync(recursive: true);
    File('${src.path}/rpcs3').writeAsStringSync('#!/bin/sh');
    await Process.run('tar', ['-cJf', '${tmp.path}/r.tar.xz', '-C', '${tmp.path}/src', 'rpcs3']);
    final out = dest();
    await unpackArchive(File('${tmp.path}/r.tar.xz'), out, 'tar');
    expect(findExecutable(emulatorById('rpcs3')!, out, Os.linux), endsWith('rpcs3/rpcs3'));
  });

  test('AppImage is kept as-is and found', () async {
    final img = File('${tmp.path}/pcsx2-v2.9.131-linux-appimage-x64-Qt.AppImage')..writeAsStringSync('ELF');
    final out = dest();
    await unpackArchive(img, out, 'appimage');
    expect(findExecutable(emulatorById('pcsx2')!, out, Os.linux), endsWith('.AppImage'));
  });

  test('Windows and macOS names', () {
    final out = dest();
    Directory('${out.path}/PCSX2').createSync();
    File('${out.path}/PCSX2/pcsx2-qt.exe').writeAsStringSync('MZ');
    Directory('${out.path}/Dolphin.app/Contents/MacOS').createSync(recursive: true);
    expect(findExecutable(emulatorById('pcsx2')!, out, Os.windows), endsWith('pcsx2-qt.exe'));
    expect(findExecutable(emulatorById('dolphin')!, out, Os.macos), endsWith('Dolphin.app'));
  });
}
