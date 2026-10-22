import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';
import 'package:romrunner/models/console.dart';
import 'package:romrunner/services/ai_help.dart';

http.Response _json(Object body, [int status = 200]) =>
    http.Response.bytes(utf8.encode(jsonEncode(body)), status, headers: {'content-type': 'application/json'});

void main() {
  setUp(() => AiSettings.useForTesting(AiSettings(apiKey: 'sk-test', model: 'claude-sonnet-5-5')));

  test('sends the screenshot, game and question; returns text and sources', () async {
    late Map<String, dynamic> sent;
    late Map<String, String> headers;
    final client = MockClient((req) async {
      sent = jsonDecode(req.body) as Map<String, dynamic>;
      headers = req.headers;
      return _json({
        'stop_reason': 'end_turn',
        'content': [
          {'type': 'server_tool_use', 'id': 'srvtoolu_1', 'name': 'web_search', 'input': {'query': 'virbank gym'}},
          {'type': 'web_search_tool_result', 'tool_use_id': 'srvtoolu_1', 'content': []},
          {'type': 'text', 'text': 'You are in **Virbank City**.\n'},
          {
            'type': 'text',
            'text': '1. Go south.',
            'citations': [
              {'type': 'web_search_result_location', 'url': 'https://bulbapedia.example/virbank', 'title': 'Virbank City'},
              {'type': 'web_search_result_location', 'url': 'https://bulbapedia.example/virbank', 'title': 'dup'},
            ],
          },
        ],
      });
    });
    final session = AiHelpSession(title: 'Pokemon Black 2', console: Console.nds, client: client);
    final png = Uint8List.fromList([137, 80, 78, 71]);
    final answer = await session.ask('Where do I go from here?', screenshot: png);

    expect(headers['x-api-key'], 'sk-test');
    expect(headers['anthropic-version'], '2023-06-01');
    expect(sent['model'], 'claude-sonnet-5-5');
    expect(sent['system'], contains('Pokemon Black 2'));
    expect(sent['system'], contains(Console.nds.fullName));
    expect((sent['tools'] as List).single['type'], 'web_search_20250305');
    final content = (sent['messages'] as List).single['content'] as List;
    expect(content.first['type'], 'image');
    expect(content.first['source']['data'], base64Encode(png));
    expect(content.last['text'], 'Where do I go from here?');

    expect(answer.text, 'You are in **Virbank City**.\n1. Go south.');
    expect(answer.sources.single.url, 'https://bulbapedia.example/virbank');
    expect(session.turns.length, 2);
  });

  test('follow-ups resend the earlier turns, including search results', () async {
    final bodies = <Map<String, dynamic>>[];
    final client = MockClient((req) async {
      bodies.add(jsonDecode(req.body) as Map<String, dynamic>);
      return _json({
        'stop_reason': 'end_turn',
        'content': [
          {'type': 'web_search_tool_result', 'tool_use_id': 'x', 'content': [{'type': 'web_search_result', 'encrypted_content': 'keep-me'}]},
          {'type': 'text', 'text': 'answer ${bodies.length}'},
        ],
      });
    });
    final session = AiHelpSession(title: 'Game', console: Console.n64, client: client);
    await session.ask('first', screenshot: Uint8List(4));
    await session.ask('I beat the guitarist');
    final msgs = bodies.last['messages'] as List;
    expect(msgs.length, 3);
    expect(msgs[1]['role'], 'assistant');
    expect(jsonEncode(msgs[1]), contains('keep-me'));
    expect((msgs[2]['content'] as List).single['type'], 'text'); // no new screenshot
  });

  test('continues a paused turn', () async {
    var calls = 0;
    final client = MockClient((req) async {
      calls++;
      return _json(calls == 1
          ? {'stop_reason': 'pause_turn', 'content': [{'type': 'text', 'text': 'Part one. '}]}
          : {'stop_reason': 'end_turn', 'content': [{'type': 'text', 'text': 'Part two.'}]});
    });
    final session = AiHelpSession(title: 'Game', console: Console.snes, client: client);
    final a = await session.ask('help');
    expect(calls, 2);
    expect(a.text, 'Part one. Part two.');
  });

  test('a rejected key gives a clear message and leaves the conversation unchanged', () async {
    final client = MockClient((req) async => _json({'type': 'error', 'error': {'type': 'authentication_error', 'message': 'invalid x-api-key'}}, 401));
    final session = AiHelpSession(title: 'Game', console: Console.gba, client: client);
    await expectLater(session.ask('help'), throwsA(isA<AiHelpException>().having((e) => e.message, 'message', contains('API key'))));
    expect(session.turns, isEmpty);
  });

  test('a DNS failure explains how to reconnect and keeps the conversation unchanged', () async {
    final client = MockClient((_) async => throw http.ClientException(
          'SocketException: Failed host lookup: api.anthropic.com (No address associated with hostname)',
        ));
    final session = AiHelpSession(title: 'Game', console: Console.nds, client: client);
    await expectLater(
      session.ask('Where do I go next?'),
      throwsA(isA<AiHelpException>().having((e) => e.message, 'message', allOf(contains('DNS'), contains('api.anthropic.com')))),
    );
    expect(session.turns, isEmpty);
  });

  test('a generic connection failure has a readable message', () async {
    final client = MockClient((_) async => throw http.ClientException('connection refused'));
    final session = AiHelpSession(title: 'Game', console: Console.nds, client: client);
    await expectLater(
      session.ask('Where do I go next?'),
      throwsA(isA<AiHelpException>().having((e) => e.message, 'message', contains('Check your connection'))),
    );
    expect(session.turns, isEmpty);
  });

  test('without a key or endpoint it asks for setup instead of calling out', () async {
    AiSettings.useForTesting(AiSettings());
    var called = false;
    final session = AiHelpSession(
        title: 'Game', console: Console.gba, client: MockClient((_) async { called = true; return _json({}); }));
    await expectLater(session.ask('help'), throwsA(isA<AiHelpException>()));
    expect(called, isFalse);
  });

  test('a custom endpoint is used and needs no key', () async {
    AiSettings.useForTesting(AiSettings(endpoint: 'https://proxy.example/v1/messages', webSearch: false));
    late http.Request seen;
    final client = MockClient((req) async {
      seen = req;
      return _json({'stop_reason': 'end_turn', 'content': [{'type': 'text', 'text': 'ok'}]});
    });
    await AiHelpSession(title: 'Game', console: Console.nes, client: client).ask('help');
    expect(seen.url.toString(), 'https://proxy.example/v1/messages');
    expect(seen.headers.containsKey('x-api-key'), isFalse);
    expect((jsonDecode(seen.body) as Map).containsKey('tools'), isFalse);
  });

  test('a bare host in Custom endpoint gets the Messages API path', () {
    expect(AiSettings.normalizeEndpoint(''), AiSettings.anthropicUrl);
    expect(AiSettings.normalizeEndpoint('https://api.anthropic.com'), 'https://api.anthropic.com/v1/messages');
    expect(AiSettings.normalizeEndpoint('https://api.anthropic.com/'), 'https://api.anthropic.com/v1/messages');
    expect(AiSettings.normalizeEndpoint('api.anthropic.com/v1'), 'https://api.anthropic.com/v1/messages');
    expect(AiSettings.normalizeEndpoint('https://proxy.example/claude/v1/messages'), 'https://proxy.example/claude/v1/messages');
    expect(AiSettings.normalizeEndpoint('https://proxy.example/ask'), 'https://proxy.example/ask');
  });

  test('a 404 with no body points at the endpoint setting', () async {
    AiSettings.useForTesting(AiSettings(apiKey: 'k', endpoint: 'https://wrong.example/x'));
    final session = AiHelpSession(title: 'G', console: Console.nds, client: MockClient((_) async => http.Response('', 404)));
    await expectLater(session.ask('help'), throwsA(isA<AiHelpException>().having((e) => e.message, 'message', contains('Custom endpoint'))));
  });
}
