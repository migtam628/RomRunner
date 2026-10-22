import 'updater.dart';

/// The web build is served fresh from the site; nothing to update.
UpdatePlatform platform() => UpdatePlatform.web;

Future<bool> autoCheck() async => false;
Future<void> setAutoCheck(bool on) async {}
Future<DateTime?> lastCheck() async => null;
Future<void> saveLastCheck(DateTime t) async {}
Future<String?> skipped() async => null;
Future<void> setSkipped(String v) async {}

Future<UpdateOutcome> install(ReleaseFile f, {void Function(double?)? onProgress}) async =>
    throw UpdateException('The web version updates itself.');
