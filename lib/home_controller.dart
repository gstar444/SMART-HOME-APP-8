import 'dart:async';
import 'dart:convert';
import 'dart:math';

import 'package:flutter/foundation.dart';

import 'esp32_api.dart';
import 'models.dart';
import 'mqtt_models.dart';
import 'mqtt_service.dart';
import 'settings.dart';
import 'snapshot_cache.dart';
import 'strings.dart';
import 'strings_cloud.dart';

/// One row of the "Boards" screen.
class BoardStatus {
  const BoardStatus({
    required this.id,
    required this.name,
    required this.source,
    required this.available,
    required this.fw,
    required this.chip,
    this.lastSeen,
  });

  final String id;
  final String name;
  final BoardSource source;
  final bool available;
  final String fw;
  final String chip;
  final DateTime? lastSeen;
}

class _Overlay {
  _Overlay(this.on, this.sentAt);
  final bool on;
  final DateTime sentAt;
}

/// One view of the whole home for the dashboard: every board, every room, every device.
///
/// Data sources, per board:
///   * LOCAL  - REST polling of the active board over the LAN: immediate, works with no internet;
///   * CLOUD  - retained MQTT state/inventory through the broker: every board, anywhere.
/// When both exist the local copy wins (fresher, and commands go straight to the board). A board
/// that is neither announced online nor recently heard from stays listed, but its devices are
/// marked unavailable instead of showing stale values as if they were live.
///
/// Commands follow the same rule. Over MQTT the tile updates optimistically; the overlay is
/// dropped as soon as a newer state arrives from the board, on a failed acknowledgement, or after
/// 10 s, so the UI always converges on what the board really reports.
///
/// Hardware settings (GPIO pins, wattage, Wi-Fi) are LOCAL ONLY by design: a stolen cloud login can
/// switch devices but can not rewire or reconfigure a board.
class HomeController extends ChangeNotifier {
  HomeController({
    required this.settings,
    required this.mqtt,
    required this.local,
    this.cache,
    DateTime Function()? clock,
    Duration reevaluateEvery = const Duration(seconds: 20),
    this._cacheDelay = const Duration(seconds: 3),
    Random? random,
  })  : _clock = clock ?? DateTime.now,
        _random = random ?? Random.secure() {
    _restored.addAll(cache?.load(now: _clock()) ?? const <String, CachedBoard>{});
    mqtt.addListener(_onSource);
    local.addListener(_onSource);
    _ticker = Timer.periodic(reevaluateEvery, (_) => _onSource());
    _rebuild();
  }

  final SettingsController settings;
  final MqttService mqtt;
  final LocalBoard local;

  /// Last known state of every board, shown at start-up while the network is still answering.
  final SnapshotCache? cache;
  final Duration _cacheDelay;
  final DateTime Function() _clock;
  final Random _random;
  Timer? _ticker;
  bool _disposed = false;

  Esp32State? _state;
  List<RoomInfo> _rooms = const <RoomInfo>[];
  Map<String, RoomInfo> _roomTable = const <String, RoomInfo>{};
  List<BoardStatus> _boards = const <BoardStatus>[];
  final Map<String, _Overlay> _overlay = <String, _Overlay>{};
  final Set<String> _pending = <String>{};
  final Map<String, DateTime> _lastRepair = <String, DateTime>{};
  final Map<String, CachedBoard> _restored = <String, CachedBoard>{};
  Timer? _cacheTimer;
  String _lastSignature = '';
  final List<Map<String, double>> _samples = <Map<String, double>>[];
  DateTime? _lastSample;
  bool _anyAvailable = false;
  Map<String, BoardInventory> _inventories = const <String, BoardInventory>{};

  Tr get _tr => Tr.of(settings.value);

  // ------------------------------------------------------------------ read side
  Esp32State? get state => _state;
  bool get online => _anyAvailable;
  bool get loaded => local.loaded || _state != null;
  String? get error => _state == null ? (local.error ?? mqtt.error) : null;
  bool isBusy(String key) => _pending.contains(key) || local.isBusy(key);
  List<Map<String, double>> get samples => List.unmodifiable(_samples);
  List<BoardStatus> get boards => _boards;

  /// Names for the room tabs (never empty, so the tab bar always has something to show).
  List<String> get roomNames => _rooms.isEmpty ? const <String>['Room 1'] : [for (final r in _rooms) r.name];
  List<String> get roomIds => [for (final r in _rooms) r.id];

  String get localBoardId => local.boardId;
  bool get localOnline => local.online;

  /// The local copy of a device (it carries the GPIO pin, which the cloud never sends).
  Esp32Device? localDevice(Esp32Device d) {
    final s = local.state;
    if (s == null || d.boardId != local.boardId) return null;
    for (final x in s.devices) {
      if (x.id == d.id) return x;
    }
    return null;
  }

  // ---------------------------------------------------------------- rebuilding
  void _onSource() {
    if (_disposed) return;
    _rebuild();
    notifyListeners();
  }

  void _rebuild() {
    final now = _clock();
    final invs = <String, BoardInventory>{};
    final live = <String, BoardState?>{};
    final avail = <String, bool>{};
    final source = <String, BoardSource>{};
    final seen = <String, DateTime?>{};

    for (final v in mqtt.boards.values) {
      final inv = v.inventory;
      if (inv == null) continue;
      invs[v.id] = inv;
      live[v.id] = v.state;
      final ok = mqtt.connected && v.isAvailable(now);
      avail[v.id] = ok;
      source[v.id] = ok ? BoardSource.cloud : BoardSource.none;
      seen[v.id] = v.stateAt;
    }
    final linv = local.inventory;
    if (linv != null) {
      final reachable = local.online;
      if (reachable || !invs.containsKey(linv.board)) {
        invs[linv.board] = linv;
        live[linv.board] = local.liveState;
        avail[linv.board] = reachable;
        source[linv.board] = reachable ? BoardSource.local : BoardSource.none;
        seen[linv.board] = reachable ? now : seen[linv.board];
      }
    }

    // Boards remembered from the last run are shown at once, but never as live: their devices stay
    // unavailable until the board really answers (over the local link or the broker).
    for (final r in _restored.entries) {
      if (invs.containsKey(r.key)) continue;
      invs[r.key] = r.value.inventory;
      live[r.key] = r.value.state;
      avail[r.key] = false;
      source[r.key] = BoardSource.none;
      seen[r.key] = r.value.savedAt;
    }

    // overlays that the board has already answered (or that grew old) are dropped
    _overlay.removeWhere((id, o) {
      if (now.difference(o.sentAt) > const Duration(seconds: 10)) return true;
      final sp = splitCompositeId(id);
      final at = sp == null ? null : mqtt.boards[sp.board]?.stateAt;
      return at != null && at.isAfter(o.sentAt);
    });

    _roomTable = mergeRoomTable(invs.values.map((i) => i.rooms));
    _rooms = liveRooms(_roomTable);
    final roomIndex = {for (var i = 0; i < _rooms.length; i++) _rooms[i].id: i};

    final ids = invs.keys.toList()
      ..sort((a, b) {
        final c = invs[a]!.name.toLowerCase().compareTo(invs[b]!.name.toLowerCase());
        return c != 0 ? c : a.compareTo(b);
      });

    final devices = <Esp32Device>[];
    var power = 0.0;
    final temps = <double>[], hums = <double>[];
    var dnd = false;
    for (final id in ids) {
      final inv = invs[id]!;
      final st = live[id];
      final ok = avail[id] ?? false;
      for (final d in dashboardDevices(inv, st, roomIndex: (r) => roomIndex[r] ?? 0, available: ok)) {
        final o = _overlay[d.id];
        devices.add(o == null ? d : d.copyWith(on: o.on));
      }
      if (ok && st != null) {
        power += st.powerW;
        if (st.temp != null) temps.add(st.temp!);
        if (st.hum != null) hums.add(st.hum!);
        dnd = dnd || st.dnd;
      }
    }

    final ls = local.state;
    _anyAvailable = avail.values.any((v) => v);
    _inventories = invs;
    _boards = [
      for (final id in ids)
        BoardStatus(
          id: id,
          name: invs[id]!.name,
          source: source[id] ?? BoardSource.none,
          available: avail[id] ?? false,
          fw: invs[id]!.fw,
          chip: invs[id]!.chip,
          lastSeen: seen[id],
        ),
    ];
    _state = ids.isEmpty
        ? null
        : Esp32State(
            name: settings.value.title,
            fw: {for (final id in ids) invs[id]!.fw}.join(', '),
            ssid: ls?.ssid ?? '',
            ip: ls?.ip ?? '',
            rssi: ls?.rssi ?? 0,
            uptime: ls?.uptime ?? 0,
            temp: temps.isEmpty ? null : temps.reduce((a, b) => a + b) / temps.length,
            hum: hums.isEmpty ? null : hums.reduce((a, b) => a + b) / hums.length,
            dnd: dnd,
            camera: settings.value.cameraUrl,
            powerW: power,
            devices: devices,
            mode: ls?.mode ?? 'sta',
            apSsid: ls?.apSsid ?? '',
            pinsOut: ls?.pinsOut ?? defaultOutPins,
            pinsIn: ls?.pinsIn ?? defaultInPins,
            maxDevices: ls?.maxDevices ?? 24,
          );

    _recordSample(now);
    _reconcileRooms(now, avail);
    _scheduleCacheSave(invs, live, avail);
  }

  // ------------------------------------------------------------------- start-up cache
  /// Only data that really came from a board is remembered, and only when something meaningful changed
  /// (names, rooms, on/off, temperature) - not on every heartbeat - so flash is not written needlessly.
  void _scheduleCacheSave(Map<String, BoardInventory> invs, Map<String, BoardState?> live, Map<String, bool> avail) {
    if (cache == null || _disposed) return;
    final fresh = <String, CachedBoard>{};
    final now = _clock();
    for (final id in invs.keys) {
      if (avail[id] != true) continue;
      fresh[id] = CachedBoard(inventory: invs[id]!, state: live[id], savedAt: now);
    }
    if (fresh.isEmpty) return;
    final sig = _signature(fresh);
    if (sig == _lastSignature) return;
    _pendingFresh = fresh;
    _pendingSignature = sig;
    _cacheTimer ??= Timer(_cacheDelay, _flushCache);
  }

  Map<String, CachedBoard> _pendingFresh = const <String, CachedBoard>{};
  String _pendingSignature = '';

  String _signature(Map<String, CachedBoard> boards) {
    final ids = boards.keys.toList()..sort();
    return jsonEncode([
      for (final id in ids)
        [
          boards[id]!.inventory.toJson(),
          {
            for (final e in (boards[id]!.state?.devices ?? const <String, LiveDevice>{}).entries) e.key: e.value.on,
          },
          boards[id]!.state?.temp,
          boards[id]!.state?.hum,
        ],
    ]);
  }

  Future<void> _flushCache() async {
    _cacheTimer = null;
    final c = cache;
    if (c == null || _disposed || _pendingSignature == _lastSignature) return;
    _restored.addAll(_pendingFresh); // what we just learned replaces the old memory of those boards
    _lastSignature = _pendingSignature;
    await c.save(_restored);
  }

  /// Removes everything remembered about a board (used when the board is removed from the app).
  Future<void> forgetCached(String boardId) async {
    if (_restored.remove(boardId) == null) return;
    _lastSignature = '';
    await cache?.save(_restored);
    _onSource();
  }

  void _recordSample(DateTime now) {
    final s = _state;
    if (s == null) return;
    final last = _lastSample;
    if (last != null && now.difference(last) < const Duration(seconds: 15)) return;
    _lastSample = now;
    _samples.add({
      for (final d in s.devices)
        if (d.on && d.available && d.watts > 0) d.id: d.watts,
    });
    if (_samples.length > 20) _samples.removeAt(0);
  }

  // ------------------------------------------------------------------- routing
  bool _viaLocal(String boardId) => local.online && local.boardId == boardId && boardId.isNotEmpty;

  Future<String?> _remote(String board, Map<String, dynamic> body, {String? busyKey}) async {
    if (!mqtt.connected) return _tr.notConnected;
    if (busyKey != null) {
      _pending.add(busyKey);
      _onSource();
    }
    final ack = await mqtt.send(board, body);
    if (busyKey != null) _pending.remove(busyKey);
    return ack.ok ? null : _tr.boardError(ack.error);
  }

  Esp32Device? _device(String id) {
    final s = _state;
    if (s == null) return null;
    for (final d in s.devices) {
      if (d.id == id) return d;
    }
    return null;
  }

  Future<String?> setDevice(String id, bool on) async {
    final sp = splitCompositeId(id);
    final dev = _device(id);
    if (sp == null || dev == null) return _tr.notConnected;
    if (!dev.available) return _tr.boardOffline;
    if (_viaLocal(sp.board)) return local.setDevice(id, on);
    if (!dev.remoteAllowed && !(dev.type == 'lock' && on)) return _tr.remoteNotAllowed;

    _overlay[id] = _Overlay(on, _clock());
    final err = await _remote(sp.board, {'op': 'set', 'dev': sp.device, 'on': on}, busyKey: id);
    if (err != null) _overlay.remove(id); // the board refused or never answered: show the truth again
    _onSource();
    return err;
  }

  Future<String?> setDnd(bool on) async {
    String? first;
    var reached = false;
    for (final b in _boards.where((b) => b.available)) {
      final err = _viaLocal(b.id) ? await local.setDnd(on) : await _remote(b.id, {'op': 'dnd', 'on': on});
      if (err == null) {
        reached = true;
      } else {
        first ??= err;
      }
    }
    _onSource();
    return reached ? null : (first ?? _tr.notConnected);
  }

  /// Runs a routine on every reachable board (each board applies only what concerns its own devices).
  Future<String?> runScene(String sceneId) async {
    final key = 'scene:$sceneId';
    _pending.add(key);
    _onSource();
    String? first;
    var reached = false;
    for (final b in _boards.where((b) => b.available)) {
      final err = _viaLocal(b.id) ? await local.runScene(sceneId) : await _remote(b.id, {'op': 'scene', 'scene': sceneId});
      if (err == null) {
        reached = true;
      } else {
        first ??= err;
      }
    }
    _pending.remove(key);
    _onSource();
    return reached ? null : (first ?? _tr.notConnected);
  }

  Future<void> refresh() => local.refresh();

  // --------------------------------------------------------------- device list
  // Adding, rewiring (GPIO pin) and removing devices is local-only on purpose.
  Future<String?> addDevice({
    required String name,
    required String icon,
    required int pin,
    required int room,
    int? watts,
  }) {
    if (!local.online) return Future.value(_tr.localOnly);
    return local.addDevice(name: name, icon: icon, pin: pin, roomId: _roomIdAt(room), watts: watts);
  }

  Future<String?> updateDevice(
    String id, {
    String? name,
    String? icon,
    int? pin,
    int? room,
    int? watts,
  }) {
    final sp = splitCompositeId(id);
    if (sp == null) return Future.value(_tr.notConnected);
    final roomId = room == null ? null : _roomIdAt(room);
    if (_viaLocal(sp.board)) {
      return local.updateDevice(id, name: name, icon: icon, pin: pin, roomId: roomId, watts: watts);
    }
    if (pin != null || watts != null) return Future.value(_tr.localOnly);
    return _remote(
      sp.board,
      {
        'op': 'device.update',
        'dev': sp.device,
        'name': ?name,
        'icon': ?icon,
        'room': ?roomId,
      },
      busyKey: id,
    );
  }

  Future<String?> deleteDevice(String id) {
    final sp = splitCompositeId(id);
    if (sp == null || !_viaLocal(sp.board)) return Future.value(_tr.localOnly);
    return local.deleteDevice(id);
  }

  Future<String?> moveDevice(String id, int roomIndex) => updateDevice(id, room: roomIndex);

  // --------------------------------------------------------------------- rooms
  String _roomIdAt(int index) => (index >= 0 && index < _rooms.length) ? _rooms[index].id : (_rooms.isEmpty ? '' : _rooms.first.id);

  /// "r" + time (base 36, 6 digits until 2059) + 2 random characters: unique across phones and
  /// sorting in creation order, so a new room becomes the last tab. Matches [A-Za-z0-9_]{1,15}.
  String _newRoomId() {
    const chars = 'abcdefghijklmnopqrstuvwxyz0123456789';
    final seconds = _clock().millisecondsSinceEpoch ~/ 1000;
    final salt = List.generate(2, (_) => chars[_random.nextInt(chars.length)]).join();
    return 'r${seconds.toRadixString(36)}$salt';
  }

  Future<String?> addRoom(String name) =>
      _broadcastRoom(RoomOp.upsert(RoomInfo(id: _newRoomId(), name: name.trim(), rev: 1)));

  Future<String?> renameRoom(int index, String name) {
    if (index < 0 || index >= _rooms.length) return Future.value(_tr.notConnected);
    final r = _rooms[index];
    return _broadcastRoom(RoomOp.upsert(RoomInfo(id: r.id, name: name.trim(), rev: r.rev + 1)));
  }

  Future<String?> deleteRoom(int index) {
    if (index < 0 || index >= _rooms.length) return Future.value(_tr.notConnected);
    final r = _rooms[index];
    return _broadcastRoom(RoomOp.delete(RoomInfo(id: r.id, name: r.name, rev: r.rev + 1, deleted: true)));
  }

  Map<String, dynamic> _roomJson(RoomInfo r) => {'id': r.id, 'name': r.name, 'rev': r.rev};

  Future<String?> _sendRoom(String board, RoomOp op) {
    if (_viaLocal(board)) return op.delete ? local.deleteRoom(op.room) : local.upsertRoom(op.room);
    return _remote(board, {'op': op.delete ? 'room.delete' : 'room.upsert', 'room': _roomJson(op.room)});
  }

  /// Room metadata is replicated on every board. Boards that can not be reached now are brought
  /// up to date later by [_reconcileRooms].
  Future<String?> _broadcastRoom(RoomOp op) async {
    String? first;
    var accepted = false;
    for (final b in _boards.where((b) => b.available)) {
      final err = await _sendRoom(b.id, op);
      if (err == null) {
        accepted = true;
      } else {
        first ??= err;
      }
    }
    if (accepted) {
      unawaited(local.refresh());
      return null;
    }
    return first ?? _tr.notConnected;
  }

  /// A board that missed a room edit gets the merged state pushed (at most once a minute each).
  void _reconcileRooms(DateTime now, Map<String, bool> avail) {
    for (final entry in _inventories.entries) {
      if (avail[entry.key] != true) continue;
      final last = _lastRepair[entry.key];
      if (last != null && now.difference(last) < const Duration(seconds: 60)) continue;
      final ops = roomRepairs(_roomTable, entry.value.rooms);
      if (ops.isEmpty) continue;
      _lastRepair[entry.key] = now;
      unawaited(Future(() async {
        for (final op in ops) {
          await _sendRoom(entry.key, op);
        }
      }));
    }
  }

  @override
  void dispose() {
    _disposed = true;
    _ticker?.cancel();
    _cacheTimer?.cancel();
    mqtt.removeListener(_onSource);
    local.removeListener(_onSource);
    super.dispose();
  }
}
