import 'dart:async';
import 'dart:convert';

import 'package:esp32_controller/esp32_api.dart';
import 'package:esp32_controller/home_controller.dart';
import 'package:esp32_controller/models.dart';
import 'package:esp32_controller/mqtt_models.dart';
import 'package:esp32_controller/mqtt_service.dart';
import 'package:esp32_controller/secure_storage.dart';
import 'package:esp32_controller/settings.dart';
import 'package:esp32_controller/snapshot_cache.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// The local (REST) path, scripted. Records what it was asked to do.
class FakeLocal extends ChangeNotifier implements LocalBoard {
  Esp32State? _state;
  BoardInventory? _inv;
  BoardState? _live;
  bool _online = false;
  final List<String> calls = <String>[];

  void setBoard(Map<String, dynamic> rest, {bool online = true}) {
    _state = esp32StateFromRest(rest);
    _inv = BoardInventory.tryParse(rest['inventory']);
    _live = BoardState.tryParse(rest['state']);
    _online = online;
    notifyListeners();
  }

  @override
  Esp32State? get state => _state;
  @override
  BoardInventory? get inventory => _inv;
  @override
  BoardState? get liveState => _live;
  @override
  bool get online => _online;
  @override
  bool get loaded => true;
  @override
  String? get error => null;
  @override
  bool get hasToken => true;
  @override
  String get boardId => _state?.board ?? '';
  @override
  String? get recoveryCode => null;
  @override
  String? get recoveryHotspot => null;
  @override
  bool isBusy(String key) => false;
  @override
  Future<void> refresh() async => calls.add('refresh');
  @override
  Future<void> reloadToken() async {}
  @override
  Future<String?> setDevice(String id, bool on) async {
    calls.add('set:$id:$on');
    return null;
  }

  @override
  Future<String?> setDnd(bool on) async {
    calls.add('dnd:$on');
    return null;
  }

  @override
  Future<String?> runScene(String id) async {
    calls.add('scene:$id');
    return null;
  }

  @override
  Future<String?> addDevice({required String name, required String icon, required int pin, required String roomId, int? watts}) async {
    calls.add('add:$name:$pin:$roomId');
    return null;
  }

  @override
  Future<String?> updateDevice(String id, {String? name, String? icon, int? pin, String? roomId, int? watts}) async {
    calls.add('update:$id:${name ?? ''}:${pin ?? ''}:${roomId ?? ''}');
    return null;
  }

  @override
  Future<String?> deleteDevice(String id) async {
    calls.add('delete:$id');
    return null;
  }

  @override
  Future<String?> upsertRoom(RoomInfo room) async {
    calls.add('room.upsert:${room.id}:${room.rev}');
    return null;
  }

  @override
  Future<String?> deleteRoom(RoomInfo room) async {
    calls.add('room.delete:${room.id}:${room.rev}');
    return null;
  }

  @override
  Future<ApiResult<WifiScan>> wifiScan({bool rescan = false}) async => const ApiResult<WifiScan>.fail('n/a');
  @override
  Future<ApiResult<WifiStatus>> wifiStatus() async => const ApiResult<WifiStatus>.fail('n/a');
  @override
  Future<String?> wifiConnect(String ssid, String password) async => null;
  @override
  Future<String?> wifiReset() async => null;
}

/// Broker connection stand-in (see mqtt_service_test.dart for the fuller version).
class FakeTransport implements MqttTransport {
  final StreamController<MqttInbound> _messages = StreamController<MqttInbound>.broadcast();
  final StreamController<bool> _connection = StreamController<bool>.broadcast();
  final List<Map<String, dynamic>> commands = <Map<String, dynamic>>[];
  final List<String> commandTopics = <String>[];
  String? refuseWith; // answer every command with this error code

  @override
  Stream<MqttInbound> get messages => _messages.stream;
  @override
  Stream<bool> get connection => _connection.stream;
  @override
  bool get isConnected => true;
  @override
  Future<void> connect(CloudConfig config, String clientId) async {}
  @override
  Future<void> subscribe(String topic) async {}
  @override
  Future<void> disconnect() async {}

  @override
  Future<void> publish(String topic, String payload) async {
    final body = jsonDecode(payload) as Map<String, dynamic>;
    commands.add(body);
    commandTopics.add(topic);
    final board = topic.split('/')[3];
    scheduleMicrotask(() => inject('sh/v1/myhome/$board/ack', {
          'v': 1,
          'id': body['id'],
          'ok': refuseWith == null,
          if (refuseWith != null) 'err': refuseWith,
        }));
  }

  void inject(String topic, Object payload) =>
      _messages.add(MqttInbound(topic, payload is String ? payload : jsonEncode(payload)));
}

const CloudConfig cloud = CloudConfig(host: 'abc123.s1.eu.hivemq.cloud', username: 'u', password: 'p', homeId: 'myhome');

Map<String, dynamic> inventory(
  String board,
  String name, {
  List<Map<String, dynamic>> rooms = const [
    {'id': 'room1', 'name': 'Hall', 'rev': 1},
  ],
  List<Map<String, dynamic>>? devices,
}) =>
    {
      'v': 1,
      'board': board,
      'name': name,
      'fw': '4.0.0',
      'chip': 'esp32',
      'rooms': rooms,
      'devices': devices ??
          [
            {'id': 'light', 'name': '$name light', 'type': 'switch', 'icon': 'light', 'room': 'room1', 'watts': 10, 'remote': true},
          ],
    };

Map<String, dynamic> liveState({bool light = false, double power = 0}) => {
      'v': 1,
      'powerW': power,
      'd': {
        'light': {'on': light},
        'door': {'on': true},
      },
    };

Future<void> pump() => Future<void>.delayed(const Duration(milliseconds: 10));

void main() {
  late SettingsController settings;
  late FakeTransport transport;
  late FakeLocal local;
  late MqttService mqtt;
  late HomeController home;
  late DateTime now;

  Future<void> publishBoard(String board, String name,
      {List<Map<String, dynamic>>? rooms, List<Map<String, dynamic>>? devices, bool light = false, double power = 0}) async {
    transport.inject('sh/v1/myhome/$board/inventory',
        inventory(board, name, devices: devices, rooms: rooms ?? const [{'id': 'room1', 'name': 'Hall', 'rev': 1}]));
    transport.inject('sh/v1/myhome/$board/state', liveState(light: light, power: power));
    transport.inject('sh/v1/myhome/$board/lwt', 'online');
    await pump();
  }

  Map<String, dynamic> localRest(String board, String name, {bool light = false}) => {
        'inventory': inventory(board, name),
        'state': liveState(light: light),
        'wifi': {'mode': 'station', 'ssid': 'Home'},
      };

  setUp(() async {
    SharedPreferences.setMockInitialValues(<String, Object>{});
    settings = await SettingsController.load();
    transport = FakeTransport();
    local = FakeLocal();
    now = DateTime(2026, 10, 23, 12);
    mqtt = MqttService(
      transportFactory: () => transport,
      clock: () => now,
      ackTimeout: const Duration(milliseconds: 100),
      delayFor: (_) => const Duration(milliseconds: 20),
    );
    await mqtt.configure(cloud);
    home = HomeController(settings: settings, mqtt: mqtt, local: local, clock: () => now);
  });

  tearDown(() {
    home.dispose();
    mqtt.dispose();
  });

  group('one dashboard for several boards', () {
    test('devices of every board are listed with board-scoped ids, rooms are merged', () async {
      await publishBoard('sh-aaaaaa', 'Hall', rooms: [
        {'id': 'room1', 'name': 'Hall', 'rev': 1},
        {'id': 'room2', 'name': 'Kitchen', 'rev': 1},
      ]);
      await publishBoard('sh-bbbbbb', 'Bedroom', rooms: [
        {'id': 'room1', 'name': 'Hall', 'rev': 1},
        {'id': 'room3', 'name': 'Bedroom', 'rev': 1},
      ]);
      final st = home.state!;
      expect(st.devices.map((d) => d.id), ['sh-bbbbbb/light', 'sh-aaaaaa/light']); // boards ordered by name
      expect(home.roomNames, ['Hall', 'Kitchen', 'Bedroom']);
      expect(home.roomIds, ['room1', 'room2', 'room3']);
      expect(home.boards.map((b) => b.id).toSet(), {'sh-aaaaaa', 'sh-bbbbbb'});
      expect(home.boards.every((b) => b.available && b.source == BoardSource.cloud), isTrue);
      expect(home.online, isTrue);
    });

    test('power is summed over boards that are actually reachable', () async {
      await publishBoard('sh-aaaaaa', 'Hall', power: 12);
      await publishBoard('sh-bbbbbb', 'Bedroom', power: 60);
      expect(home.state!.powerW, 72);
      transport.inject('sh/v1/myhome/sh-bbbbbb/lwt', 'offline');
      await pump();
      expect(home.state!.powerW, 12);
    });

    test('a board that announced it went offline stays listed but its devices are unavailable', () async {
      await publishBoard('sh-aaaaaa', 'Hall');
      transport.inject('sh/v1/myhome/sh-aaaaaa/lwt', 'offline');
      await pump();
      final d = home.state!.devices.single;
      expect(d.available, isFalse);
      expect(home.boards.single.available, isFalse);
      expect(home.boards.single.source, BoardSource.none);
      expect(await home.setDevice(d.id, true), isNotNull); // refused up front, nothing published
      expect(transport.commands, isEmpty);
    });

    test('a board that went silent is detected without any last-will message', () async {
      await publishBoard('sh-aaaaaa', 'Hall');
      expect(home.state!.devices.single.available, isTrue);
      now = now.add(const Duration(seconds: 200)); // > two missed 60 s heartbeats
      transport.inject('sh/v1/myhome/sh-aaaaaa/lwt', 'online'); // any message makes the controller re-evaluate
      await pump();
      expect(home.state!.devices.single.available, isFalse);
    });

    test('the local connection wins for the board it reaches; the others use the cloud', () async {
      await publishBoard('sh-aaaaaa', 'Hall');
      await publishBoard('sh-bbbbbb', 'Bedroom');
      local.setBoard(localRest('sh-aaaaaa', 'Hall'));
      expect(home.boards.firstWhere((b) => b.id == 'sh-aaaaaa').source, BoardSource.local);
      expect(home.boards.firstWhere((b) => b.id == 'sh-bbbbbb').source, BoardSource.cloud);

      expect(await home.setDevice('sh-aaaaaa/light', true), isNull);
      expect(local.calls, contains('set:sh-aaaaaa/light:true'));
      expect(transport.commands, isEmpty); // not sent through the broker

      expect(await home.setDevice('sh-bbbbbb/light', true), isNull);
      expect(transport.commands.single['op'], 'set');
      expect(transport.commands.single['dev'], 'light'); // the board's own id, not the dashboard id
      expect(transport.commandTopics.single, 'sh/v1/myhome/sh-bbbbbb/cmd');
    });

    test('without internet the dashboard still works from the local board alone', () async {
      local.setBoard(localRest('sh-aaaaaa', 'Hall', light: true));
      expect(home.state!.devices.single.on, isTrue);
      expect(home.online, isTrue);
      local.setBoard(localRest('sh-aaaaaa', 'Hall'), online: false);
      expect(home.online, isFalse);
      expect(home.state!.devices.single.available, isFalse); // known, but not shown as live
    });
  });

  group('commands over the cloud', () {
    test('the tile changes at once and the overlay yields to the next real state from the board', () async {
      await publishBoard('sh-aaaaaa', 'Hall');
      expect(home.state!.devices.single.on, isFalse);
      expect(await home.setDevice('sh-aaaaaa/light', true), isNull);
      expect(home.state!.devices.single.on, isTrue); // optimistic, acknowledged
      now = now.add(const Duration(seconds: 1));
      transport.inject('sh/v1/myhome/sh-aaaaaa/state', liveState(light: false)); // the board says: still off
      await pump();
      expect(home.state!.devices.single.on, isFalse); // reality wins
    });

    test('a refusal rolls the tile back and explains why', () async {
      await publishBoard('sh-aaaaaa', 'Hall');
      transport.refuseWith = 'dnd';
      final err = await home.setDevice('sh-aaaaaa/light', true);
      expect(err, isNotNull);
      expect(err, isNot('dnd')); // a sentence, not the raw code
      expect(home.state!.devices.single.on, isFalse);
    });

    test('devices that are not cloud-enabled are refused locally; a lock can always be locked but not unlocked', () async {
      await publishBoard('sh-aaaaaa', 'Hall', devices: [
        {'id': 'fan', 'name': 'Fan', 'type': 'switch', 'icon': 'fan', 'room': 'room1', 'remote': false},
        {'id': 'door', 'name': 'Door', 'type': 'lock', 'icon': 'lock', 'room': 'room1', 'remote': false},
      ]);
      expect(await home.setDevice('sh-aaaaaa/fan', true), isNotNull);
      expect(await home.setDevice('sh-aaaaaa/door', false), isNotNull); // unlock: refused
      expect(transport.commands, isEmpty);
      expect(await home.setDevice('sh-aaaaaa/door', true), isNull); // lock: allowed
      expect(transport.commands.single['dev'], 'door');
      expect(transport.commands.single['on'], true);
    });

    test('routines run on every reachable board', () async {
      await publishBoard('sh-aaaaaa', 'Hall');
      await publishBoard('sh-bbbbbb', 'Bedroom');
      expect(await home.runScene('goodnight'), isNull);
      expect(transport.commands.map((c) => c['op']), ['scene', 'scene']);
      expect(transport.commandTopics.toSet(), {'sh/v1/myhome/sh-aaaaaa/cmd', 'sh/v1/myhome/sh-bbbbbb/cmd'});
    });
  });

  group('hardware changes are local only', () {
    test('adding, rewiring and deleting need the board on the local network', () async {
      await publishBoard('sh-aaaaaa', 'Hall');
      expect(await home.addDevice(name: 'Lamp', icon: 'light', pin: 23, room: 0), isNotNull);
      expect(await home.updateDevice('sh-aaaaaa/light', pin: 22), isNotNull);
      expect(await home.updateDevice('sh-aaaaaa/light', watts: 40), isNotNull);
      expect(await home.deleteDevice('sh-aaaaaa/light'), isNotNull);
      expect(transport.commands, isEmpty);
      expect(local.calls, isEmpty);
    });

    test('names, icons and rooms may be changed through the cloud', () async {
      await publishBoard('sh-aaaaaa', 'Hall');
      expect(await home.updateDevice('sh-aaaaaa/light', name: 'Reading lamp', room: 0), isNull);
      final c = transport.commands.single;
      expect(c['op'], 'device.update');
      expect(c['dev'], 'light');
      expect(c['name'], 'Reading lamp');
      expect(c['room'], 'room1'); // translated from the tab index to the room id
      expect(c.containsKey('pin'), isFalse);
    });

    test('with the local connection up the same operations go straight to the board', () async {
      await publishBoard('sh-aaaaaa', 'Hall');
      local.setBoard(localRest('sh-aaaaaa', 'Hall'));
      expect(await home.addDevice(name: 'Lamp', icon: 'light', pin: 23, room: 0), isNull);
      expect(await home.updateDevice('sh-aaaaaa/light', pin: 22), isNull);
      expect(await home.deleteDevice('sh-aaaaaa/light'), isNull);
      expect(local.calls, ['add:Lamp:23:room1', 'update:sh-aaaaaa/light::22:', 'delete:sh-aaaaaa/light']);
      expect(transport.commands, isEmpty);
    });
  });

  group('rooms', () {
    test('a new room goes to every reachable board with revision 1 and a creation-ordered id', () async {
      await publishBoard('sh-aaaaaa', 'Hall');
      await publishBoard('sh-bbbbbb', 'Bedroom');
      expect(await home.addRoom('  Study '), isNull);
      expect(transport.commands.length, 2);
      for (final c in transport.commands) {
        expect(c['op'], 'room.upsert');
        final r = c['room'] as Map<String, dynamic>;
        expect(r['name'], 'Study');
        expect(r['rev'], 1);
        expect(r['id'], matches(RegExp(r'^r[a-z0-9]{8,12}$')));
        expect(r['id'], isNot(startsWith('room'))); // never collides with migrated "roomN" ids
      }
    });

    test('rename raises the revision; delete sends a tombstone one revision higher', () async {
      await publishBoard('sh-aaaaaa', 'Hall', rooms: [
        {'id': 'room1', 'name': 'Hall', 'rev': 4},
        {'id': 'room2', 'name': 'Kitchen', 'rev': 2},
      ]);
      expect(await home.renameRoom(0, 'Lobby'), isNull);
      final rename = transport.commands.last['room'] as Map<String, dynamic>;
      expect([rename['id'], rename['name'], rename['rev']], ['room1', 'Lobby', 5]);
      expect(await home.deleteRoom(1), isNull);
      expect(transport.commands.last['op'], 'room.delete');
      final del = transport.commands.last['room'] as Map<String, dynamic>;
      expect([del['id'], del['rev']], ['room2', 3]);
    });

    test('a board that missed an edit is brought up to date once, not repeatedly', () async {
      await publishBoard('sh-aaaaaa', 'Hall', rooms: [
        {'id': 'room1', 'name': 'Hall', 'rev': 1},
        {'id': 'den', 'name': 'Den', 'rev': 2},
      ]);
      await publishBoard('sh-bbbbbb', 'Bedroom', rooms: [
        {'id': 'room1', 'name': 'Hall', 'rev': 1},
        {'id': 'den', 'name': 'Old den', 'rev': 1},
      ]);
      await pump();
      final repairs = transport.commands.where((c) => c['op'] == 'room.upsert').toList();
      expect(transport.commandTopics.where((t) => t.contains('sh-bbbbbb')).length, repairs.length);
      expect(repairs.length, greaterThanOrEqualTo(1));
      expect((repairs.first['room'] as Map)['name'], 'Den');
      expect((repairs.first['room'] as Map)['rev'], 2);
      final before = transport.commands.length;
      transport.inject('sh/v1/myhome/sh-bbbbbb/state', liveState()); // another update arrives within the minute
      await pump();
      expect(transport.commands.length, before);
    });
  });

  group('start-up cache (instant dashboard)', () {
    late SnapshotCache cache;
    late SharedPreferences prefs;

    HomeController withCache() {
      final h = HomeController(
        settings: settings,
        mqtt: mqtt,
        local: local,
        cache: cache,
        clock: () => now,
        cacheDelay: const Duration(milliseconds: 20),
      );
      addTearDown(h.dispose);
      return h;
    }

    setUp(() async {
      SharedPreferences.setMockInitialValues(<String, Object>{});
      prefs = await SharedPreferences.getInstance();
      cache = SnapshotCache(prefs);
    });

    test('the remembered home is on screen immediately, marked as not live, and can not be switched', () async {
      await cache.save({
        'sh-aaaaaa': CachedBoard(
          inventory: BoardInventory.tryParse(inventory('sh-aaaaaa', 'Hall'))!,
          state: BoardState.tryParse(liveState(light: true)),
          savedAt: now,
        ),
      });
      final h = withCache();
      expect(h.loaded, isTrue); // no spinner
      final d = h.state!.devices.single;
      expect(d.name, 'Hall light');
      expect(d.on, isTrue); // the last known value ...
      expect(d.available, isFalse); // ... but never presented as live
      expect(h.online, isFalse);
      expect(h.boards.single.source, BoardSource.none);
      expect(await h.setDevice(d.id, false), isNotNull);
      expect(transport.commands, isEmpty);
    });

    test('live data from the broker takes over from the remembered state', () async {
      await cache.save({
        'sh-aaaaaa': CachedBoard(
          inventory: BoardInventory.tryParse(inventory('sh-aaaaaa', 'Hall'))!,
          state: BoardState.tryParse(liveState(light: true)),
          savedAt: now,
        ),
      });
      final h = withCache();
      await publishBoard('sh-aaaaaa', 'Hall renamed', light: false);
      final d = h.state!.devices.single;
      expect(d.name, 'Hall renamed light');
      expect(d.on, isFalse);
      expect(d.available, isTrue);
      expect(h.boards.single.source, BoardSource.cloud);
    });

    test('what boards report is saved once it settles - and not again for a mere heartbeat', () async {
      final h = withCache();
      await publishBoard('sh-aaaaaa', 'Hall');
      await Future<void>.delayed(const Duration(milliseconds: 80));
      final first = prefs.getString('snapshot_cache_v1');
      expect(first, isNotNull);
      expect(cache.load(now: now).keys, ['sh-aaaaaa']);

      now = now.add(const Duration(minutes: 1));
      transport.inject('sh/v1/myhome/sh-aaaaaa/state', {...liveState(), 'seq': 99, 'up': 5000, 'rssi': -70}); // heartbeat only
      await Future<void>.delayed(const Duration(milliseconds: 80));
      expect(prefs.getString('snapshot_cache_v1'), first); // unchanged: nothing was written

      transport.inject('sh/v1/myhome/sh-aaaaaa/state', liveState(light: true)); // a real change
      await Future<void>.delayed(const Duration(milliseconds: 80));
      expect(prefs.getString('snapshot_cache_v1'), isNot(first));
      expect(cache.load(now: now)['sh-aaaaaa']!.state!.devices['light']!.on, isTrue);
      expect(h.state!.devices.single.on, isTrue);
    });

    test('a removed board can be forgotten', () async {
      await cache.save({
        'sh-aaaaaa': CachedBoard(inventory: BoardInventory.tryParse(inventory('sh-aaaaaa', 'Hall'))!, state: null, savedAt: now),
      });
      final h = withCache();
      expect(h.boards.length, 1);
      await h.forgetCached('sh-aaaaaa');
      expect(h.boards, isEmpty);
      expect(h.state, isNull);
      expect(cache.load(now: now), isEmpty);
    });
  });
}
