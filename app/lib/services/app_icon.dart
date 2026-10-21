import 'app_icon_native.dart' if (dart.library.js_interop) 'app_icon_web.dart' as impl;

/// One of the app icons the user can pick in Settings. Image files are made by
/// tool/icons/generate_icons.py from `tool/icons/svg/<nn>_<id>.svg`.
class AppIconOption {
  const AppIconOption(this.id, this.name);
  final String id;
  final String name;

  String get asset => 'assets/app_icons/$id.png';
  String get _camel => id.split('_').map((p) => p[0].toUpperCase() + p.substring(1)).join();

  /// Android launcher alias (AndroidManifest.xml).
  String get androidAlias => '.Icon$_camel';

  /// iOS alternate icon set; null for the primary icon.
  String? get iosName => id == defaultAppIcon ? null : 'AppIcon-$_camel';
}

const defaultAppIcon = 'play_r';

const appIcons = [
  AppIconOption('play_r', 'Play R'),
  AppIconOption('cartridge', 'Play Cartridge'),
  AppIconOption('speed_dpad', 'Speed D-pad'),
  AppIconOption('pixel_rr', 'Pixel RR'),
  AppIconOption('handheld', 'Handheld Play'),
  AppIconOption('neon_r', 'Neon Runner R'),
  AppIconOption('play_pad', 'Play Pad'),
  AppIconOption('pixel_runner', 'Pixel Runner'),
  AppIconOption('cart_stack', 'Cartridge Stack'),
  AppIconOption('retro_tv', 'Retro TV'),
  AppIconOption('arcade_stick', 'Arcade Stick'),
  AppIconOption('disc_cart', 'Disc + Cart'),
];

AppIconOption appIconById(String? id) =>
    appIcons.firstWhere((i) => i.id == id, orElse: () => appIcons.firstWhere((i) => i.id == defaultAppIcon));

/// Switches the app's icon: the launcher icon on Android and iOS, the
/// window / Dock icon on desktop (while RomRunner runs), the tab icon on web.
abstract class AppIcons {
  static final AppIcons instance = impl.createAppIcons();

  AppIconOption current = appIconById(defaultAppIcon);

  /// Reads the saved choice and applies it where that has to happen at
  /// every start (desktop window icon, browser tab).
  Future<void> load();

  /// Changes the icon. Returns a note to show the user, if any.
  Future<String?> set(AppIconOption icon);

  /// What changing the icon affects on this platform.
  String get explanation;
}
