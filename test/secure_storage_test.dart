import 'dart:math';

import 'package:esp32_controller/secure_storage.dart';
import 'package:flutter_test/flutter_test.dart';

CloudConfig valid({CloudProvider provider = CloudProvider.hivemq}) => CloudConfig(
      provider: provider,
      host: 'abc123.s1.eu.hivemq.cloud',
      username: 'app-phone',
      password: 's3cret-pass',
      homeId: 'myhome',
    );

void main() {
  group('CloudConfig.validate', () {
    test('accepts a complete HiveMQ/EMQX configuration', () {
      expect(valid().validate(), isNull);
      expect(valid(provider: CloudProvider.emqx).validate(), isNull);
      expect(valid().isComplete, isTrue);
    });

    test('there is no way to configure plaintext MQTT for the cloud providers', () {
      expect(valid().copyWith(port: 1883).validate(), isNotNull);
      expect(valid(provider: CloudProvider.emqx).copyWith(port: 1883).validate(), isNotNull);
      expect(valid().copyWith(port: 8883).validate(), isNull);
    });

    test('a self-hosted broker may use another TLS port, but still needs a valid port', () {
      final custom = valid(provider: CloudProvider.custom);
      expect(custom.copyWith(port: 8884).validate(), isNull);
      expect(custom.copyWith(port: 0).validate(), isNotNull);
      expect(custom.copyWith(port: 70000).validate(), isNotNull);
    });

    test('hosts must be names, not IP addresses or junk', () {
      for (final bad in ['', '192.168.1.5', 'localhost', 'a b.com', 'host_name.com', '.hivemq.cloud', 'x']) {
        expect(valid().copyWith(host: bad).validate(), isNotNull, reason: bad);
      }
      expect(valid().copyWith(host: 'broker.example.org').validate(), isNull);
    });

    test('credentials and home id are required and the home id matches the topic rule', () {
      expect(valid().copyWith(username: ' ').validate(), isNotNull);
      expect(valid().copyWith(password: '').validate(), isNotNull);
      for (final bad in ['', 'My Home', 'a/b', 'a#', 'a+b', 'UPPER', 'x' * 33]) {
        expect(valid().copyWith(homeId: bad).validate(), isNotNull, reason: bad);
      }
      for (final good in ['home', 'my-home_2', 'x' * 32]) {
        expect(valid().copyWith(homeId: good).validate(), isNull, reason: good);
      }
    });

    test('an extra CA must be a certificate, never a private key', () {
      const cert = '-----BEGIN CERTIFICATE-----\nMIIB\n-----END CERTIFICATE-----';
      expect(valid(provider: CloudProvider.custom).copyWith(customCaPem: cert).validate(), isNull);
      expect(valid(provider: CloudProvider.custom).copyWith(customCaPem: 'hello').validate(), isNotNull);
      expect(
        valid(provider: CloudProvider.custom)
            .copyWith(customCaPem: '$cert\n-----BEGIN PRIVATE KEY-----\nabc\n-----END PRIVATE KEY-----')
            .validate(),
        isNotNull,
      );
    });

    test('toString never prints the password', () {
      expect(valid().toString(), isNot(contains('s3cret-pass')));
    });
  });

  group('SecureStore', () {
    test('cloud settings round trip through the secure backend only', () async {
      final backend = MemoryKeyValueStore();
      final store = SecureStore(backend);
      expect(await store.loadCloud(), isNull);
      await store.saveCloud(valid());
      final back = (await store.loadCloud())!;
      expect(back.host, 'abc123.s1.eu.hivemq.cloud');
      expect(back.password, 's3cret-pass');
      expect(back.provider, CloudProvider.hivemq);
      await store.clearCloud();
      expect(await store.loadCloud(), isNull);
      expect(backend.data, isEmpty);
    });

    test('a corrupted entry behaves like "not configured"', () async {
      final backend = MemoryKeyValueStore()..data['cloud_v1'] = '{not json';
      expect(await SecureStore(backend).loadCloud(), isNull);
    });

    test('board tokens are stored per board key and can be deleted', () async {
      final store = SecureStore(MemoryKeyValueStore());
      await store.setBoardToken('sh-aabbcc', 'tokenA');
      await store.setBoardToken('url:http://192.168.4.1', 'tokenB');
      expect(await store.boardToken('sh-aabbcc'), 'tokenA');
      expect(await store.boardToken('url:http://192.168.4.1'), 'tokenB');
      expect(await store.boardToken('sh-other'), isNull);
      await store.deleteBoardToken('sh-aabbcc');
      expect(await store.boardToken('sh-aabbcc'), isNull);
      expect(await store.boardToken('url:http://192.168.4.1'), 'tokenB');
    });

    test('generated tokens satisfy the firmware format and are unique', () {
      final pattern = RegExp(r'^[A-Za-z0-9_-]{32,128}$'); // what the board accepts
      final seen = <String>{};
      for (var i = 0; i < 50; i++) {
        final t = SecureStore.generateToken();
        expect(t, matches(pattern));
        expect(t.length, 43); // 32 random bytes, base64url without padding
        seen.add(t);
      }
      expect(seen.length, 50);
    });

    test('token generation is driven by the supplied random source', () {
      expect(SecureStore.generateToken(Random(1)), SecureStore.generateToken(Random(1)));
      expect(SecureStore.generateToken(Random(1)), isNot(SecureStore.generateToken(Random(2))));
    });
  });
}
