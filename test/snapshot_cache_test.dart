import 'dart:convert';

import 'package:esp32_controller/mqtt_models.dart';
import 'package:esp32_controller/snapshot_cache.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

Map<String, dynamic> inventoryJson(String board, {String name = 'Hall'}) => {
      'v': 1,
      'board': board,
      'name': name,
      'fw': '4.0.0',
      'chip': 'esp32-s3',
      'psram': true,
      'cfgRev': 7,
      'maxDevices': 24,
      'rooms': [
        {'id': 'room1', 'name': 'Hall', 'rev': 2},
        {'id': 'old', 'name': 'Old', 'rev': 5, 'deleted': true},
      ],
      'devices': [
        {'id': 'light', 'name': 'Light', 'type': 'switch', 'group': 'Lighting', 'icon': 'light', 'room': 'room1', 'watts': 12, 'enabled': true, 'remote': true},
        {'id': 'fan', 'name': 'Fan', 'type': 'switch', 'icon': 'fan', 'room': 'room1', 'remote': false, 'fault': 'flash', 'pin': 6},
      ],
      'scenes': [
        {'id': 'alloff', 'name': 'All off'},
      ],
      'counts': {'schedules': 2, 'automations': 1},
      'pins': {
        'out': [13, 22],
        'in': [34],
      },
    };

Map<String, dynamic> stateJson() => {
      'v': 1,
      'seq': 9,
      'ts': 1700000000,
      'up': 300,
      'rssi': -60,
      'ip': '192.168.1.5',
      'dnd': true,
      'temp': 24.5,
      'powerW': 12.0,
      'cloud': 'connected',
      'd': {
        'light': {'on': true, 'wh': 1.5},
        'fan': {'on': false, 'wh': 0},
      },
    };

void main() {
  group('models can be written back in the firmware format', () {
    test('inventory round trip keeps rooms (incl. tombstones), devices, scenes, counts and pins', () {
      final inv = BoardInventory.tryParse(inventoryJson('sh-aabbcc'))!;
      final back = BoardInventory.tryParse(jsonDecode(jsonEncode(inv.toJson())))!;
      expect(back.board, 'sh-aabbcc');
      expect(back.chip, 'esp32-s3');
      expect(back.psram, isTrue);
      expect(back.cfgRev, 7);
      expect(back.rooms, inv.rooms);
      expect(back.rooms[1].deleted, isTrue);
      expect(back.devices.map((d) => d.id), ['light', 'fan']);
      expect(back.devices[1].fault, 'flash');
      expect(back.devices[1].remote, isFalse);
      expect(back.devices[1].pin, 6);
      expect(back.devices[0].watts, 12);
      expect(back.scenes.single.id, 'alloff');
      expect(back.schedules, 2);
      expect(back.automations, 1);
      expect(back.pinsOut, [13, 22]);
      expect(back.pinsIn, [34]);
    });

    test('state round trip keeps live values and optional sensors', () {
      final st = BoardState.tryParse(stateJson())!;
      final back = BoardState.tryParse(jsonDecode(jsonEncode(st.toJson())))!;
      expect(back.devices['light']!.on, isTrue);
      expect(back.devices['light']!.wh, 1.5);
      expect(back.devices['fan']!.on, isFalse);
      expect(back.temp, 24.5);
      expect(back.hum, isNull);
      expect(back.dnd, isTrue);
      expect(back.powerW, 12.0);
    });
  });

  group('SnapshotCache', () {
    late SharedPreferences prefs;
    late SnapshotCache cache;
    final now = DateTime(2026, 10, 26, 12);

    CachedBoard board(String id, {DateTime? at}) => CachedBoard(
          inventory: BoardInventory.tryParse(inventoryJson(id))!,
          state: BoardState.tryParse(stateJson()),
          savedAt: at ?? now,
        );

    setUp(() async {
      SharedPreferences.setMockInitialValues(<String, Object>{});
      prefs = await SharedPreferences.getInstance();
      cache = SnapshotCache(prefs);
    });

    test('an empty cache loads as empty', () => expect(cache.load(now: now), isEmpty));

    test('save then load returns the boards', () async {
      await cache.save({'sh-aaaaaa': board('sh-aaaaaa'), 'sh-bbbbbb': board('sh-bbbbbb')});
      final loaded = SnapshotCache(prefs).load(now: now);
      expect(loaded.keys.toSet(), {'sh-aaaaaa', 'sh-bbbbbb'});
      expect(loaded['sh-aaaaaa']!.inventory.devices.length, 2);
      expect(loaded['sh-aaaaaa']!.state!.devices['light']!.on, isTrue);
      expect(loaded['sh-aaaaaa']!.savedAt, now);
    });

    test('a damaged cache, an old entry and an entry for the wrong board are ignored', () async {
      await prefs.setString('snapshot_cache_v1', '{not json');
      expect(cache.load(now: now), isEmpty);
      await prefs.setString('snapshot_cache_v1', jsonEncode({'boards': 'nope'}));
      expect(cache.load(now: now), isEmpty);

      await cache.save({
        'sh-aaaaaa': board('sh-aaaaaa'),
        'sh-old000': board('sh-old000', at: now.subtract(const Duration(days: 31))),
        'sh-wrong0': board('sh-other0'), // stored under another board's key
      });
      expect(cache.load(now: now).keys, ['sh-aaaaaa']);
    });

    test('an entry of another protocol version is ignored', () async {
      final doc = {
        'boards': {
          'sh-aaaaaa': {
            'savedAt': now.millisecondsSinceEpoch,
            'inventory': {...inventoryJson('sh-aaaaaa'), 'v': 2},
          },
        },
      };
      await prefs.setString('snapshot_cache_v1', jsonEncode(doc));
      expect(cache.load(now: now), isEmpty);
    });

    test('no more than the allowed number of boards is kept', () async {
      final many = {for (var i = 0; i < 30; i++) 'sh-${i.toString().padLeft(6, '0')}': board('sh-${i.toString().padLeft(6, '0')}')};
      await cache.save(many);
      expect(cache.load(now: now).length, SnapshotCache.maxBoards);
    });

    test('the cache never contains credentials: only home layout and states', () async {
      await cache.save({'sh-aaaaaa': board('sh-aaaaaa')});
      final raw = prefs.getString('snapshot_cache_v1')!.toLowerCase();
      for (final word in ['password', 'token', 'secret', 'authorization']) {
        expect(raw.contains(word), isFalse, reason: word);
      }
    });

    test('clear removes everything', () async {
      await cache.save({'sh-aaaaaa': board('sh-aaaaaa')});
      await cache.clear();
      expect(cache.load(now: now), isEmpty);
    });
  });
}
