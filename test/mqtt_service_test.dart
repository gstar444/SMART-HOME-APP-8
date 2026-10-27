import 'dart:async';
import 'dart:convert';

import 'package:esp32_controller/mqtt_service.dart';
import 'package:esp32_controller/secure_storage.dart';
import 'package:flutter_test/flutter_test.dart';

/// Scripted stand-in for the broker connection.
class FakeTransport implements MqttTransport {
  final StreamController<MqttInbound> _messages = StreamController<MqttInbound>.broadcast();
  final StreamController<bool> _connection = StreamController<bool>.broadcast();
  bool up = false;
  bool disconnectCalled = false;
  String? failWith;
  String? clientId;
  final List<String> subscribed = <String>[];
  final List<({String topic, String payload})> published = <({String topic, String payload})>[];

  /// Called for every publish; lets a test play the board.
  void Function(FakeTransport t, String topic, Map<String, dynamic> body)? responder;

  @override
  Stream<MqttInbound> get messages => _messages.stream;
  @override
  Stream<bool> get connection => _connection.stream;
  @override
  bool get isConnected => up;

  @override
  Future<void> connect(CloudConfig config, String clientId) async {
    this.clientId = clientId;
    if (failWith != null) throw MqttTransportException(failWith!);
    up = true;
  }

  @override
  Future<void> subscribe(String topic) async => subscribed.add(topic);

  @override
  Future<void> publish(String topic, String payload) async {
    published.add((topic: topic, payload: payload));
    final r = responder;
    if (r != null) {
      final body = jsonDecode(payload) as Map<String, dynamic>;
      scheduleMicrotask(() => r(this, topic, body));
    }
  }

  @override
  Future<void> disconnect() async {
    up = false;
    disconnectCalled = true;
  }

  void inject(String topic, Object payload) =>
      _messages.add(MqttInbound(topic, payload is String ? payload : jsonEncode(payload)));
  void ack(String board, Map<String, dynamic> body) => inject('sh/v1/myhome/$board/ack', {'v': 1, ...body});
  void drop() {
    up = false;
    _connection.add(false);
  }
}

const CloudConfig cfg = CloudConfig(
  host: 'abc123.s1.eu.hivemq.cloud',
  username: 'app-phone',
  password: 'pw',
  homeId: 'myhome',
);

Map<String, dynamic> inventory(String board, {String name = 'Hall'}) => {
      'v': 1,
      'board': board,
      'name': name,
      'fw': '4.0.0',
      'chip': 'esp32',
      'rooms': [
        {'id': 'room1', 'name': 'Hall', 'rev': 1},
      ],
      'devices': [
        {'id': 'light', 'name': 'Light', 'type': 'switch', 'icon': 'light', 'room': 'room1'},
      ],
    };

Future<void> pump() => Future<void>.delayed(const Duration(milliseconds: 5));

Future<void> waitFor(bool Function() condition, {Duration timeout = const Duration(seconds: 2)}) async {
  final end = DateTime.now().add(timeout);
  while (!condition()) {
    if (DateTime.now().isAfter(end)) fail('condition not reached in time');
    await Future<void>.delayed(const Duration(milliseconds: 5));
  }
}

void main() {
  late List<FakeTransport> fakes;
  late int failFirst;
  late MqttService mqtt;

  MqttService build({Duration ackTimeout = const Duration(milliseconds: 60)}) => MqttService(
        transportFactory: () {
          final t = FakeTransport();
          if (failFirst > 0) {
            failFirst--;
            t.failWith = 'The broker is unreachable.';
          }
          fakes.add(t);
          return t;
        },
        ackTimeout: ackTimeout,
        delayFor: (_) => const Duration(milliseconds: 15),
      );

  setUp(() {
    fakes = <FakeTransport>[];
    failFirst = 0;
    mqtt = build();
  });

  tearDown(() => mqtt.dispose());

  group('session', () {
    test('connects, subscribes to this home only, and never to cmd', () async {
      await mqtt.configure(cfg);
      expect(mqtt.phase, MqttPhase.connected);
      expect(fakes.single.clientId, startsWith('app-session-'));
      expect(fakes.single.subscribed, [
        'sh/v1/myhome/+/state',
        'sh/v1/myhome/+/inventory',
        'sh/v1/myhome/+/lwt',
        'sh/v1/myhome/+/ack',
      ]);
    });

    test('an incomplete configuration or null keeps the service disabled and never connects', () async {
      await mqtt.configure(null);
      expect(mqtt.phase, MqttPhase.disabled);
      await mqtt.configure(cfg.copyWith(port: 1883)); // plaintext port for a cloud provider: refused
      expect(mqtt.phase, MqttPhase.disabled);
      expect(fakes, isEmpty);
    });

    test('a failed connection reports the reason and retries with backoff until it works', () async {
      failFirst = 2;
      await mqtt.configure(cfg);
      expect(mqtt.phase, MqttPhase.failed);
      expect(mqtt.error, 'The broker is unreachable.');
      await waitFor(() => mqtt.phase == MqttPhase.connected);
      expect(fakes.length, 3);
      expect(mqtt.error, isNull);
    });

    test('a lost connection is noticed and re-established, subscriptions included', () async {
      await mqtt.configure(cfg);
      fakes.first.drop();
      await waitFor(() => mqtt.phase == MqttPhase.failed || fakes.length > 1);
      await waitFor(() => mqtt.phase == MqttPhase.connected && fakes.length == 2);
      expect(fakes[1].subscribed.length, 4);
      expect(fakes[0].disconnectCalled, isTrue);
    });

    test('configure(null) disconnects and forgets the boards', () async {
      await mqtt.configure(cfg);
      fakes.single.inject('sh/v1/myhome/sh-aabbcc/inventory', inventory('sh-aabbcc'));
      await pump();
      expect(mqtt.boards, isNotEmpty);
      await mqtt.configure(null);
      expect(mqtt.phase, MqttPhase.disabled);
      expect(mqtt.boards, isEmpty);
      expect(fakes.single.disconnectCalled, isTrue);
    });

    test('test() checks a configuration on its own connection and reports problems', () async {
      expect(await mqtt.test(cfg), isNull);
      expect(fakes.single.clientId, startsWith('app-test-'));
      expect(fakes.single.disconnectCalled, isTrue);
      expect(mqtt.phase, MqttPhase.disabled); // a test does not start the real session
      failFirst = 1;
      expect(await mqtt.test(cfg), 'The broker is unreachable.');
      expect(await mqtt.test(cfg.copyWith(host: '10.0.0.1')), isNotNull); // validation message, no connection made
      expect(fakes.length, 2);
    });
  });

  group('incoming data', () {
    setUp(() async => mqtt.configure(cfg));

    test('retained inventory, state and last-will are mirrored per board', () async {
      final t = fakes.single;
      t.inject('sh/v1/myhome/sh-aabbcc/inventory', inventory('sh-aabbcc'));
      t.inject('sh/v1/myhome/sh-aabbcc/state', {
        'v': 1,
        'd': {
          'light': {'on': true},
        },
      });
      t.inject('sh/v1/myhome/sh-aabbcc/lwt', 'online');
      await pump();
      final b = mqtt.boards['sh-aabbcc']!;
      expect(b.inventory!.name, 'Hall');
      expect(b.state!.devices['light']!.on, isTrue);
      expect(b.lwtOnline, isTrue);
      expect(b.isAvailable(DateTime.now()), isTrue);
      t.inject('sh/v1/myhome/sh-aabbcc/lwt', 'offline\n'); // the broker's Last Will
      await pump();
      expect(mqtt.boards['sh-aabbcc']!.lwtOnline, isFalse);
      expect(mqtt.boards['sh-aabbcc']!.isAvailable(DateTime.now()), isFalse);
    });

    test('messages for another home, junk payloads and wrong versions change nothing', () async {
      final t = fakes.single;
      t.inject('sh/v1/otherhome/sh-aabbcc/state', {'v': 1});
      t.inject('sh/v1/myhome/sh-aabbcc/state', 'not json at all');
      t.inject('sh/v1/myhome/sh-aabbcc/state', {'v': 2});
      t.inject('sh/v1/myhome/sh-aabbcc/lwt', 'maybe');
      t.inject('sh/v1/myhome/bad board/state', {'v': 1});
      t.inject('sh/v1/myhome/sh-aabbcc/cmd', {'v': 1, 'op': 'set'});
      await pump();
      expect(mqtt.boards, isEmpty);
    });

    test('an inventory must describe the board whose topic it arrived on', () async {
      fakes.single.inject('sh/v1/myhome/sh-aaaaaa/inventory', inventory('sh-bbbbbb'));
      await pump();
      expect(mqtt.boards, isEmpty);
    });

    test('oversized payloads are ignored', () async {
      fakes.single.inject('sh/v1/myhome/sh-aabbcc/inventory', 'x' * (MqttService.maxPayloadBytes + 1));
      await pump();
      expect(mqtt.boards, isEmpty);
    });

    test('a flood of unknown boards can not grow memory without bound', () async {
      for (var i = 0; i < 60; i++) {
        fakes.single.inject('sh/v1/myhome/sh-${i.toString().padLeft(6, '0')}/state', {'v': 1});
      }
      await pump();
      expect(mqtt.boards.length, MqttService.maxBoards);
    });
  });

  group('commands', () {
    setUp(() async => mqtt.configure(cfg));

    test('a command carries version, a valid id and the body, and resolves with the acknowledgement', () async {
      final t = fakes.single;
      t.responder = (t, topic, body) => t.ack('sh-aabbcc', {'id': body['id'], 'ok': true});
      final ack = await mqtt.send('sh-aabbcc', {'op': 'set', 'dev': 'light', 'on': true});
      expect(ack.ok, isTrue);
      final sent = jsonDecode(t.published.single.payload) as Map<String, dynamic>;
      expect(t.published.single.topic, 'sh/v1/myhome/sh-aabbcc/cmd');
      expect(sent['v'], 1);
      expect(sent['op'], 'set');
      expect(sent['dev'], 'light');
      expect(sent['on'], true);
      expect(sent['id'], matches(RegExp(r'^[A-Za-z0-9_-]{1,24}$'))); // what the firmware accepts
    });

    test('a refusal from the board is returned as is', () async {
      fakes.single.responder = (t, topic, body) => t.ack('sh-aabbcc', {'id': body['id'], 'ok': false, 'err': 'dnd'});
      final ack = await mqtt.send('sh-aabbcc', {'op': 'set', 'dev': 'siren', 'on': true});
      expect(ack.ok, isFalse);
      expect(ack.error, 'dnd');
    });

    test('a missing acknowledgement is retried ONCE with the same id (the board runs an id at most once)', () async {
      var calls = 0;
      fakes.single.responder = (t, topic, body) {
        calls++;
        if (calls == 2) t.ack('sh-aabbcc', {'id': body['id'], 'ok': true, 'dup': true});
      };
      final ack = await mqtt.send('sh-aabbcc', {'op': 'set', 'dev': 'light', 'on': true});
      expect(ack.ok, isTrue);
      expect(fakes.single.published.length, 2);
      final ids = fakes.single.published.map((p) => (jsonDecode(p.payload) as Map)['id']).toSet();
      expect(ids.length, 1);
    });

    test('no acknowledgement at all ends in a timeout after exactly two attempts', () async {
      final ack = await mqtt.send('sh-aabbcc', {'op': 'ping'});
      expect(ack.ok, isFalse);
      expect(ack.error, 'timeout');
      expect(fakes.single.published.length, 2);
    });

    test('an acknowledgement that arrives from a different board is ignored', () async {
      fakes.single.responder = (t, topic, body) => t.ack('sh-ffffff', {'id': body['id'], 'ok': true});
      final ack = await mqtt.send('sh-aabbcc', {'op': 'ping'});
      expect(ack.error, 'timeout');
    });

    test('sending while disconnected, or to an invalid board id, fails fast without publishing', () async {
      expect((await mqtt.send('bad/id', {'op': 'ping'})).error, 'not_connected');
      await mqtt.configure(null);
      expect((await mqtt.send('sh-aabbcc', {'op': 'ping'})).error, 'not_connected');
      expect(fakes.single.published, isEmpty);
    });

    test('a connection loss completes waiting commands immediately instead of hanging', () async {
      final slow = build(ackTimeout: const Duration(seconds: 5));
      addTearDown(slow.dispose);
      await slow.configure(cfg);
      final pending = slow.send('sh-aabbcc', {'op': 'ping'});
      await pump();
      fakes.last.drop();
      final ack = await pending.timeout(const Duration(seconds: 1));
      expect(ack.ok, isFalse);
      expect(ack.error, 'connection_lost');
    });
  });

  test('reconnect delays double from 5 s to a 5 minute ceiling with at most 20 % jitter', () {
    expect(reconnectDelay(0).inMilliseconds, inInclusiveRange(5000, 6000));
    expect(reconnectDelay(1).inMilliseconds, inInclusiveRange(10000, 12000));
    expect(reconnectDelay(3).inMilliseconds, inInclusiveRange(40000, 48000));
    for (final attempt in [6, 7, 20, 200]) {
      expect(reconnectDelay(attempt).inMilliseconds, inInclusiveRange(300000, 360000));
    }
  });
}
