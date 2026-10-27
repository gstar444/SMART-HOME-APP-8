import 'dart:async';
import 'dart:convert';
import 'dart:math';

import 'package:flutter/foundation.dart';

import 'mqtt_models.dart';
import 'secure_storage.dart';

/// One message received from the broker.
class MqttInbound {
  const MqttInbound(this.topic, this.payload);
  final String topic;
  final String payload;
}

/// A connection problem with a message that can be shown to the user (never contains secrets).
class MqttTransportException implements Exception {
  const MqttTransportException(this.message);
  final String message;
  @override
  String toString() => message;
}

/// What [MqttService] needs from an MQTT client. The production implementation
/// (mqtt_transport.dart) does TLS with certificate verification; tests use a fake.
abstract class MqttTransport {
  Stream<MqttInbound> get messages;

  /// Emits false when an established connection drops.
  Stream<bool> get connection;
  bool get isConnected;

  /// Throws [MqttTransportException] on failure (unreachable, TLS verification failed, bad login...).
  Future<void> connect(CloudConfig config, String clientId);
  Future<void> subscribe(String topic);

  /// Publishes at QoS 1.
  Future<void> publish(String topic, String payload);
  Future<void> disconnect();
}

enum MqttPhase { disabled, connecting, connected, reconnecting, failed }

/// 5 s doubling up to 5 min, plus up to 20 % jitter (same curve as the firmware).
Duration reconnectDelay(int attempt, [Random? random]) {
  final r = random ?? Random();
  var baseMs = 5000 * (1 << (attempt > 6 ? 6 : attempt));
  if (baseMs > 300000) baseMs = 300000;
  return Duration(milliseconds: baseMs + r.nextInt(baseMs ~/ 5 + 1));
}

/// Keeps one broker session, mirrors every board's retained state/inventory and sends commands.
///
/// Security notes
///  * the transport verifies the broker certificate; this class never relaxes that;
///  * incoming documents are accepted only for the configured home, for a valid board id, with
///    protocol version 1, and an inventory must name the board whose topic it arrived on;
///  * at most [maxBoards] boards are tracked, so a flood of junk topics can not exhaust memory.
class MqttService extends ChangeNotifier {
  MqttService({
    required MqttTransport Function() transportFactory,
    DateTime Function()? clock,
    this._ackTimeout = const Duration(seconds: 5),
    Duration Function(int attempt)? delayFor,
    Random? random,
  })  : _factory = transportFactory,
        _clock = clock ?? DateTime.now,
        _delayFor = delayFor ?? reconnectDelay,
        _random = random ?? Random.secure();

  static const int maxBoards = 32;
  static const int maxPayloadBytes = 64 * 1024;

  final MqttTransport Function() _factory;
  final DateTime Function() _clock;
  final Duration _ackTimeout;
  final Duration Function(int attempt) _delayFor;
  final Random _random;

  MqttTransport? _transport;
  StreamSubscription<MqttInbound>? _msgSub;
  StreamSubscription<bool>? _connSub;
  Timer? _retryTimer;
  CloudConfig? _config;
  Topics _topics = const Topics('home');
  MqttPhase _phase = MqttPhase.disabled;
  String? _error;
  int _attempt = 0;
  int _generation = 0; // invalidates callbacks of an older session after configure()/dispose()
  bool _disposed = false;

  final Map<String, BoardView> _boards = <String, BoardView>{};
  final Map<String, _PendingCommand> _pending = <String, _PendingCommand>{};

  MqttPhase get phase => _phase;
  bool get connected => _phase == MqttPhase.connected;
  String? get error => _error;
  CloudConfig? get config => _config;
  Topics get topics => _topics;
  Map<String, BoardView> get boards => Map.unmodifiable(_boards);

  // -------------------------------------------------------------- session
  /// Starts (or restarts) the session with [config]; null or an incomplete config disconnects.
  Future<void> configure(CloudConfig? config) async {
    _generation++;
    _retryTimer?.cancel();
    await _teardown();
    _boards.clear();
    _failPending('not_connected');
    _config = config;
    _attempt = 0;
    _error = null;
    if (config == null || config.validate() != null) {
      _setPhase(MqttPhase.disabled);
      return;
    }
    _topics = Topics(config.homeId);
    await _connect(_generation);
  }

  /// Opens a throw-away session to check host, TLS certificate and login. Returns null on success.
  Future<String?> test(CloudConfig config) async {
    final problem = config.validate();
    if (problem != null) return problem;
    final t = _factory();
    try {
      await t.connect(config, _clientId('test'));
      return null;
    } on MqttTransportException catch (e) {
      return e.message;
    } catch (_) {
      return 'Could not connect.';
    } finally {
      try {
        await t.disconnect();
      } catch (_) {
        // nothing to clean up
      }
    }
  }

  String _clientId(String kind) {
    final suffix = List<int>.generate(5, (_) => _random.nextInt(256))
        .map((b) => b.toRadixString(16).padLeft(2, '0'))
        .join();
    return 'app-$kind-$suffix';
  }

  Future<void> _connect(int generation) async {
    final config = _config;
    if (config == null || _disposed) return;
    _setPhase(_attempt == 0 ? MqttPhase.connecting : MqttPhase.reconnecting);
    final t = _factory();
    try {
      await t.connect(config, _clientId('session'));
      if (generation != _generation || _disposed) {
        await t.disconnect();
        return;
      }
      _transport = t;
      _msgSub = t.messages.listen(_onMessage);
      _connSub = t.connection.listen((up) {
        if (!up && generation == _generation) _onLost(generation);
      });
      for (final topic in _topics.subscriptions) {
        await t.subscribe(topic);
      }
      _attempt = 0;
      _error = null;
      _setPhase(MqttPhase.connected);
    } on MqttTransportException catch (e) {
      await _failed(generation, e.message);
    } catch (_) {
      await _failed(generation, 'Could not connect to the broker.');
    }
  }

  Future<void> _failed(int generation, String message) async {
    if (generation != _generation || _disposed) return;
    _error = message;
    await _teardown();
    _scheduleRetry(generation);
  }

  void _onLost(int generation) {
    if (generation != _generation || _disposed) return;
    _error = 'Connection to the broker was lost.';
    _failPending('connection_lost');
    unawaited(_teardown().then((_) => _scheduleRetry(generation)));
  }

  void _scheduleRetry(int generation) {
    if (generation != _generation || _disposed) return;
    _setPhase(MqttPhase.failed);
    final delay = _delayFor(_attempt);
    _attempt++;
    _retryTimer?.cancel();
    _retryTimer = Timer(delay, () {
      if (generation == _generation && !_disposed) unawaited(_connect(generation));
    });
  }

  Future<void> _teardown() async {
    await _msgSub?.cancel();
    await _connSub?.cancel();
    _msgSub = null;
    _connSub = null;
    final t = _transport;
    _transport = null;
    if (t != null) {
      try {
        await t.disconnect();
      } catch (_) {
        // already gone
      }
    }
  }

  void _setPhase(MqttPhase p) {
    _phase = p;
    if (!_disposed) notifyListeners();
  }

  // ------------------------------------------------------------ receiving
  BoardView? _view(String id, {bool create = true}) {
    final existing = _boards[id];
    if (existing != null || !create) return existing;
    if (_boards.length >= maxBoards) return null;
    return _boards[id] = BoardView(id);
  }

  void _onMessage(MqttInbound m) {
    if (m.payload.length > maxPayloadBytes) return;
    final parsed = _topics.parse(m.topic);
    if (parsed == null) return;
    final now = _clock();
    switch (parsed.leaf) {
      case 'lwt':
        final text = m.payload.trim();
        if (text != 'online' && text != 'offline') return;
        final v = _view(parsed.board);
        if (v == null) return;
        v.lwtOnline = text == 'online';
      case 'state':
        final st = BoardState.tryParse(_decode(m.payload));
        if (st == null) return;
        final v = _view(parsed.board);
        if (v == null) return;
        v.state = st;
        v.stateAt = now;
      case 'inventory':
        final inv = BoardInventory.tryParse(_decode(m.payload));
        if (inv == null || inv.board != parsed.board) return; // a board may only describe itself
        final v = _view(parsed.board);
        if (v == null) return;
        v.inventory = inv;
        v.inventoryAt = now;
      case 'ack':
        final ack = CommandAck.tryParse(_decode(m.payload));
        if (ack == null) return;
        final p = _pending[ack.id];
        if (p != null && p.board == parsed.board && !p.completer.isCompleted) p.completer.complete(ack);
        return; // acks do not change what is displayed
      default:
        return; // cmd and anything else: not for us
    }
    if (!_disposed) notifyListeners();
  }

  Object? _decode(String payload) {
    try {
      return jsonDecode(payload);
    } catch (_) {
      return null;
    }
  }

  // -------------------------------------------------------------- commands
  /// Sends [body] (must contain "op") to [board] and waits for its acknowledgement.
  /// The command id makes the retry safe: the board runs a given id at most once.
  Future<CommandAck> send(String board, Map<String, dynamic> body) async {
    final t = _transport;
    if (t == null || !connected || !validBoardId(board)) {
      return const CommandAck(id: '', ok: false, error: 'not_connected');
    }
    final id = _clientCommandId();
    final payload = jsonEncode({'v': protocolVersion, 'id': id, ...body});
    final pending = _PendingCommand(board);
    _pending[id] = pending;
    try {
      for (var attempt = 0; attempt < 2; attempt++) {
        try {
          await t.publish(_topics.cmd(board), payload);
        } catch (_) {
          return CommandAck(id: id, ok: false, error: 'publish_failed');
        }
        try {
          return await pending.completer.future.timeout(_ackTimeout);
        } on TimeoutException {
          // retry once with the SAME id, then give up
        }
      }
      return CommandAck(id: id, ok: false, error: 'timeout');
    } finally {
      _pending.remove(id);
    }
  }

  String _clientCommandId() {
    final bytes = List<int>.generate(9, (_) => _random.nextInt(256));
    return base64Url.encode(bytes); // 12 chars of [A-Za-z0-9_-]: valid for the firmware (<= 24)
  }

  void _failPending(String reason) {
    for (final p in _pending.values) {
      if (!p.completer.isCompleted) p.completer.complete(CommandAck(id: '', ok: false, error: reason));
    }
  }

  @override
  void dispose() {
    _disposed = true;
    _generation++;
    _retryTimer?.cancel();
    _failPending('disposed');
    unawaited(_teardown());
    super.dispose();
  }
}

class _PendingCommand {
  _PendingCommand(this.board);
  final String board;
  final Completer<CommandAck> completer = Completer<CommandAck>();
}
