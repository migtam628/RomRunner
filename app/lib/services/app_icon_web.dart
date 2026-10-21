import 'package:web/web.dart' as web;

import 'app_icon.dart';

AppIcons createAppIcons() => _WebAppIcons();

class _WebAppIcons extends AppIcons {
  static const _key = 'romrunner.appIcon';

  @override
  String get explanation => 'Changes the icon on this browser tab.';

  @override
  Future<void> load() async {
    try {
      current = appIconById(web.window.localStorage.getItem(_key));
    } catch (_) {}
    _apply();
  }

  @override
  Future<String?> set(AppIconOption icon) async {
    current = icon;
    try {
      web.window.localStorage.setItem(_key, icon.id);
    } catch (_) {}
    _apply();
    return null;
  }

  void _apply() {
    // Flutter web serves asset "x" at "assets/x".
    final href = 'assets/${current.asset}';
    final links = web.document.querySelectorAll('link[rel="icon"], link[rel="apple-touch-icon"]');
    for (var i = 0; i < links.length; i++) {
      (links.item(i) as web.HTMLLinkElement).href = href;
    }
  }
}
