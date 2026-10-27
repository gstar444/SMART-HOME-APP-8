import 'package:esp32_controller/mqtt_models.dart';
import 'package:flutter_test/flutter_test.dart';

RoomInfo room(String id, String name, int rev, {bool deleted = false}) =>
    RoomInfo(id: id, name: name, rev: rev, deleted: deleted);

/// Order-independent description of a merged room table (a tombstone's name carries no meaning).
String describe(Map<String, RoomInfo> table) {
  final parts = [
    for (final r in table.values) '${r.id}|${r.deleted ? '-' : r.name}|${r.rev}|${r.deleted}',
  ]..sort();
  return parts.join(';');
}

void main() {
  group('topics', () {
    const topics = Topics('myhome');

    test('command topic and wildcard subscriptions follow the protocol', () {
      expect(topics.cmd('sh-aabbcc'), 'sh/v1/myhome/sh-aabbcc/cmd');
      expect(topics.subscriptions, [
        'sh/v1/myhome/+/state',
        'sh/v1/myhome/+/inventory',
        'sh/v1/myhome/+/lwt',
        'sh/v1/myhome/+/ack',
      ]);
      expect(topics.subscriptions.any((t) => t.endsWith('/cmd')), isFalse); // the app never listens to cmd
    });

    test('parse accepts only this home, a valid board id and a known leaf', () {
      expect(topics.parse('sh/v1/myhome/sh-aabbcc/state')?.board, 'sh-aabbcc');
      expect(topics.parse('sh/v1/myhome/sh-aabbcc/state')?.leaf, 'state');
      expect(topics.parse('sh/v1/otherhome/sh-aabbcc/state'), isNull);
      expect(topics.parse('sh/v2/myhome/sh-aabbcc/state'), isNull);
      expect(topics.parse('sh/v1/myhome/sh-aabbcc/state/extra'), isNull);
      expect(topics.parse('sh/v1/myhome/sh-aabbcc/secrets'), isNull);
      expect(topics.parse('sh/v1/myhome/+/state'), isNull);
      expect(topics.parse('sh/v1/myhome/bad board/state'), isNull);
      expect(topics.parse('other/v1/myhome/sh-aabbcc/state'), isNull);
    });

    test('board ids', () {
      expect(validBoardId('sh-a1b2c3'), isTrue);
      expect(validBoardId(''), isFalse);
      expect(validBoardId('a/b'), isFalse);
      expect(validBoardId('x' * 24), isFalse);
    });
  });

  group('payload parsing', () {
    final inventory = <String, dynamic>{
      'v': 1,
      'board': 'sh-aabbcc',
      'name': 'Hall',
      'fw': '4.0.0',
      'chip': 'esp32-s3',
      'psram': true,
      'cfgRev': 7,
      'maxDevices': 24,
      'rooms': [
        {'id': 'room1', 'name': 'Hall', 'rev': 1},
        {'id': 'old', 'name': 'Old', 'rev': 3, 'deleted': true},
        {'id': '', 'name': 'no id', 'rev': 1},
        'junk',
      ],
      'devices': [
        {'id': 'light', 'name': 'Light', 'type': 'switch', 'icon': 'light', 'room': 'room1', 'watts': 12},
        {'name': 'no id'},
        42,
      ],
      'scenes': [
        {'id': 'alloff', 'name': 'All off'},
        {'id': 5},
      ],
      'counts': {'schedules': 2, 'automations': 1},
    };

    test('inventory: valid document, junk entries skipped', () {
      final inv = BoardInventory.tryParse(inventory)!;
      expect(inv.board, 'sh-aabbcc');
      expect(inv.chip, 'esp32-s3');
      expect(inv.psram, isTrue);
      expect(inv.rooms.length, 2);
      expect(inv.rooms[1].deleted, isTrue);
      expect(inv.devices.single.id, 'light');
      expect(inv.devices.single.watts, 12);
      expect(inv.scenes.single.name, 'All off');
      expect(inv.schedules, 2);
      expect(inv.automations, 1);
      expect(inv.pinsOut, isNull); // the cloud copy carries no GPIO details
    });

    test('inventory: wrong version, bad board id and non-objects are rejected', () {
      expect(BoardInventory.tryParse({...inventory, 'v': 2}), isNull);
      expect(BoardInventory.tryParse({...inventory, 'board': 'a/b'}), isNull);
      expect(BoardInventory.tryParse({...inventory, 'board': 7}), isNull);
      expect(BoardInventory.tryParse('text'), isNull);
      expect(BoardInventory.tryParse(null), isNull);
    });

    test('state: live values, optional sensors, estimated power', () {
      final st = BoardState.tryParse({
        'v': 1,
        'seq': 9,
        'ts': 1700000000,
        'up': 300,
        'rssi': -61,
        'ip': '192.168.1.5',
        'dnd': true,
        'temp': 24.5,
        'powerW': 72.0,
        'cloud': 'connected',
        'd': {
          'light': {'on': true, 'wh': 2.25},
          'fan': {'on': false},
          'broken': 'not an object',
        },
      })!;
      expect(st.devices.keys, containsAll(<String>['light', 'fan']));
      expect(st.devices.containsKey('broken'), isFalse);
      expect(st.devices['light']!.on, isTrue);
      expect(st.devices['light']!.wh, 2.25);
      expect(st.temp, 24.5);
      expect(st.hum, isNull);
      expect(st.dnd, isTrue);
      expect(st.powerW, 72.0);
      expect(BoardState.tryParse({'v': 2}), isNull);
      expect(BoardState.tryParse([]), isNull);
    });

    test('acknowledgements', () {
      final ok = CommandAck.tryParse({'v': 1, 'id': 'abc', 'ok': true})!;
      expect(ok.ok, isTrue);
      final bad = CommandAck.tryParse({'v': 1, 'id': 'abc', 'ok': false, 'err': 'dnd', 'dup': true})!;
      expect(bad.ok, isFalse);
      expect(bad.error, 'dnd');
      expect(bad.duplicate, isTrue);
      expect(CommandAck.tryParse({'v': 1, 'ok': true}), isNull);
      expect(CommandAck.tryParse({'v': 3, 'id': 'a', 'ok': true}), isNull);
    });

    test('a board is available only when not announced offline and recently heard from', () {
      final now = DateTime(2026, 10, 23, 12);
      final v = BoardView('sh-aabbcc');
      expect(v.isAvailable(now), isFalse); // never heard
      v.stateAt = now.subtract(const Duration(seconds: 30));
      expect(v.isAvailable(now), isTrue); // fresh state, no last-will message yet
      v.lwtOnline = true;
      expect(v.isAvailable(now), isTrue);
      v.stateAt = now.subtract(const Duration(seconds: 151)); // two missed 60 s heartbeats
      expect(v.isAvailable(now), isFalse);
      v.stateAt = now;
      v.lwtOnline = false; // the broker published the Last Will
      expect(v.isAvailable(now), isFalse);
    });
  });

  group('room merge (same rules as the firmware)', () {
    test('higher revision wins', () {
      expect(roomBeats(room('a', 'X', 2), room('a', 'Y', 1)), isTrue);
      expect(roomBeats(room('a', 'X', 1), room('a', 'Y', 2)), isFalse);
    });

    test('a tombstone beats a live room at the same revision', () {
      expect(roomBeats(room('a', 'x', 4, deleted: true), room('a', 'Revived', 4)), isTrue);
      expect(roomBeats(room('a', 'Revived', 4), room('a', 'x', 4, deleted: true)), isFalse);
      expect(roomBeats(room('a', 'x', 4, deleted: true), room('a', 'y', 4, deleted: true)), isFalse);
    });

    test('at the same revision the greater name wins, compared byte-wise like strcmp', () {
      expect(roomBeats(room('a', 'Kitchen & Dining', 3), room('a', 'Cooking', 3)), isTrue);
      expect(roomBeats(room('a', 'Cooking', 3), room('a', 'Kitchen & Dining', 3)), isFalse);
      // Hindi (3-byte UTF-8, lead byte 0xE0) sorts after ASCII
      expect(roomBeats(room('a', 'रसोई', 3), room('a', 'zebra', 3)), isTrue);
      expect(roomBeats(room('a', 'same', 3), room('a', 'same', 3)), isFalse);
    });

    test('the merged table does not depend on the order boards report in', () {
      final boards = <List<RoomInfo>>[
        [room('kitchen', 'Kitchen', 1), room('den', 'Den', 1)],
        [room('kitchen', 'Kitchen & Dining', 3), room('living', 'Living', 1)],
        [room('kitchen', 'Cooking', 3)],
        [room('kitchen', 'Revived', 4), room('den', 'Den', 2)],
        [room('kitchen', 'gone', 4, deleted: true)],
      ];
      final expected = describe(mergeRoomTable(boards));
      expect(expected, contains('kitchen|-|4|true')); // tombstone wins the rev-4 tie
      expect(expected, contains('den|Den|2|false'));
      // every rotation and the reverse
      for (var i = 0; i < boards.length; i++) {
        final rotated = [...boards.sublist(i), ...boards.sublist(0, i)];
        expect(describe(mergeRoomTable(rotated)), expected);
        expect(describe(mergeRoomTable(rotated.reversed)), expected);
      }
      expect(liveRooms(mergeRoomTable(boards)).map((r) => r.id), ['den', 'living']);
    });

    test('live rooms are ordered by id (creation order for app-made ids)', () {
      final table = mergeRoomTable([
        [room('rzz', 'Last', 1), room('room1', 'First', 1), room('rab', 'Middle', 1)],
      ]);
      expect(liveRooms(table).map((r) => r.id), ['rab', 'room1', 'rzz']);
    });

    test('repairs bring a lagging board up to the merged state, and nothing else', () {
      final merged = mergeRoomTable([
        [room('kitchen', 'gone', 4, deleted: true), room('den', 'Den', 2), room('living', 'Living', 1), room('same', 'Same', 1)],
      ]);
      final lagging = [room('kitchen', 'Kitchen', 3), room('den', 'Den', 1), room('same', 'Same', 1)];
      final ops = roomRepairs(merged, lagging);
      expect(ops.map((o) => '${o.delete ? 'del' : 'set'}:${o.room.id}:${o.room.rev}').toList()..sort(), [
        'del:kitchen:4',
        'set:den:2',
        'set:living:1',
      ]);
      // a board that never had the deleted room needs no tombstone; an up-to-date board needs nothing
      expect(roomRepairs(merged, [room('den', 'Den', 2), room('living', 'Living', 1), room('same', 'Same', 1)]), isEmpty);
      expect(roomRepairs(merged, merged.values.toList()), isEmpty);
    });
  });

  group('composite ids', () {
    test('join and split', () {
      expect(compositeId('sh-aabbcc', 'light'), 'sh-aabbcc/light');
      expect(splitCompositeId('sh-aabbcc/light')?.device, 'light');
      expect(splitCompositeId('sh-aabbcc/light')?.board, 'sh-aabbcc');
      expect(splitCompositeId('light'), isNull);
      expect(splitCompositeId('/light'), isNull);
      expect(splitCompositeId('board/'), isNull);
    });
  });
}
