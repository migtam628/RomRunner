import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

void main() {
  test('Android release manifest requests internet access', () {
    // Debug/profile builds have separate manifests with INTERNET permission;
    // the main manifest must include it so installed release APKs can connect.
    final manifest = File('android/app/src/main/AndroidManifest.xml').readAsStringSync();
    expect(
      manifest,
      contains(RegExp(r'<uses-permission\b[^>]*android:name="android\.permission\.INTERNET"[^>]*/>')),
    );
  });
}
