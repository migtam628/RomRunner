import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

// Flutter only adds INTERNET to the debug/profile manifests. Without it in the
// main manifest, release APKs can't load cover art or download emulators.
void main() {
  test('release Android manifest can use the network', () {
    final manifest = File('android/app/src/main/AndroidManifest.xml').readAsStringSync();
    expect(manifest, contains('android.permission.INTERNET'));
  });
}
