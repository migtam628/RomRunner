import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:romrunner/services/app_icon.dart';

void main() {
  test('every icon has its source, asset, Android alias and iOS set', () {
    final svgs = Directory('tool/icons/svg').listSync().map((f) => f.path.split('/').last).toList();
    final manifest = File('android/app/src/main/AndroidManifest.xml').readAsStringSync();
    expect(appIcons.length, svgs.length);
    expect({for (final i in appIcons) i.id}.length, appIcons.length);
    for (final i in appIcons) {
      expect(svgs.any((f) => f.endsWith('_${i.id}.svg')), isTrue, reason: i.id);
      expect(File(i.asset).existsSync(), isTrue, reason: i.asset);
      expect(manifest, contains('android:name="${i.androidAlias}"'));
      expect(File('android/app/src/main/res/mipmap-anydpi-v26/ic_icon_${i.id}.xml').existsSync(), isTrue);
      final ios = i.iosName ?? 'AppIcon';
      expect(File('ios/Runner/Assets.xcassets/$ios.appiconset/icon-1024.png').existsSync(), isTrue, reason: ios);
    }
    // Exactly one launcher alias is on out of the box: the default icon.
    expect(RegExp(r'android:enabled="true"').allMatches(manifest).length, 1);
    expect(manifest, contains('android:name="${appIconById(defaultAppIcon).androidAlias}"'));
    final pbx = File('ios/Runner.xcodeproj/project.pbxproj').readAsStringSync();
    for (final i in appIcons.where((i) => i.iosName != null)) {
      expect(pbx, contains(i.iosName!));
    }
  });
}
