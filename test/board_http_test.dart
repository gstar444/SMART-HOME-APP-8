import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:esp32_controller/esp32_api.dart';
import 'package:esp32_controller/settings.dart';
import 'package:esp32_controller/strings.dart';
import 'package:flutter_test/flutter_test.dart';

class Seen {
  Seen(this.method, this.uri, this.headers, this.body);
  final String method;
  final Uri uri;
  final HttpHeaders headers;
  final String body;
}

void main() {
  late HttpServer server;
  late String base;
  late List<Seen> seen;
  late Future<void> Function(HttpRequest req) reply;

  Future<void> json(HttpRequest req, int status, Object body) async {
    req.response
      ..statusCode = status
      ..headers.contentType = ContentType.json
      ..write(jsonEncode(body));
    await req.response.close();
  }

  setUp(() async {
    seen = <Seen>[];
    reply = (req) => json(req, 200, {'ok': true});
    server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    base = 'http://127.0.0.1:${server.port}';
    server.listen((req) async {
      final body = await utf8.decoder.bind(req).join();
      seen.add(Seen(req.method, req.uri, req.headers, body));
      await reply(req);
    });
  });

  tearDown(() => server.close(force: true));

  Matcher apiError(ApiErrorKind kind, [String? detail]) => throwsA(
        isA<ApiException>()
            .having((e) => e.kind, 'kind', kind)
            .having((e) => e.detail, 'detail', detail == null ? anything : equals(detail)),
      );

  group('what goes over the wire', () {
    test('reads are GET with the token in the Authorization header only', () async {
      final http = BoardHttp(baseUrl: base, token: 'tok-1234567890');
      final r = await http.call('GET', 'state');
      expect(r['ok'], true);
      final s = seen.single;
      expect(s.method, 'GET');
      expect(s.uri.path, '/api/v2/state');
      expect(s.uri.hasQuery, isFalse);
      expect(s.headers.value('authorization'), 'Bearer tok-1234567890');
      expect(s.uri.toString(), isNot(contains('tok-1234567890')));
      expect(s.body, isEmpty);
      http.close();
    });

    test('every change is a POST with a JSON body, never a query string', () async {
      final http = BoardHttp(baseUrl: base, token: 'tok-1234567890');
      await http.call('POST', 'set', body: {'id': 'light', 'on': true});
      await http.call('POST', 'wifi/connect', body: {'ssid': 'Home', 'password': 'pa55 word'});
      await http.call('POST', 'wifi/scan'); // no body given: still a valid JSON object
      expect(seen.map((s) => s.method), everyElement('POST'));
      expect(seen.every((s) => !s.uri.hasQuery), isTrue);
      expect(seen[0].headers.contentType?.mimeType, 'application/json');
      expect(jsonDecode(seen[0].body), {'id': 'light', 'on': true});
      expect(jsonDecode(seen[1].body), {'ssid': 'Home', 'password': 'pa55 word'});
      expect(seen[1].uri.toString(), isNot(contains('pa55'))); // the Wi-Fi password is never in a URL
      expect(jsonDecode(seen[2].body), <String, dynamic>{});
      http.close();
    });

    test('info and claim are sent without any token', () async {
      final http = BoardHttp(baseUrl: base);
      await http.call('GET', 'info', auth: false);
      await http.call('POST', 'claim', body: {'code': 'ABCDE-FGHJK', 'token': 'x' * 43}, auth: false);
      expect(seen.every((s) => s.headers.value('authorization') == null), isTrue);
      http.close();
    });

    test('a call that needs a token fails before any request when none is stored', () async {
      final http = BoardHttp(baseUrl: base);
      await expectLater(http.call('GET', 'state'), apiError(ApiErrorKind.auth, 'no_token'));
      await expectLater(BoardHttp(baseUrl: base, token: '').call('POST', 'set'), apiError(ApiErrorKind.auth));
      expect(seen, isEmpty);
      http.close();
    });
  });

  group('answers', () {
    Future<void> expectKind(int status, Object body, ApiErrorKind kind, [String? detail]) async {
      reply = (req) => json(req, status, body);
      final http = BoardHttp(baseUrl: base, token: 'tok-1234567890');
      await expectLater(http.call('GET', 'state'), apiError(kind, detail));
      http.close();
    }

    test('401 means the token was rejected', () => expectKind(401, {'ok': false, 'error': 'unauthorized'}, ApiErrorKind.auth));
    test('403 not_claimed means the board still needs to be claimed', () => expectKind(403, {'ok': false, 'error': 'not_claimed'}, ApiErrorKind.notClaimed));
    test('429 means too many attempts', () => expectKind(429, {'ok': false, 'error': 'too_many_attempts'}, ApiErrorKind.rateLimited));
    test('409 carries the board\'s reason', () => expectKind(409, {'ok': false, 'error': 'dnd'}, ApiErrorKind.server, 'dnd'));
    test('an unknown route is reported as old firmware', () => expectKind(404, {'ok': false, 'error': 'not_found_or_wrong_method'}, ApiErrorKind.firmware));

    test('a web page instead of JSON means firmware 3.x without /api/v2', () async {
      reply = (req) async {
        req.response
          ..statusCode = 200
          ..headers.contentType = ContentType.html
          ..write('<html>old firmware</html>');
        await req.response.close();
      };
      final http = BoardHttp(baseUrl: base, token: 'tok-1234567890');
      await expectLater(http.call('GET', 'state'), apiError(ApiErrorKind.firmware));
      http.close();
    });

    test('a server error without JSON is still a server error with the status', () async {
      reply = (req) async {
        req.response.statusCode = 500;
        await req.response.close();
      };
      final http = BoardHttp(baseUrl: base, token: 'tok-1234567890');
      await expectLater(http.call('GET', 'state'), apiError(ApiErrorKind.server, 'HTTP 500'));
      http.close();
    });

    test('an unreachable board is a network error', () async {
      final dead = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
      final url = 'http://127.0.0.1:${dead.port}';
      await dead.close(force: true);
      final http = BoardHttp(baseUrl: url, token: 'tok-1234567890');
      await expectLater(http.call('GET', 'state'), apiError(ApiErrorKind.network));
      http.close();
    });
  });

  test('every error kind has a readable message in both languages, and codes become sentences', () {
    for (final hindi in [true, false]) {
      final tr = Tr.of(AppSettings(hindi: hindi));
      for (final kind in ApiErrorKind.values) {
        final msg = describeApiException(tr, ApiException(kind, 'dnd'));
        expect(msg.trim(), isNotEmpty);
      }
      expect(describeApiException(tr, ApiException(ApiErrorKind.server, 'dnd')), isNot('dnd'));
    }
    final en = Tr.of(const AppSettings(hindi: false));
    final hi = Tr.of(const AppSettings(hindi: true));
    expect(describeApiException(en, ApiException(ApiErrorKind.auth)), isNot(describeApiException(hi, ApiException(ApiErrorKind.auth))));
  });
}
