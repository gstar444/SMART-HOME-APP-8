import 'package:esp32_controller/device_icons.dart';
import 'package:esp32_controller/models.dart';
import 'package:esp32_controller/mqtt_models.dart';
import 'package:esp32_controller/settings.dart';
import 'package:esp32_controller/voice.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

void main() {
  test('default ESP32 URL is an HTTP URL with an IP host', () {
    final uri = Uri.parse(esp32Url);
    expect(uri.scheme, 'http');
    expect(uri.host, '192.168.1.37');
    expect(uri.path, '/');
  });

  group('normalizeUrl', () {
    test('adds http:// to a bare IP', () {
      expect(normalizeUrl('192.168.1.50'), 'http://192.168.1.50');
    });
    test('keeps https URLs', () {
      expect(normalizeUrl(' https://esp32.local/ '), 'https://esp32.local/');
    });
    test('rejects empty and unsupported input', () {
      expect(normalizeUrl(''), isNull);
      expect(normalizeUrl('ftp://192.168.1.1'), isNull);
    });
  });

  test('settings survive a JSON round trip', () {
    const original = AppSettings(
      title: 'My Home',
      devices: [
        Device(name: 'Hall', url: 'http://192.168.1.10/'),
        Device(name: 'Garden', url: 'http://192.168.1.11/'),
      ],
      activeDevice: 1,
      themeMode: ThemeMode.dark,
      accent: 0xFF2563EB,
      showToolbar: false,
      pageZoom: 120,
      autoRefreshSeconds: 30,
      timeoutSeconds: 10,
      javaScript: false,
      hindi: false,
    );
    final copy = AppSettings.fromJson(original.toJson());
    expect(copy.title, 'My Home');
    expect(copy.devices.length, 2);
    expect(copy.activeUrl, 'http://192.168.1.11/');
    expect(copy.themeMode, ThemeMode.dark);
    expect(copy.accent, 0xFF2563EB);
    expect(copy.showToolbar, isFalse);
    expect(copy.pageZoom, 120);
    expect(copy.autoRefreshSeconds, 30);
    expect(copy.timeoutSeconds, 10);
    expect(copy.javaScript, isFalse);
    expect(copy.hindi, isFalse);
  });

  test('out-of-range saved values are clamped', () {
    final s = AppSettings.fromJson({'pageZoom': 999, 'activeDevice': 7});
    expect(s.pageZoom, 150);
    expect(s.activeUrl, defaultEsp32Url);
  });

  test('poll interval, board ids and the camera address survive a JSON round trip', () {
    const original = AppSettings(
      pollSeconds: 5,
      cameraUrl: 'http://192.168.1.50:81/stream',
      devices: [Device(name: 'Hall', url: 'http://192.168.1.10', boardId: 'sh-aabbcc')],
    );
    final copy = AppSettings.fromJson(original.toJson());
    expect(copy.pollSeconds, 5);
    expect(copy.cameraUrl, 'http://192.168.1.50:81/stream');
    expect(copy.device.boardId, 'sh-aabbcc');
    expect(copy.device.tokenKey, 'sh-aabbcc');
    expect(const Device(name: 'x', url: 'http://1.2.3.4').tokenKey, 'url:http://1.2.3.4');
  });

  test('a plaintext API key left by app 3.x is dropped, and unsafe camera addresses are ignored', () {
    final s = AppSettings.fromJson({'apiKey': 'old-secret', 'cameraUrl': 'javascript:alert(1)'});
    expect(s.toJson().containsKey('apiKey'), isFalse);
    expect(s.cameraUrl, isEmpty);
    expect(isAcceptableCameraUrl('file:///etc/passwd'), isFalse);
    expect(isAcceptableCameraUrl('https://cam.example/stream'), isTrue);
    expect(isAcceptableCameraUrl(''), isTrue);
  });

  group('firmware 4.x state document', () {
    Map<String, dynamic> restSample() => {
          'ok': true,
          'api': 2,
          'inventory': {
            'v': 1,
            'board': 'sh-aabbcc',
            'name': 'Hall board',
            'fw': '4.0.0',
            'chip': 'esp32',
            'psram': false,
            'cfgRev': 3,
            'maxDevices': 24,
            'rooms': [
              {'id': 'room1', 'name': 'Hall', 'rev': 1},
              {'id': 'room2', 'name': 'Bedroom', 'rev': 2},
            ],
            'devices': [
              {'id': 'light', 'name': 'Main Light', 'type': 'switch', 'group': 'Lighting', 'icon': 'light', 'room': 'room1', 'watts': 12, 'enabled': true, 'remote': true, 'pin': 23},
              {'id': 'door', 'name': 'Door', 'type': 'lock', 'group': 'Security', 'icon': 'lock', 'room': 'room1', 'remote': false, 'pin': 21},
              {'id': 'fan', 'name': 'Fan', 'type': 'switch', 'icon': 'fan', 'room': 'room2', 'pin': 22, 'fault': 'flash'},
              {'id': '', 'name': 'broken entry without id'},
            ],
            'scenes': [
              {'id': 'alloff', 'name': 'All off'},
            ],
            'counts': {'schedules': 1, 'automations': 0},
            'pins': {
              'out': [13, 22, 23],
              'in': [34],
            },
          },
          'state': {
            'v': 1,
            'seq': 5,
            'ts': 0,
            'up': 120,
            'rssi': -52,
            'ip': '192.168.1.37',
            'dnd': false,
            'temp': 22.5,
            'powerW': 12.0,
            'cloud': 'disabled',
            'd': {
              'light': {'on': true, 'wh': 1.5},
              'door': {'on': true},
            },
          },
          'wifi': {'mode': 'station', 'ssid': 'Home', 'apSsid': ''},
        };

    test('parses the board, rooms, devices and live values', () {
      final st = esp32StateFromRest(restSample());
      expect(st.name, 'Hall board');
      expect(st.board, 'sh-aabbcc');
      expect(st.ssid, 'Home');
      expect(st.temp, 22.5);
      expect(st.hum, isNull);
      expect(st.mode, 'sta');
      expect(st.devices.length, 3); // the entry without an id is skipped
      final light = st.devices.first;
      expect(light.id, 'sh-aabbcc/light'); // dashboard ids are "<board>/<device>"
      expect(light.rawId, 'light');
      expect(light.on, isTrue);
      expect(light.watts, 12);
      expect(light.wh, 1.5);
      expect(light.pin, 23);
      expect(light.room, 0);
      expect(st.devices[2].room, 1);
      expect(st.pinsOut, [13, 22, 23]);
      expect(st.pinsIn, [34]);
    });

    test('locks, remote permission and unusable pins are carried through', () {
      final st = esp32StateFromRest(restSample());
      final door = st.devices[1];
      expect(door.isLock, isTrue);
      expect(door.controllable, isTrue);
      expect(door.on, isTrue);
      expect(door.remoteAllowed, isFalse);
      expect(st.devices[2].available, isFalse); // pin fault: shown, but never presented as usable
      expect(st.devices.first.available, isTrue);
    });

    test('withDevice flips only the chosen device', () {
      final st = esp32StateFromRest(restSample()).withDevice('sh-aabbcc/light', on: false);
      expect(st.devices.first.on, isFalse);
      expect(st.devices[1].on, isTrue);
    });

    test('documents that are not firmware 4.x answers are rejected', () {
      expect(() => esp32StateFromRest(<String, dynamic>{}), throwsFormatException);
      expect(
        () => esp32StateFromRest({
          'inventory': {'v': 2, 'board': 'sh-aabbcc'},
          'state': {'v': 1},
        }),
        throwsFormatException,
      );
      expect(
        () => esp32StateFromRest({
          'inventory': {'v': 1, 'board': 'bad/id'},
          'state': {'v': 1},
        }),
        throwsFormatException,
      );
    });
  });


  group('voice commands', () {
    const devices = [
      Esp32Device(
          id: 'light', name: 'Main Light', type: 'switch', group: 'Lighting',
          icon: 'light', room: 0, on: false),
      Esp32Device(
          id: 'fan', name: 'Ceiling Fan', type: 'switch', group: 'Climate',
          icon: 'fan', room: 0, on: false),
      Esp32Device(
          id: 'lock', name: 'Smart Lock', type: 'lock', group: 'Security',
          icon: 'lock', room: 0, on: true),
    ];

    test('English and Hindi device commands', () {
      expect(parseVoice('light on', devices)?.set, {'light': true});
      expect(parseVoice('पंखा बंद करो', devices)?.set, {'fan': false});
      expect(parseVoice('light and fan off', devices)?.set,
          {'light': false, 'fan': false});
    });

    test('scenes', () {
      expect(parseVoice('sab band', devices)?.scene, 'alloff');
      expect(parseVoice('good night', devices)?.scene, 'goodnight');
    });

    test('a door can be locked but never unlocked by voice', () {
      expect(parseVoice('lock the door', devices)?.set, {'lock': true});
      final blocked = parseVoice('unlock the door', devices);
      expect(blocked?.lockBlocked, isTrue);
      expect(blocked?.set, isEmpty);
    });

    test('unknown or incomplete sentences return null', () {
      expect(parseVoice('hello there', devices), isNull);
      expect(parseVoice('light', devices), isNull);
    });
  });

  group('device list and Wi-Fi (firmware 4.x)', () {
    test('a setup-mode board reports its hotspot and is not on Wi-Fi', () {
      final st = esp32StateFromRest({
        'inventory': {
          'v': 1,
          'board': 'sh-aabbcc',
          'name': 'New board',
          'devices': <dynamic>[],
          'pins': {
            'out': [5, 13, 23],
            'in': [34],
          },
        },
        'state': {'v': 1, 'ip': '192.168.4.1'},
        'wifi': {'mode': 'setup', 'apSsid': 'SmartHome-AABBCC'},
      });
      expect(st.apActive, isTrue);
      expect(st.wifiConnected, isFalse);
      expect(st.apSsid, 'SmartHome-AABBCC');
      expect(st.pinsOut, [5, 13, 23]);
      expect(st.maxDevices, 24);
    });

    test('Wi-Fi mode names of the firmware map to the UI names', () {
      expect(wifiModeOf('station'), 'sta');
      expect(wifiModeOf('setup'), 'ap');
      expect(wifiModeOf('offline'), 'offline');
      expect(wifiModeOf(null), 'offline');
    });


    test('Wi-Fi scan and status answers parse', () {
      final scan = WifiScan.fromJson({
        'scanning': false,
        'networks': [
          {'ssid': 'चाय वाला', 'rssi': -48, 'secure': true},
          {'ssid': 'Cafe', 'rssi': -70, 'secure': false},
          {'ssid': '', 'rssi': -80, 'secure': true},
        ],
      });
      expect(scan.scanning, isFalse);
      expect(scan.networks.length, 2);
      expect(scan.networks.first.ssid, 'चाय वाला');
      expect(scan.networks[1].secure, isFalse);

      final status = WifiStatus.fromJson({
        'state': 'failed',
        'connected': false,
        'ssid': '',
        'ip': '192.168.4.1',
        'mode': 'ap',
        'error': 'could not connect',
        'saved': true,
      });
      expect(status.state, 'failed');
      expect(status.connected, isFalse);
      expect(status.saved, isTrue);
    });

    test('icons decide the device type', () {
      expect(typeForIcon('light'), 'switch');
      expect(typeForIcon('plug'), 'switch');
      expect(typeForIcon('lock'), 'lock');
      expect(typeForIcon('pir'), 'pir');
      expect(iconsForType('pir'), ['pir']);
      expect(iconsForType('switch'), contains('fan'));
      expect(allIcons, containsAll(['light', 'fan', 'lock', 'pir']));
    });

    test('a device called by its own name is the only one switched', () {
      const devices = [
        Esp32Device(
            id: 'd1', name: 'Bulb 1', type: 'switch', group: 'Lighting',
            icon: 'light', room: 0, on: false),
        Esp32Device(
            id: 'd2', name: 'Bulb 2', type: 'switch', group: 'Lighting',
            icon: 'light', room: 0, on: false),
      ];
      expect(parseVoice('bulb 2 on', devices)?.set, {'d2': true});
      expect(parseVoice('light on', devices)?.set, {'d1': true, 'd2': true});
    });

    test('the active ESP32 address can be changed and the setup address added', () async {
      TestWidgetsFlutterBinding.ensureInitialized();
      SharedPreferences.setMockInitialValues(<String, Object>{});
      final controller = await SettingsController.load();
      await controller.setActiveUrl('http://10.0.0.77/');
      expect(controller.value.activeUrl, 'http://10.0.0.77/');
      expect(controller.value.devices.length, 1);

      await controller.activateSetupDevice();
      expect(controller.value.devices.length, 2);
      expect(controller.value.activeUrl, setupEsp32Url);

      // asking again selects the same entry instead of adding another one
      await controller.activateSetupDevice();
      expect(controller.value.devices.length, 2);
    });
  });
}
