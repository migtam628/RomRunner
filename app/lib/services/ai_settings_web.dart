import 'package:web/web.dart' as web;

const _key = 'romrunner.ai';

Future<String?> readAiSettings() async {
  try {
    return web.window.localStorage.getItem(_key);
  } catch (_) {
    return null;
  }
}

Future<void> writeAiSettings(String json) async {
  try {
    web.window.localStorage.setItem(_key, json);
  } catch (_) {}
}
