import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:http/http.dart' as http;

import '../models/console.dart';
import 'ai_settings_native.dart' if (dart.library.js_interop) 'ai_settings_web.dart' as impl;

/// "Ask for help" while playing: the paused game's picture, its title and
/// system and the player's question go to Claude, which can search the web
/// for guides and answers with its sources.
///
/// The player brings their own Anthropic API key (Settings → AI help); it
/// stays on the device. [AiSettings.endpoint] can point at a proxy that adds
/// the key server-side instead, so no key has to be on the device at all.
class AiSettings {
  AiSettings({
    this.apiKey = '',
    this.model = defaultModel,
    this.webSearch = true,
    this.showButton = true,
    this.endpoint = '',
  });

  String apiKey;
  String model;
  bool webSearch;
  bool showButton;

  /// Empty: Anthropic's API. Otherwise a Messages-API-compatible URL.
  String endpoint;

  static const defaultModel = 'claude-sonnet-5-5';
  static const anthropicUrl = 'https://api.anthropic.com/v1/messages';

  static const models = <(String, String, String)>[
    ('claude-sonnet-5-5', 'Claude Sonnet 5.5', 'Balanced (recommended)'),
    ('claude-haiku-5-5', 'Claude Haiku 5.5', 'Fastest, cheapest'),
    ('claude-opus-5-5', 'Claude Opus 5.5', 'Most capable'),
  ];

  static String modelName(String id) => models.firstWhere((m) => m.$1 == id, orElse: () => (id, id, '')).$2;

  /// Ready to ask: a key, or a custom endpoint that supplies one.
  bool get configured => apiKey.trim().isNotEmpty || endpoint.trim().isNotEmpty;

  String get url => endpoint.trim().isEmpty ? anthropicUrl : endpoint.trim();

  Map<String, dynamic> toJson() => {
        'apiKey': apiKey,
        'model': model,
        'webSearch': webSearch,
        'showButton': showButton,
        'endpoint': endpoint,
      };

  factory AiSettings.fromJson(Map<String, dynamic> j) => AiSettings(
        apiKey: j['apiKey'] as String? ?? '',
        model: j['model'] as String? ?? defaultModel,
        webSearch: j['webSearch'] as bool? ?? true,
        showButton: j['showButton'] as bool? ?? true,
        endpoint: j['endpoint'] as String? ?? '',
      );

  static AiSettings? _cached;

  /// Tests: use [s] instead of reading the settings file.
  static void useForTesting(AiSettings s) => _cached = s;

  static Future<AiSettings> load() async {
    if (_cached != null) return _cached!;
    try {
      final raw = await impl.readAiSettings();
      _cached = raw == null ? AiSettings() : AiSettings.fromJson(jsonDecode(raw) as Map<String, dynamic>);
    } catch (_) {
      _cached = AiSettings();
    }
    return _cached!;
  }

  Future<void> save() async {
    _cached = this;
    await impl.writeAiSettings(jsonEncode(toJson()));
  }
}

class AiSource {
  const AiSource(this.title, this.url);
  final String title;
  final String url;
}

/// One message in the help conversation, as shown to the player.
class AiTurn {
  AiTurn.user(this.text, {this.image}) : fromUser = true, sources = const [];
  AiTurn.assistant(this.text, this.sources) : fromUser = false, image = null;

  final bool fromUser;
  final String text;
  final Uint8List? image;
  final List<AiSource> sources;
}

class AiHelpException implements Exception {
  AiHelpException(this.message);
  final String message;
  @override
  String toString() => message;
}

/// A conversation about one game. Lives as long as the game screen, so
/// follow-up questions ("I beat the guitarist, now what?") keep the context.
class AiHelpSession {
  AiHelpSession({required this.title, required this.console, http.Client? client}) : _client = client ?? http.Client();

  final String title;
  final Console console;
  final http.Client _client;

  /// What the player sees.
  final List<AiTurn> turns = [];

  /// What the API sees: the full messages, including the search results the
  /// API asks to be sent back unchanged on later turns.
  final List<Map<String, dynamic>> _messages = [];

  String get _system => '''
You are the game guide built into RomRunner, a retro game emulator. The player is playing "$title" on the ${console.fullName}. They paused the game and are asking for help; when a screenshot is attached it is exactly what is on their game screen right now.

- Work out where they are from the screenshot (area, menu, battle, puzzle) and help them get past what is stopping them.
- Be brief: they read this on a phone or TV over the paused game. A sentence of context, then short numbered steps. Bold the names of places, characters and items.
- Name buttons as on the original ${console.name} controller, and directions as up/down/left/right on screen.
- Don't spoil more than the question needs; give more if they ask.
- If the screenshot doesn't show enough to be sure, say what you see and ask one short question.
- Use web search to check a walkthrough or wiki when you aren't certain of the details for this exact game and version.''';

  /// Sends [question], with [screenshot] (PNG) when given, and returns the answer.
  Future<AiTurn> ask(String question, {Uint8List? screenshot}) async {
    final settings = await AiSettings.load();
    if (!settings.configured) {
      throw AiHelpException('Add your Anthropic API key in Settings → AI help.');
    }
    turns.add(AiTurn.user(question, image: screenshot));
    _messages.add({
      'role': 'user',
      'content': [
        if (screenshot != null)
          {
            'type': 'image',
            'source': {'type': 'base64', 'media_type': 'image/png', 'data': base64Encode(screenshot)},
          },
        {'type': 'text', 'text': question},
      ],
    });
    try {
      final answer = await _complete(settings);
      turns.add(answer);
      return answer;
    } catch (_) {
      // Leave the conversation as it was so the player can just ask again.
      _messages.removeLast();
      turns.removeLast();
      rethrow;
    }
  }

  Future<AiTurn> _complete(AiSettings s) async {
    final text = StringBuffer();
    final sources = <String, AiSource>{};
    // A long search can end a response with "pause_turn": send the partial
    // answer back and let it continue.
    final pending = <Map<String, dynamic>>[];
    for (var round = 0; round < 4; round++) {
      final body = {
        'model': s.model,
        'max_tokens': 1500,
        'system': _system,
        'messages': [..._messages, ...pending],
        if (s.webSearch)
          'tools': [
            {'type': 'web_search_20250305', 'name': 'web_search', 'max_uses': 3},
          ],
      };
      final http.Response res;
      try {
        res = await _client
            .post(
              Uri.parse(s.url),
              headers: {
                'content-type': 'application/json',
                'anthropic-version': '2023-06-01',
                if (s.apiKey.trim().isNotEmpty) 'x-api-key': s.apiKey.trim(),
                // Lets the web build call the API from the browser.
                'anthropic-dangerous-direct-browser-access': 'true',
              },
              body: jsonEncode(body),
            )
            .timeout(const Duration(seconds: 90));
      } on TimeoutException {
        throw AiHelpException('Connection to ${Uri.parse(s.url).host} timed out. Check your connection and retry.');
      } on Exception catch (e) {
        final detail = e.toString().toLowerCase();
        if (detail.contains('failed host lookup') || detail.contains('no address associated with hostname')) {
          throw AiHelpException(
            'Could not find ${Uri.parse(s.url).host}. Check your internet connection and DNS settings, then retry.',
          );
        }
        throw AiHelpException('Could not connect to ${Uri.parse(s.url).host}. Check your connection and retry.');
      }
      final Map<String, dynamic> json;
      try {
        json = jsonDecode(utf8.decode(res.bodyBytes)) as Map<String, dynamic>;
      } catch (_) {
        throw AiHelpException('Unexpected reply from ${Uri.parse(s.url).host} (HTTP ${res.statusCode}).');
      }
      if (res.statusCode != 200) throw AiHelpException(_errorText(res.statusCode, json));

      final content = (json['content'] as List? ?? const []).cast<Map<String, dynamic>>();
      for (final block in content) {
        if (block['type'] == 'text') {
          text.write(block['text'] as String? ?? '');
          for (final c in (block['citations'] as List? ?? const []).cast<Map<String, dynamic>>()) {
            final url = c['url'] as String?;
            if (url != null) sources[url] = AiSource(c['title'] as String? ?? Uri.parse(url).host, url);
          }
        }
      }
      pending.add({'role': 'assistant', 'content': content});
      if (json['stop_reason'] != 'pause_turn') break;
    }
    // Keep the whole assistant turn for follow-ups (continuations merged).
    _messages.add({
      'role': 'assistant',
      'content': [for (final m in pending) ...(m['content'] as List)],
    });
    final answer = text.toString().trim();
    if (answer.isEmpty) throw AiHelpException('No answer came back. Try asking again.');
    return AiTurn.assistant(answer, sources.values.toList());
  }

  static String _errorText(int status, Map<String, dynamic> json) {
    final msg = (json['error'] is Map ? json['error']['message'] : null) as String?;
    return switch (status) {
      401 => 'The API key was rejected. Check it in Settings → AI help.',
      403 => 'This API key isn\'t allowed to use that model or feature${msg != null ? ': $msg' : '.'}',
      429 => 'Rate limit or credit limit reached. Wait a moment, or check your Anthropic account.',
      529 || 503 => 'Claude is busy right now. Try again in a moment.',
      _ => msg ?? 'Request failed (HTTP $status).',
    };
  }

  void dispose() => _client.close();
}
