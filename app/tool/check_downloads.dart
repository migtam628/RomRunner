// Resolves every emulator download source for every platform and prints
// what would be installed. Run in CI (needs network):
//
//   dart run tool/check_downloads.dart            # human-readable
//   dart run tool/check_downloads.dart --github   # also ::notice/::warning annotations
import 'dart:io';

import 'package:http/http.dart' as http;
import 'package:romrunner/services/emulator_sources.dart';

Future<void> main(List<String> args) async {
  final annotate = args.contains('--github');
  GitHubReleases.token = Platform.environment['GITHUB_TOKEN'];
  final client = http.Client();
  var failures = 0;
  final cache = <Source, List<ReleaseAsset>>{};
  for (final MapEntry(key: id, value: src) in emulatorSources.entries) {
    for (final MapEntry(key: os, value: source) in src.sources.entries) {
      for (final arch in os == Os.android ? [Arch.arm64] : (os == Os.windows ? [Arch.x64] : [Arch.x64, Arch.arm64])) {
        String line;
        try {
          final assets = cache[source] ??= await source.assets(client);
          final pick = pickAsset(assets, os, arch, include: src.include[os], exclude: src.exclude);
          if (pick == null) {
            final sample = assets.take(12).map((a) => a.name).join(' | ');
            line = 'MISS $id ${os.name}/${arch.name}: none of ${assets.length} assets matched. Seen: $sample';
            if (arch == Arch.x64 || os == Os.android) failures++;
          } else {
            line = 'OK   $id ${os.name}/${arch.name}: ${pick.name} [${pick.version}] ${pick.url}';
          }
        } catch (e) {
          line = 'FAIL $id ${os.name}/${arch.name}: $e';
          failures++;
        }
        stdout.writeln(line);
        if (annotate) stdout.writeln('${line.startsWith('OK') ? '::notice' : '::warning'} title=$id ${os.name}/${arch.name}::${line.substring(5)}');
      }
    }
  }
  client.close();
  stdout.writeln('\n$failures problem(s)');
}
