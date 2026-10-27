import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:mqtt_client/mqtt_client.dart';
import 'package:mqtt_client/mqtt_server_client.dart';

import 'mqtt_service.dart';
import 'secure_storage.dart';

/// Production [MqttTransport]: MQTT 3.1.1 over TLS with full certificate verification.
///
///  * `secure = true` always; there is no plaintext path in this class;
///  * the context trusts the platform's root store (HiveMQ Cloud and EMQX Cloud present publicly
///    trusted certificates). A PEM from [CloudConfig.customCaPem] is ADDED as an extra root for a
///    self-hosted broker; it never disables verification;
///  * `onBadCertificate` always answers false, so a certificate that does not verify (wrong host,
///    expired, unknown CA, man-in-the-middle) aborts the handshake;
///  * the library's auto-reconnect is off: [MqttService] owns the retry/backoff policy.
class MqttClientTransport implements MqttTransport {
  MqttServerClient? _client;
  final StreamController<MqttInbound> _messages = StreamController<MqttInbound>.broadcast();
  final StreamController<bool> _connection = StreamController<bool>.broadcast();
  StreamSubscription<List<MqttReceivedMessage<MqttMessage>>>? _updates;
  bool _wasConnected = false;

  @override
  Stream<MqttInbound> get messages => _messages.stream;

  @override
  Stream<bool> get connection => _connection.stream;

  @override
  bool get isConnected => _client?.connectionStatus?.state == MqttConnectionState.connected;

  @override
  Future<void> connect(CloudConfig config, String clientId) async {
    final client = MqttServerClient.withPort(config.host.trim(), clientId, config.port);
    client.secure = true;
    final context = SecurityContext(withTrustedRoots: true);
    if (config.customCaPem.isNotEmpty) {
      try {
        context.setTrustedCertificatesBytes(utf8.encode(config.customCaPem));
      } catch (_) {
        throw const MqttTransportException('The custom CA certificate could not be read.');
      }
    }
    client.securityContext = context;
    client.onBadCertificate = (dynamic certificate) => false; // never accept an unverifiable certificate
    client.setProtocolV311();
    client.keepAlivePeriod = 30;
    client.connectTimeoutPeriod = 8000;
    client.autoReconnect = false;
    client.logging(on: false);
    client.onDisconnected = () {
      if (_wasConnected) {
        _wasConnected = false;
        if (!_connection.isClosed) _connection.add(false);
      }
    };
    client.connectionMessage = MqttConnectMessage()
        .withClientIdentifier(clientId)
        .authenticateAs(config.username, config.password)
        .startClean();

    try {
      final status = await client.connect();
      if (status == null || status.state != MqttConnectionState.connected) {
        client.disconnect();
        throw MqttTransportException(_describeReturnCode(status?.returnCode));
      }
    } on MqttTransportException {
      rethrow;
    } on HandshakeException {
      client.disconnect();
      throw const MqttTransportException(
          'The broker certificate could not be verified (wrong host, expired, or untrusted CA). '
          'The app refuses to connect.');
    } on TlsException {
      client.disconnect();
      throw const MqttTransportException('TLS error while connecting to the broker.');
    } on SocketException {
      client.disconnect();
      throw const MqttTransportException('The broker is unreachable. Check the host name and your internet connection.');
    } on NoConnectionException {
      client.disconnect();
      throw const MqttTransportException('The broker did not accept the connection (check host, username and password).');
    } catch (_) {
      client.disconnect();
      throw const MqttTransportException('Could not connect to the broker.');
    }

    _client = client;
    _wasConnected = true;
    _updates = client.updates?.listen((events) {
      for (final e in events) {
        final msg = e.payload;
        if (msg is MqttPublishMessage) {
          final text = MqttPublishPayload.bytesToStringAsString(msg.payload.message);
          if (!_messages.isClosed) _messages.add(MqttInbound(e.topic, text));
        }
      }
    });
  }

  String _describeReturnCode(MqttConnectReturnCode? code) {
    switch (code) {
      case MqttConnectReturnCode.badUsernameOrPassword:
        return 'The broker rejected the username or password.';
      case MqttConnectReturnCode.notAuthorized:
        return 'This account is not authorised on the broker (check its permissions).';
      case MqttConnectReturnCode.brokerUnavailable:
        return 'The broker is unavailable right now.';
      default:
        return 'The broker refused the connection.';
    }
  }

  @override
  Future<void> subscribe(String topic) async {
    _client?.subscribe(topic, MqttQos.atLeastOnce);
  }

  @override
  Future<void> publish(String topic, String payload) async {
    final c = _client;
    if (c == null || !isConnected) throw const MqttTransportException('Not connected.');
    final builder = MqttClientPayloadBuilder()..addString(payload);
    c.publishMessage(topic, MqttQos.atLeastOnce, builder.payload!);
  }

  @override
  Future<void> disconnect() async {
    _wasConnected = false; // an intentional disconnect is not a "lost connection"
    await _updates?.cancel();
    _updates = null;
    _client?.disconnect();
    _client = null;
  }
}
