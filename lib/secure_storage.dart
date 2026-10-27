import 'dart:convert';
import 'dart:math';

import 'package:flutter_secure_storage/flutter_secure_storage.dart';

/// Minimal key/value interface so the secure store can be faked in tests.
abstract class KeyValueStore {
  Future<String?> read(String key);
  Future<void> write(String key, String value);
  Future<void> delete(String key);
}

/// Production backend: Android Keystore-backed storage via flutter_secure_storage.
/// (No AndroidOptions are passed on purpose: the plugin's current defaults are the
/// secure ones, and the option names changed between plugin major versions.)
class PlatformKeyValueStore implements KeyValueStore {
  PlatformKeyValueStore() : _storage = const FlutterSecureStorage();

  final FlutterSecureStorage _storage;

  @override
  Future<String?> read(String key) => _storage.read(key: key);

  @override
  Future<void> write(String key, String value) => _storage.write(key: key, value: value);

  @override
  Future<void> delete(String key) => _storage.delete(key: key);
}

/// In-memory backend for tests.
class MemoryKeyValueStore implements KeyValueStore {
  final Map<String, String> data = <String, String>{};

  @override
  Future<String?> read(String key) async => data[key];

  @override
  Future<void> write(String key, String value) async => data[key] = value;

  @override
  Future<void> delete(String key) async => data.remove(key);
}

/// MQTT providers the app supports. All of them are TLS-only with a verified certificate chain.
enum CloudProvider { hivemq, emqx, custom }

CloudProvider providerFromName(String? name) => CloudProvider.values.firstWhere(
      (p) => p.name == name,
      orElse: () => CloudProvider.hivemq,
    );

final RegExp _hostPattern = RegExp(r'^(?=.{3,253}$)([A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?)(\.[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?)+$');
final RegExp _homePattern = RegExp(r'^[a-z0-9_-]{1,32}$');
final RegExp _ipv4Pattern = RegExp(r'^\d{1,3}(\.\d{1,3}){3}$');

/// Cloud (MQTT) connection settings of the *app*. The password lives only in secure storage.
class CloudConfig {
  const CloudConfig({
    this.provider = CloudProvider.hivemq,
    this.host = '',
    this.port = 8883,
    this.username = '',
    this.password = '',
    this.homeId = 'home',
    this.customCaPem = '',
  });

  final CloudProvider provider;
  final String host;
  final int port;
  final String username;
  final String password;

  /// Topic namespace; must equal the board's `mqtt.homeId` (topics: sh/v1/<homeId>/<board>/...).
  final String homeId;

  /// Optional PEM added to the trusted roots (self-hosted broker with a private CA, or a provider
  /// that changed its certificate chain). It ADDS a trusted root, it never disables verification.
  final String customCaPem;

  bool get isComplete => validate() == null;

  /// Returns a user-readable problem, or null when the settings are acceptable.
  /// There is deliberately no way to configure plaintext MQTT or to skip certificate checks.
  String? validate() {
    final h = host.trim();
    if (!_hostPattern.hasMatch(h) || _ipv4Pattern.hasMatch(h)) {
      return 'Enter the broker host name (for example abc123.s1.eu.hivemq.cloud), not an IP address.';
    }
    if (provider != CloudProvider.custom && port != 8883) {
      return 'HiveMQ Cloud and EMQX Cloud use TLS on port 8883.';
    }
    if (port < 1 || port > 65535) return 'Port must be between 1 and 65535.';
    if (username.trim().isEmpty) return 'Username is required.';
    if (password.isEmpty) return 'Password is required.';
    if (!_homePattern.hasMatch(homeId)) {
      return 'Home id: 1-32 characters, lower-case letters, digits, "-" or "_".';
    }
    if (customCaPem.isNotEmpty &&
        (!customCaPem.contains('-----BEGIN CERTIFICATE-----') || customCaPem.contains('PRIVATE KEY'))) {
      return 'The custom CA must be a PEM certificate (never a private key).';
    }
    return null;
  }

  CloudConfig copyWith({
    CloudProvider? provider,
    String? host,
    int? port,
    String? username,
    String? password,
    String? homeId,
    String? customCaPem,
  }) =>
      CloudConfig(
        provider: provider ?? this.provider,
        host: host ?? this.host,
        port: port ?? this.port,
        username: username ?? this.username,
        password: password ?? this.password,
        homeId: homeId ?? this.homeId,
        customCaPem: customCaPem ?? this.customCaPem,
      );

  Map<String, dynamic> toJson() => {
        'provider': provider.name,
        'host': host,
        'port': port,
        'username': username,
        'password': password,
        'homeId': homeId,
        'customCaPem': customCaPem,
      };

  factory CloudConfig.fromJson(Map<String, dynamic> j) => CloudConfig(
        provider: providerFromName(j['provider'] as String?),
        host: (j['host'] as String?) ?? '',
        port: (j['port'] as num?)?.toInt() ?? 8883,
        username: (j['username'] as String?) ?? '',
        password: (j['password'] as String?) ?? '',
        homeId: (j['homeId'] as String?) ?? 'home',
        customCaPem: (j['customCaPem'] as String?) ?? '',
      );

  @override
  String toString() => 'CloudConfig($provider, $host:$port, user=$username, password=<hidden>)';
}

/// Typed access to every secret the app keeps. Nothing here is written to shared_preferences.
class SecureStore {
  SecureStore([KeyValueStore? backend]) : _kv = backend ?? PlatformKeyValueStore();

  final KeyValueStore _kv;

  static const String _cloudKey = 'cloud_v1';
  static String _tokenKey(String boardKey) => 'board_token.$boardKey';

  Future<CloudConfig?> loadCloud() async {
    final raw = await _kv.read(_cloudKey);
    if (raw == null) return null;
    try {
      return CloudConfig.fromJson(jsonDecode(raw) as Map<String, dynamic>);
    } catch (_) {
      return null; // corrupted entry: behave as "not configured"
    }
  }

  Future<void> saveCloud(CloudConfig config) => _kv.write(_cloudKey, jsonEncode(config.toJson()));

  Future<void> clearCloud() => _kv.delete(_cloudKey);

  /// [boardKey] is the board id once known (sh-xxxxxx), otherwise "url:<address>".
  Future<String?> boardToken(String boardKey) => _kv.read(_tokenKey(boardKey));

  Future<void> setBoardToken(String boardKey, String token) => _kv.write(_tokenKey(boardKey), token);

  Future<void> deleteBoardToken(String boardKey) => _kv.delete(_tokenKey(boardKey));

  /// 32 random bytes from the platform CSPRNG, URL-safe Base64 without padding (43 characters).
  /// The firmware accepts 32..128 characters of [A-Za-z0-9_-] and stores only its SHA-256.
  static String generateToken([Random? random]) {
    final r = random ?? Random.secure();
    final bytes = List<int>.generate(32, (_) => r.nextInt(256));
    return base64Url.encode(bytes).replaceAll('=', '');
  }
}
