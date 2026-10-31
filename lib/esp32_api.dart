import 'package:multicast_dns/multicast_dns.dart';
import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/widgets.dart';

import 'models.dart';
import 'mqtt_models.dart';
import 'secure_storage.dart';
import 'settings.dart';
import 'strings.dart';
import 'strings_cloud.dart';

enum ApiErrorKind { network, auth, server, firmware, notClaimed, rateLimited }

class ApiException implements Exception {
  ApiException(this.kind, [this.detail]);

  final ApiErrorKind kind;
  final String? detail;

  @override
  String toString() => 'ApiException($kind, $detail)';
}

/// Result of a call that returns data: either [data] or a user-readable [error].
class ApiResult<T> {
  const ApiResult.ok(this.data) : error = null;
  const ApiResult.fail(this.error) : data = null;

  final T? data;
  final String? error;

  bool get ok => error == null;
}

/// Client for ONE board's local REST API v2 (firmware 4.x), plain HTTP on the local network.
///
///   GET  /api/v2/info                    no token: board id, firmware, "claimed" flag
///   POST /api/v2/claim                   no token: {code, token} while the board is unclaimed
///   GET  /api/v2/state                   inventory + live state + wifi + mqtt status
///   POST /api/v2/set | scene | dnd       control
///   POST /api/v2/device|room/...         configuration
///   GET  /api/v2/wifi/status|scan, POST /api/v2/wifi/scan|connect|reset
///
/// Rules this client follows: the token travels only in the `Authorization: Bearer` header, every
/// change is a POST with a JSON body, nothing goes into the URL, the token is never logged.
/// (The local link is plain HTTP, protected by the Wi-Fi's own encryption; see docs/SECURITY.md.
/// Remote access uses MQTT over verified TLS instead.)
class BoardHttp {
  BoardHttp({required this.baseUrl, this.token, HttpClient? client})
      : _http = client ?? (HttpClient()..connectionTimeout = const Duration(seconds: 2)),
        _ownsClient = client == null;

  final String baseUrl;
  final String? token;
  final HttpClient _http;
  final bool _ownsClient;

  void close() {
    if (_ownsClient) _http.close(force: true);
  }

  /// [path] is relative to /api/v2/, for example "state" or "wifi/connect".
  Future<Map<String, dynamic>> call(
    String method,
    String path, {
    Map<String, dynamic>? body,
    bool auth = true,
  }) async {
    if (auth && (token == null || token!.isEmpty)) throw ApiException(ApiErrorKind.auth, 'no_token');
    try {
      final uri = Uri.parse(baseUrl).replace(path: '/api/v2/$path');
      final req = await _http.openUrl(method, uri).timeout(const Duration(seconds: 3));
      if (auth) req.headers.set(HttpHeaders.authorizationHeader, 'Bearer $token');
      req.headers.set(HttpHeaders.acceptHeader, 'application/json');
      if (method != 'GET') {
        final bytes = utf8.encode(jsonEncode(body ?? const <String, dynamic>{}));
        req.headers.contentType = ContentType.json;
        req.contentLength = bytes.length;
        req.add(bytes);
      }
      final res = await req.close().timeout(const Duration(seconds: 4));
      final text = await res.transform(utf8.decoder).join().timeout(const Duration(seconds: 4));

      Map<String, dynamic>? json;
      try {
        final decoded = jsonDecode(text);
        if (decoded is Map<String, dynamic>) json = decoded;
      } catch (_) {
        // not JSON: handled below
      }
      final error = json?['error']?.toString();
      final code = res.statusCode;

      if (code == 401) throw ApiException(ApiErrorKind.auth, error);
      if (code == 403 && error == 'not_claimed') throw ApiException(ApiErrorKind.notClaimed, error);
      if (code == 429) throw ApiException(ApiErrorKind.rateLimited, error);
      // A web page / 404 instead of JSON: the board still runs firmware 3.x (no /api/v2).
      if (json == null && (code == 404 || code == 200)) throw ApiException(ApiErrorKind.firmware);
      if (code == 404 && (error == 'not found' || error == 'not_found_or_wrong_method')) {
        throw ApiException(ApiErrorKind.firmware, error);
      }
      if (code < 200 || code >= 300) throw ApiException(ApiErrorKind.server, error ?? 'HTTP $code');
      if (json == null) throw ApiException(ApiErrorKind.firmware);
      return json;
    } on ApiException {
      rethrow;
    } on TimeoutException {
      throw ApiException(ApiErrorKind.network, 'timeout');
    } on SocketException catch (e) {
      throw ApiException(ApiErrorKind.network, e.message);
    } on HttpException catch (e) {
      throw ApiException(ApiErrorKind.network, e.message);
    } on FormatException catch (e) {
      throw ApiException(ApiErrorKind.network, e.message);
    }
  }
}

/// What the rest of the app needs from the local (REST) path to the active board. [Esp32Controller]
/// is the real implementation; tests use a fake.
abstract interface class LocalBoard implements Listenable {
  Esp32State? get state;
  BoardInventory? get inventory;
  BoardState? get liveState;
  bool get online;
  bool get loaded;
  String? get error;
  bool get hasToken;

  /// Id of the board this link talks to ('' until known).
  String get boardId;
  String? get recoveryCode;
  String? get recoveryHotspot;
  bool isBusy(String key);

  Future<void> refresh();
  Future<void> reloadToken();
  Future<String?> setDevice(String id, bool on);
  Future<String?> setDnd(bool on);
  Future<String?> runScene(String id);
  Future<String?> addDevice({
    required String name,
    required String icon,
    required int pin,
    required String roomId,
    int? watts,
  });
  Future<String?> updateDevice(String id, {String? name, String? icon, int? pin, String? roomId, int? watts});
  Future<String?> deleteDevice(String id);
  Future<String?> upsertRoom(RoomInfo room);
  Future<String?> deleteRoom(RoomInfo room);
  Future<ApiResult<WifiScan>> wifiScan({bool rescan = false});
  Future<ApiResult<WifiStatus>> wifiStatus();
  Future<String?> wifiConnect(String ssid, String password);
  Future<String?> wifiReset();
}

/// Polls the ACTIVE board over the local network, applies changes optimistically (the tile reacts
/// instantly) and rolls back to the real previous value if the board refuses or is unreachable.
///
/// Remote/multi-board use goes through MqttService + HomeController; this controller is the
/// immediate, no-internet-needed path and the one used for hardware setup (pins, Wi-Fi, claiming).
class Esp32Controller extends ChangeNotifier with WidgetsBindingObserver implements LocalBoard {
  Esp32Controller(this._settings, this._store) {
    _applied = _settings.value;
    _settings.addListener(_onSettings);
    WidgetsBinding.instance.addObserver(this);
    unawaited(_loadToken().then((_) {
      if (_disposed) return;
      _startTimer();
      refresh();
    }));
  }

  final SettingsController _settings;
  final SecureStore _store;
  late AppSettings _applied;
  final HttpClient _client = HttpClient()..connectionTimeout = const Duration(seconds: 2);

  Timer? _timer;
  bool _refreshing = false;
  bool _disposed = false;
  bool _adopting = false;
  int _failures = 0;
  String? _token;
  DateTime _lastWrite = DateTime.fromMillisecondsSinceEpoch(0);
  DateTime? _lastSample;

  Esp32State? _state;
  BoardInventory? _inventory;
  BoardState? _liveState;
  bool _online = false;
  bool _loaded = false; // true after the first answer *or* the first failure
  String? _error;
  final Set<String> _pending = <String>{};
  final List<Map<String, double>> _samples = <Map<String, double>>[];

  /// Shown once to the owner after "forget Wi-Fi": password of the recovery hotspot.
  @override
  String? recoveryCode;
  @override
  String? recoveryHotspot;

  @override
  Esp32State? get state => _state;
  @override
  BoardInventory? get inventory => _inventory;
  @override
  BoardState? get liveState => _liveState;
  @override
  bool get online => _online;
  @override
  bool get loaded => _loaded;
  @override
  String? get error => _error;
  @override
  bool get hasToken => _token != null && _token!.isNotEmpty;
  @override
  bool isBusy(String key) => _pending.contains(key);

  /// Id of the board this controller talks to ('' until known).
  @override
  String get boardId => _state?.board ?? _settings.value.device.boardId;

  /// Recent power samples (one every ~15 s, newest last): device id -> watts.
  List<Map<String, double>> get samples => List.unmodifiable(_samples);

  Tr get _tr => Tr.of(_settings.value);

  // ------------------------------------------------------------ token
  Future<void> _loadToken() async {
    _token = await _store.boardToken(_settings.value.device.tokenKey);
  }

  /// Call after a token was stored (claim, manual entry, rotation).
  @override
  Future<void> reloadToken() async {
    await _loadToken();
    _failures = 0;
    _error = null;
    await refresh();
  }

  // ------------------------------------------------------------ lifecycle
  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state == AppLifecycleState.resumed) {
      _startTimer();
      refresh();
    } else if (state == AppLifecycleState.paused) {
      _timer?.cancel();
    }
  }

  void _startTimer() {
    _timer?.cancel();
    _timer = Timer.periodic(Duration(seconds: _settings.value.pollSeconds), (_) => refresh());
  }

  void _onSettings() {
    final prev = _applied;
    final next = _settings.value;
    _applied = next;
    final boardChanged = prev.activeUrl != next.activeUrl || prev.device.tokenKey != next.device.tokenKey;
    if (boardChanged) {
      _state = null;
      _inventory = null;
      _liveState = null;
      _online = false;
      _loaded = false;
      _error = null;
      _failures = 0;
      _samples.clear();
      _lastSample = null;
      notifyListeners();
      unawaited(_loadToken().then((_) => refresh()));
    }
    if (prev.pollSeconds != next.pollSeconds) _startTimer();
  }

  @override
  void dispose() {
    _disposed = true;
    _timer?.cancel();
    _settings.removeListener(_onSettings);
    WidgetsBinding.instance.removeObserver(this);
    _client.close(force: true);
    super.dispose();
  }

  // -------------------------------------------------------------- polling
  @override
  Future<void> refresh() async {
    if (_refreshing || _disposed) return;
    _refreshing = true;
    final startedAt = DateTime.now();
    final urlAtStart = _settings.value.activeUrl;
    try {
      final json = await _call('GET', 'state');
      if (urlAtStart == _settings.value.activeUrl) {
        final Esp32State next;
        try {
          next = esp32StateFromRest(json);
        } on FormatException {
          throw ApiException(ApiErrorKind.firmware);
        }
        // An answer that left before our latest write may still contain the old value, so it
        // must not overwrite the optimistic state.
        if (!startedAt.isBefore(_lastWrite)) {
          _state = next;
          _inventory = BoardInventory.tryParse(json['inventory']);
          _liveState = BoardState.tryParse(json['state']);
          _recordSample(next);
        }
        _online = true;
        _failures = 0;
        _error = null;
        if (_settings.value.device.boardId.isEmpty && next.board.isNotEmpty) unawaited(_adoptBoardId(next.board));
      }
    } catch (e) {
      if (urlAtStart == _settings.value.activeUrl) {
        _failures++;
        _error = _friendly(e);
        if (_failures >= 2 || _state == null) _online = false;
      }
    } finally {
      _refreshing = false;
      if (urlAtStart == _settings.value.activeUrl) _loaded = true;
      if (!_disposed) notifyListeners();
    }
  }

  /// The app learns a board's id from its first answer and re-keys the stored token to it, so the
  /// board keeps working when its IP address changes.
  Future<void> _adoptBoardId(String id) async {
    if (_adopting) return;
    _adopting = true;
    try {
      final s = _settings.value;
      if (s.devices.isEmpty) return;
      final i = s.activeDevice.clamp(0, s.devices.length - 1).toInt();
      final dev = s.devices[i];
      if (dev.boardId.isNotEmpty) return;
      final tok = _token;
      if (tok != null) {
        await _store.setBoardToken(id, tok);
        await _store.deleteBoardToken(dev.tokenKey);
      }
      final list = [...s.devices];
      list[i] = dev.copyWith(boardId: id);
      await _settings.update(s.copyWith(devices: list));
    } finally {
      _adopting = false;
    }
  }

  void _recordSample(Esp32State s) {
    final now = DateTime.now();
    final last = _lastSample;
    if (last != null && now.difference(last) < const Duration(seconds: 15)) return;
    _lastSample = now;
    _samples.add({
      for (final d in s.devices)
        if (d.on && d.watts > 0) d.id: d.watts,
    });
    if (_samples.length > 20) _samples.removeAt(0);
  }

  // --------------------------------------------------------------- writes
  String _raw(String id) => splitCompositeId(id)?.device ?? id;

  /// [id] is the dashboard id ("`<board>`/`<device>`") or a bare device id.
  /// Returns null on success, otherwise a message that can be shown to the user.
  @override
  Future<String?> setDevice(String id, bool on) {
    final previous = _state?.devices.where((d) => d.id == id).map((d) => d.on).firstOrNull;
    return _write(
      'set',
      {'id': _raw(id), 'on': on},
      busyKey: id,
      optimistic: (s) => s.withDevice(id, on: on),
      revert: (s) => s.withDevice(id, on: previous ?? !on), // the real previous value, not just "!on"
    );
  }

  @override
  Future<String?> setDnd(bool on) {
    final previous = _state?.dnd;
    return _write(
      'dnd',
      {'on': on},
      busyKey: 'dnd',
      optimistic: (s) => s.withDnd(on),
      revert: (s) => s.withDnd(previous ?? !on),
    );
  }

  @override
  Future<String?> runScene(String id) => _write('scene', {'scene': id}, busyKey: 'scene:$id');

  // ------------------------------------------------------- device list
  @override
  Future<String?> addDevice({
    required String name,
    required String icon,
    required int pin,
    required String roomId,
    int? watts,
  }) =>
      _write(
        'device/add',
        {'name': name, 'icon': icon, 'pin': pin, 'room': roomId, 'watts': ?watts},
        busyKey: 'device:add',
      );

  @override
  Future<String?> updateDevice(
    String id, {
    String? name,
    String? icon,
    int? pin,
    String? roomId,
    int? watts,
  }) =>
      _write(
        'device/update',
        {
          'id': _raw(id),
          'name': ?name,
          'icon': ?icon,
          'pin': ?pin,
          'room': ?roomId,
          'watts': ?watts,
        },
        busyKey: id,
      );

  @override
  Future<String?> deleteDevice(String id) => _write('device/delete', {'id': _raw(id)}, busyKey: id);

  // ---------------------------------------------------------------- rooms
  @override
  Future<String?> upsertRoom(RoomInfo room) => _write(
        'room/upsert',
        {'id': room.id, 'name': room.name, 'rev': room.rev},
        busyKey: 'room:${room.id}',
      );

  @override
  Future<String?> deleteRoom(RoomInfo room) => _write(
        'room/delete',
        {'id': room.id, 'rev': room.rev},
        busyKey: 'room:${room.id}',
      );

  // --------------------------------------------------------------- Wi-Fi
  // These talk to the board directly (not through the polled state).

  /// Pass [rescan] on the first call, then call again (without it) until `scanning == false`.
  @override
  Future<ApiResult<WifiScan>> wifiScan({bool rescan = false}) async {
    try {
      if (rescan) await _call('POST', 'wifi/scan');
      final json = await _call('GET', 'wifi/scan');
      return ApiResult<WifiScan>.ok(WifiScan.fromJson(json));
    } catch (e) {
      return ApiResult<WifiScan>.fail(_friendly(e));
    }
  }

  @override
  Future<ApiResult<WifiStatus>> wifiStatus() async {
    try {
      final json = await _call('GET', 'wifi/status');
      return ApiResult<WifiStatus>.ok(WifiStatus.fromJson(json));
    } catch (e) {
      return ApiResult<WifiStatus>.fail(_friendly(e));
    }
  }

  /// Returns null when the request was accepted; the board then switches networks and rolls back
  /// to the old one if the new one does not work, so poll [wifiStatus].
  @override
  Future<String?> wifiConnect(String ssid, String password) async {
    try {
      final json = await _call('POST', 'wifi/connect', body: {'ssid': ssid, 'password': password});
      if (json['ok'] == false) return _tr.boardError(json['error']?.toString());
      return null;
    } catch (e) {
      return _friendly(e);
    }
  }

  /// Forgets the saved Wi-Fi. The board restarts into a recovery hotspot whose password is returned
  /// ONLY to this authenticated caller: it is kept in [recoveryCode] / [recoveryHotspot].
  @override
  Future<String?> wifiReset() async {
    try {
      final json = await _call('POST', 'wifi/reset');
      if (json['ok'] == false) return _tr.boardError(json['error']?.toString());
      recoveryCode = json['code']?.toString();
      recoveryHotspot = json['hotspot']?.toString();
      return null;
    } catch (e) {
      return _friendly(e);
    }
  }

  Future<String?> _write(
    String name,
    Map<String, dynamic> body, {
    required String busyKey,
    Esp32State Function(Esp32State s)? optimistic,
    Esp32State Function(Esp32State s)? revert,
  }) async {
    final before = _state;
    if (before == null) return _tr.notConnected;
    _pending.add(busyKey);
    if (optimistic != null) _state = optimistic(before);
    _lastWrite = DateTime.now();
    notifyListeners();
    try {
      final json = await _call('POST', name, body: body);
      if (json['ok'] == false) throw ApiException(ApiErrorKind.server, json['error']?.toString());
      _online = true;
      _failures = 0;
      _error = null;
      return null;
    } catch (e) {
      final current = _state;
      if (revert != null && current != null) _state = revert(current);
      return _friendly(e);
    } finally {
      _pending.remove(busyKey);
      _lastWrite = DateTime.now();
      if (!_disposed) notifyListeners();
      // Re-read the real state shortly after, so the UI always converges.
      unawaited(Future<void>.delayed(const Duration(milliseconds: 300), refresh));
    }
  }

  // ----------------------------------------------------------------- http
  // Try the saved IP first; if the board moved (DHCP), find it by its mDNS name.
  Future<String> _resolveUrl() async {
    final saved = _settings.value.activeUrl;
    if (await _reachable(saved)) return saved;
    final found = await _discoverByMdns();
    if (found != null && found != saved) {
      final s = _settings.value;
      final updated = [for (final d in s.devices) d.url == saved ? d.copyWith(url: found) : d];
      await _settings.update(s.copyWith(devices: updated));
      return found;
    }
    return saved;
  }

  Future<bool> _reachable(String url) async {
    try {
      await BoardHttp(baseUrl: url, token: _token, client: _client).call('GET', '/api/v2/state');
      return true;
    } catch (_) {
      return false;
    }
  }

  Future<String?> _discoverByMdns() async {
    final client = MDnsClient();
    try {
      await client.start();
      await for (final ip in client.lookup<IPAddressResourceRecord>(
          ResourceRecordQuery.addressIPv4('smarthome-282160.local'))) {
        return 'http://${ip.address.address}/';
      }
      return null;
    } catch (_) {
      return null;
    } finally {
      client.stop();
    }
  }

  Future<Map<String, dynamic>> _call(String method, String path, {Map<String, dynamic>? body}) async {
    final url = await _resolveUrl();
    final http = BoardHttp(baseUrl: url, token: _token, client: _client);
    return http.call(method, path, body: body);
  }

  String _friendly(Object e) => e is ApiException ? describeApiException(_tr, e) : _tr.errNetwork;
}

/// User-readable text for an API failure (shared by the dashboard path and the Boards screen).
String describeApiException(Tr tr, ApiException e) {
  final detail = e.detail;
  return switch (e.kind) {
    ApiErrorKind.auth => tr.errAuth,
    ApiErrorKind.notClaimed => tr.errNotClaimed,
    ApiErrorKind.rateLimited => tr.errRateLimited,
    ApiErrorKind.server => detail == null ? tr.errServer : tr.boardError(detail),
    ApiErrorKind.firmware => tr.errFirmware,
    ApiErrorKind.network => tr.errNetwork,
  };
}
