/// Typed models for the MQTT protocol spoken by firmware 4.x (see docs/PROTOCOL.md).
///
///   sh/v1/`<home>`/`<board>`/cmd        app  -> board   (QoS 1, not retained)
///   sh/v1/`<home>`/`<board>`/ack        board -> app    (reply to a command)
///   sh/v1/`<home>`/`<board>`/state      board -> app    (retained, live values)
///   sh/v1/`<home>`/`<board>`/inventory  board -> app    (retained, rooms/devices/scenes)
///   sh/v1/`<home>`/`<board>`/lwt        board -> app    (retained, "online"/"offline"; broker publishes
///                                                    "offline" as the Last Will if the board vanishes)
///
/// The local REST endpoint GET /api/v2/state returns the SAME inventory and state objects, so every
/// parser here serves both transports.
import 'dart:convert';

import 'models.dart';

const int protocolVersion = 1;
const String topicRoot = 'sh/v1';

final RegExp _boardIdPattern = RegExp(r'^[A-Za-z0-9_-]{1,23}$');
final RegExp _leafPattern = RegExp(r'^(cmd|ack|state|inventory|lwt)$');

bool validBoardId(String id) => _boardIdPattern.hasMatch(id);

/// Builds and parses topic names for one home.
class Topics {
  const Topics(this.homeId);

  final String homeId;

  String _t(String board, String leaf) => '$topicRoot/$homeId/$board/$leaf';

  String cmd(String board) => _t(board, 'cmd');

  /// Wildcard subscriptions of the app (it never subscribes to `cmd`).
  List<String> get subscriptions => [
        for (final leaf in const ['state', 'inventory', 'lwt', 'ack']) '$topicRoot/$homeId/+/$leaf',
      ];

  /// Splits "sh/v1/`<home>`/`<board>`/`<leaf>`" into board and leaf; null for anything else.
  ({String board, String leaf})? parse(String topic) {
    final p = topic.split('/');
    if (p.length != 5 || p[0] != 'sh' || p[1] != 'v1' || p[2] != homeId) return null;
    if (!validBoardId(p[3]) || !_leafPattern.hasMatch(p[4])) return null;
    return (board: p[3], leaf: p[4]);
  }
}

// ---------------------------------------------------------------------------- rooms
class RoomInfo {
  const RoomInfo({required this.id, required this.name, required this.rev, this.deleted = false});

  final String id;
  final String name;
  final int rev;
  final bool deleted;

  static RoomInfo? tryParse(Object? v) {
    if (v is! Map) return null;
    final id = v['id'];
    final name = v['name'];
    final rev = v['rev'];
    if (id is! String || id.isEmpty || name is! String || rev is! num) return null;
    return RoomInfo(id: id, name: name, rev: rev.toInt(), deleted: v['deleted'] == true);
  }

  Map<String, dynamic> toJson() => {'id': id, 'name': name, 'rev': rev, if (deleted) 'deleted': true};

  @override
  bool operator ==(Object other) =>
      other is RoomInfo && other.id == id && other.name == name && other.rev == rev && other.deleted == deleted;

  @override
  int get hashCode => Object.hash(id, name, rev, deleted);

  @override
  String toString() => 'Room($id, "$name", rev $rev${deleted ? ', deleted' : ''})';
}

int _compareUtf8(String a, String b) {
  final x = utf8.encode(a), y = utf8.encode(b);
  final n = x.length < y.length ? x.length : y.length;
  for (var i = 0; i < n; i++) {
    if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
  }
  return x.length.compareTo(y.length);
}

/// Conflict rule shared with the firmware (configRoomUpsert/configRoomDelete):
/// higher rev wins; at equal rev a tombstone beats a live room; otherwise the greater name wins
/// (byte-wise UTF-8 comparison, like strcmp on the board).
bool roomBeats(RoomInfo a, RoomInfo b) {
  if (a.rev != b.rev) return a.rev > b.rev;
  if (a.deleted != b.deleted) return a.deleted;
  if (a.deleted) return false;
  return _compareUtf8(a.name, b.name) > 0;
}

/// Merges the room lists of all boards into one view (tombstones included, so deletes propagate).
Map<String, RoomInfo> mergeRoomTable(Iterable<List<RoomInfo>> perBoard) {
  final merged = <String, RoomInfo>{};
  for (final rooms in perBoard) {
    for (final r in rooms) {
      final cur = merged[r.id];
      if (cur == null || roomBeats(r, cur)) merged[r.id] = r;
    }
  }
  return merged;
}

/// Live rooms in a stable order (by id), for the tab bar.
List<RoomInfo> liveRooms(Map<String, RoomInfo> table) {
  final list = table.values.where((r) => !r.deleted).toList()..sort((a, b) => a.id.compareTo(b.id));
  return list;
}

/// One room command that brings a lagging board up to the merged state.
class RoomOp {
  const RoomOp.upsert(this.room) : delete = false;
  const RoomOp.delete(this.room) : delete = true;

  final bool delete;
  final RoomInfo room;
}

/// What must be sent to a board so its rooms equal [merged]. Tombstones are only sent to boards
/// that still hold the room alive (a board that never had it needs no tombstone).
List<RoomOp> roomRepairs(Map<String, RoomInfo> merged, List<RoomInfo> board) {
  final have = {for (final r in board) r.id: r};
  final ops = <RoomOp>[];
  for (final m in merged.values) {
    final b = have[m.id];
    if (b == null) {
      if (!m.deleted) ops.add(RoomOp.upsert(m));
    } else if (roomBeats(m, b)) {
      ops.add(m.deleted ? RoomOp.delete(m) : RoomOp.upsert(m));
    }
  }
  return ops;
}

// ---------------------------------------------------------------------------- inventory
class DeviceInfo {
  const DeviceInfo({
    required this.id,
    required this.name,
    required this.type,
    required this.group,
    required this.icon,
    required this.room,
    this.watts = 0,
    this.enabled = true,
    this.remote = true,
    this.fault,
    this.pin = -1,
    this.invert = false,
    this.autoOffMin = 0,
    this.bootState = 'off',
  });

  final String id;
  final String name;
  final String type; // switch | lock | pir
  final String group;
  final String icon;
  final String room; // room id, '' = unassigned
  final double watts;
  final bool enabled;
  final bool remote;
  final String? fault; // set when the pin is unusable on this board ("flash", "psram", ...)
  final int pin; // only present in the local REST copy
  final bool invert;
  final int autoOffMin;
  final String bootState;

  Map<String, dynamic> toJson() => {
        'id': id,
        'name': name,
        'type': type,
        'group': group,
        'icon': icon,
        'room': room,
        'watts': watts,
        'enabled': enabled,
        'remote': remote,
        if (fault != null) 'fault': fault,
        if (pin >= 0) 'pin': pin,
        if (invert) 'invert': true,
        if (autoOffMin != 0) 'autoOffMin': autoOffMin,
        if (bootState != 'off') 'bootState': bootState,
      };

  static DeviceInfo? tryParse(Object? v) {
    if (v is! Map) return null;
    final id = v['id'];
    if (id is! String || id.isEmpty) return null;
    return DeviceInfo(
      id: id,
      name: (v['name'] as String?) ?? id,
      type: (v['type'] as String?) ?? 'switch',
      group: (v['group'] as String?) ?? 'Other',
      icon: (v['icon'] as String?) ?? 'plug',
      room: (v['room'] as String?) ?? '',
      watts: (v['watts'] as num?)?.toDouble() ?? 0,
      enabled: v['enabled'] != false,
      remote: v['remote'] != false,
      fault: v['fault'] as String?,
      pin: (v['pin'] as num?)?.toInt() ?? -1,
      invert: v['invert'] == true,
      autoOffMin: (v['autoOffMin'] as num?)?.toInt() ?? 0,
      bootState: (v['bootState'] as String?) ?? 'off',
    );
  }
}

class SceneInfo {
  const SceneInfo(this.id, this.name);
  final String id;
  final String name;

  Map<String, dynamic> toJson() => {'id': id, 'name': name};
}

List<int> _ints(Object? v) => v is List ? [for (final x in v) if (x is num) x.toInt()] : const <int>[];

class BoardInventory {
  const BoardInventory({
    required this.board,
    required this.name,
    required this.fw,
    required this.chip,
    required this.psram,
    required this.cfgRev,
    required this.maxDevices,
    required this.rooms,
    required this.devices,
    required this.scenes,
    this.schedules = 0,
    this.automations = 0,
    this.pinsOut,
    this.pinsIn,
  });

  final String board;
  final String name;
  final String fw;
  final String chip;
  final bool psram;
  final int cfgRev;
  final int maxDevices;
  final List<RoomInfo> rooms;
  final List<DeviceInfo> devices;
  final List<SceneInfo> scenes;
  final int schedules;
  final int automations;
  final List<int>? pinsOut; // local REST only
  final List<int>? pinsIn;

  /// Same shape as the firmware's inventory document, so [tryParse] reads it back (used by the start-up cache).
  Map<String, dynamic> toJson() => {
        'v': protocolVersion,
        'board': board,
        'name': name,
        'fw': fw,
        'chip': chip,
        'psram': psram,
        'cfgRev': cfgRev,
        'maxDevices': maxDevices,
        'rooms': [for (final r in rooms) r.toJson()],
        'devices': [for (final d in devices) d.toJson()],
        'scenes': [for (final s in scenes) s.toJson()],
        'counts': {'schedules': schedules, 'automations': automations},
        if (pinsOut != null || pinsIn != null) 'pins': {'out': pinsOut ?? const <int>[], 'in': pinsIn ?? const <int>[]},
      };

  /// Null when the document is not protocol version 1 or lacks a valid board id.
  static BoardInventory? tryParse(Object? json) {
    if (json is! Map) return null;
    if (json['v'] != protocolVersion) return null;
    final board = json['board'];
    if (board is! String || !validBoardId(board)) return null;
    final rooms = (json['rooms'] is List ? json['rooms'] as List : const <Object?>[])
        .map(RoomInfo.tryParse)
        .whereType<RoomInfo>()
        .toList();
    final devices = (json['devices'] is List ? json['devices'] as List : const <Object?>[])
        .map(DeviceInfo.tryParse)
        .whereType<DeviceInfo>()
        .toList();
    final scenes = <SceneInfo>[
      if (json['scenes'] is List)
        for (final s in json['scenes'] as List)
          if (s is Map && s['id'] is String) SceneInfo(s['id'] as String, (s['name'] as String?) ?? s['id'] as String),
    ];
    final counts = json['counts'] is Map ? json['counts'] as Map : const {};
    final pins = json['pins'] is Map ? json['pins'] as Map : null;
    return BoardInventory(
      board: board,
      name: (json['name'] as String?) ?? board,
      fw: (json['fw'] as String?) ?? '',
      chip: (json['chip'] as String?) ?? 'esp32',
      psram: json['psram'] == true,
      cfgRev: (json['cfgRev'] as num?)?.toInt() ?? 0,
      maxDevices: (json['maxDevices'] as num?)?.toInt() ?? 24,
      rooms: rooms,
      devices: devices,
      scenes: scenes,
      schedules: (counts['schedules'] as num?)?.toInt() ?? 0,
      automations: (counts['automations'] as num?)?.toInt() ?? 0,
      pinsOut: pins == null ? null : _ints(pins['out']),
      pinsIn: pins == null ? null : _ints(pins['in']),
    );
  }
}

// ---------------------------------------------------------------------------- live state
class LiveDevice {
  const LiveDevice({required this.on, this.wh = 0});
  final bool on;
  final double wh;

  Map<String, dynamic> toJson() => {'on': on, 'wh': wh};
}

class BoardState {
  const BoardState({
    required this.seq,
    required this.ts,
    required this.uptime,
    required this.rssi,
    required this.ip,
    required this.dnd,
    required this.temp,
    required this.hum,
    required this.powerW,
    required this.cloud,
    required this.devices,
  });

  final int seq;
  final int ts; // board epoch seconds, 0 = its clock is not set
  final int uptime;
  final int rssi;
  final String ip;
  final bool dnd;
  final double? temp;
  final double? hum;

  /// ESTIMATED: rated watts of the devices that are on. The boards have no current sensor.
  final double powerW;
  final String cloud;
  final Map<String, LiveDevice> devices;

  Map<String, dynamic> toJson() => {
        'v': protocolVersion,
        'seq': seq,
        'ts': ts,
        'up': uptime,
        'rssi': rssi,
        'ip': ip,
        'dnd': dnd,
        if (temp != null) 'temp': temp,
        if (hum != null) 'hum': hum,
        'powerW': powerW,
        'cloud': cloud,
        'd': {for (final e in devices.entries) e.key: e.value.toJson()},
      };

  static BoardState? tryParse(Object? json) {
    if (json is! Map || json['v'] != protocolVersion) return null;
    final live = <String, LiveDevice>{};
    final d = json['d'];
    if (d is Map) {
      for (final e in d.entries) {
        final v = e.value;
        if (e.key is String && v is Map) {
          live[e.key as String] = LiveDevice(on: v['on'] == true, wh: (v['wh'] as num?)?.toDouble() ?? 0);
        }
      }
    }
    return BoardState(
      seq: (json['seq'] as num?)?.toInt() ?? 0,
      ts: (json['ts'] as num?)?.toInt() ?? 0,
      uptime: (json['up'] as num?)?.toInt() ?? 0,
      rssi: (json['rssi'] as num?)?.toInt() ?? 0,
      ip: (json['ip'] as String?) ?? '',
      dnd: json['dnd'] == true,
      temp: (json['temp'] as num?)?.toDouble(),
      hum: (json['hum'] as num?)?.toDouble(),
      powerW: (json['powerW'] as num?)?.toDouble() ?? 0,
      cloud: (json['cloud'] as String?) ?? '',
      devices: live,
    );
  }
}

class CommandAck {
  const CommandAck({required this.id, required this.ok, this.error, this.duplicate = false});
  final String id;
  final bool ok;
  final String? error;
  final bool duplicate;

  static CommandAck? tryParse(Object? json) {
    if (json is! Map || json['v'] != protocolVersion) return null;
    final id = json['id'];
    if (id is! String) return null;
    return CommandAck(id: id, ok: json['ok'] == true, error: json['err'] as String?, duplicate: json['dup'] == true);
  }
}

/// Where a board's data currently comes from.
enum BoardSource { local, cloud, none }

/// Everything the app knows about one board through MQTT.
class BoardView {
  BoardView(this.id);

  final String id;
  BoardInventory? inventory;
  BoardState? state;
  bool? lwtOnline; // null = no last-will message seen yet
  DateTime? stateAt;
  DateTime? inventoryAt;

  /// Reachable through the broker right now: not announced offline and its state is recent.
  /// Boards publish a heartbeat every 60 s, so 150 s tolerates two missed ones.
  bool isAvailable(DateTime now, {Duration maxAge = const Duration(seconds: 150)}) {
    if (lwtOnline == false) return false;
    final at = stateAt;
    return at != null && now.difference(at) <= maxAge;
  }
}

// ---------------------------------------------------------------------------- composite ids
/// Devices of different boards may share an id ("light"), so the dashboard uses "`<board>`/`<device>`".
String compositeId(String board, String device) => '$board/$device';

({String board, String device})? splitCompositeId(String id) {
  final i = id.indexOf('/');
  if (i <= 0 || i == id.length - 1) return null;
  return (board: id.substring(0, i), device: id.substring(i + 1));
}

/// Maps an inventory + live state to the dashboard's device model. [roomIndex] translates a
/// room id into the index of the tab bar.
List<Esp32Device> dashboardDevices(
  BoardInventory inv,
  BoardState? st, {
  required int Function(String roomId) roomIndex,
  required bool available,
}) {
  return [
    for (final d in inv.devices)
      Esp32Device(
        id: compositeId(inv.board, d.id),
        name: d.name,
        type: d.type,
        group: d.group,
        icon: d.icon,
        room: roomIndex(d.room),
        on: st?.devices[d.id]?.on ?? false,
        watts: d.watts,
        wh: st?.devices[d.id]?.wh ?? 0,
        pin: d.pin,
        boardId: inv.board,
        available: available && d.enabled && d.fault == null,
        remoteAllowed: d.remote,
      ),
  ];
}

// ---------------------------------------------------------------------------- local REST adapter
/// Converts the body of GET /api/v2/state into the dashboard's [Esp32State].
/// Throws [FormatException] when the document is not a firmware 4.x answer.
Esp32State esp32StateFromRest(Map<String, dynamic> j) {
  final inv = BoardInventory.tryParse(j['inventory']);
  final st = BoardState.tryParse(j['state']);
  if (inv == null || st == null) throw const FormatException('not a firmware 4.x state document');
  final wifi = j['wifi'] is Map ? Map<String, dynamic>.from(j['wifi'] as Map) : const <String, dynamic>{};
  final roomIds = (inv.rooms.where((r) => !r.deleted).map((r) => r.id).toList()..sort());
  int roomIndex(String id) {
    final i = roomIds.indexOf(id);
    return i < 0 ? 0 : i;
  }

  return Esp32State(
    name: inv.name,
    board: inv.board,
    fw: inv.fw,
    ssid: (wifi['ssid'] as String?) ?? '',
    ip: st.ip,
    rssi: st.rssi,
    uptime: st.uptime,
    temp: st.temp,
    hum: st.hum,
    dnd: st.dnd,
    camera: '',
    powerW: st.powerW,
    devices: dashboardDevices(inv, st, roomIndex: roomIndex, available: true),
    mode: wifiModeOf(wifi['mode']),
    apSsid: (wifi['apSsid'] as String?) ?? '',
    pinsOut: inv.pinsOut ?? defaultOutPins,
    pinsIn: inv.pinsIn ?? defaultInPins,
    maxDevices: inv.maxDevices,
  );
}
