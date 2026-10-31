/// Data classes shown by the dashboard. They are built from the firmware 4.x documents
/// (see mqtt_models.dart: the same inventory/state JSON arrives over MQTT and local REST).
library;

int? _int(Object? v) => v is num ? v.toInt() : null;

/// GPIO pins offered when a board does not report its own list. Firmware 4.x always reports the
/// list for ITS chip; this conservative fallback leaves out boot-signal pins (5, 14), strapping
/// pins and 16/17 (PSRAM on WROVER modules).
const List<int> defaultOutPins = <int>[
  13, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33,
];
const List<int> defaultInPins = <int>[34, 35, 36, 39];

class Esp32Device {
  const Esp32Device({
    required this.id,
    required this.name,
    required this.type,
    required this.group,
    required this.icon,
    required this.room,
    required this.on,
    this.watts = 0,
    this.wh = 0,
    this.pin = -1,
    this.boardId = '',
    this.available = true,
    this.remoteAllowed = true,
  });

  /// Dashboard-wide id: "`<board>`/`<device>`" (two boards may both have a "light").
  final String id;
  final String name;

  /// "switch" (relay), "lock" (on = locked) or "pir" (on = motion, read-only).
  final String type;
  final String group;
  final String icon;
  final int room;
  final bool on;
  final double watts; // typical load, used for the energy estimate
  final double wh; // energy used since the ESP32 booted
  final int pin; // GPIO pin, -1 = unknown (not sent over MQTT)
  final String boardId;

  /// False while the board is offline/stale or the device is disabled or on an unusable pin.
  final bool available;

  /// The device may be switched through the cloud (a lock can then also be unlocked remotely).
  final bool remoteAllowed;

  bool get isLock => type == 'lock';
  bool get isSwitch => type == 'switch';
  bool get controllable => type == 'switch' || type == 'lock';

  /// Id of the device on its own board (without the "`<board>`/" prefix).
  String get rawId {
    final i = id.indexOf('/');
    return i < 0 ? id : id.substring(i + 1);
  }

  Esp32Device copyWith({bool? on}) => Esp32Device(
        id: id,
        name: name,
        type: type,
        group: group,
        icon: icon,
        room: room,
        on: on ?? this.on,
        watts: watts,
        wh: wh,
        pin: pin,
        boardId: boardId,
        available: available,
        remoteAllowed: remoteAllowed,
      );
}

class Esp32State {
  const Esp32State({
    required this.name,
    required this.fw,
    required this.ssid,
    required this.ip,
    required this.rssi,
    required this.uptime,
    required this.temp,
    required this.hum,
    required this.dnd,
    required this.camera,
    required this.powerW,
    required this.devices,
    this.mode = 'sta',
    this.apSsid = '',
    this.pinsOut = defaultOutPins,
    this.pinsIn = defaultInPins,
    this.maxDevices = 24,
    this.board = '',
  });

  final String name;
  final String board; // board id (sh-xxxxxx), '' when unknown
  final String fw;
  final String ssid; // Wi-Fi the ESP32 has joined ('' = none)
  final String ip;
  final int rssi;
  final int uptime; // seconds
  final double? temp; // null = no sensor / read failed
  final double? hum;
  final bool dnd;
  final String camera; // stream URL, empty = not configured
  final double powerW;
  final List<Esp32Device> devices;

  /// "sta" (joined a router), "ap" (setup hotspot only), "ap+sta" or "offline".
  final String mode;
  final String apSsid; // name of the setup hotspot while it is on
  final List<int> pinsOut; // GPIO pins a relay can use
  final List<int> pinsIn; // extra input-only pins (PIR sensors)
  final int maxDevices;

  bool get wifiConnected => mode == 'sta' || mode == 'ap+sta';
  bool get apActive => mode == 'ap' || mode == 'ap+sta';

  Esp32State withDevice(String id, {required bool on}) => _copy(
        devices: [for (final d in devices) d.id == id ? d.copyWith(on: on) : d],
      );

  Esp32State withDnd(bool value) => _copy(dnd: value);

  Esp32State _copy({bool? dnd, List<Esp32Device>? devices}) => Esp32State(
        name: name,
        fw: fw,
        ssid: ssid,
        ip: ip,
        rssi: rssi,
        uptime: uptime,
        temp: temp,
        hum: hum,
        dnd: dnd ?? this.dnd,
        camera: camera,
        powerW: powerW,
        devices: devices ?? this.devices,
        mode: mode,
        apSsid: apSsid,
        pinsOut: pinsOut,
        pinsIn: pinsIn,
        maxDevices: maxDevices,
        board: board,
      );
}

/// Firmware 4.x names the modes "station" / "setup" / "offline"; the UI uses sta / ap / offline.
String wifiModeOf(Object? v) => switch (v) {
      'station' || 'sta' => 'sta',
      'setup' || 'ap' => 'ap',
      'ap+sta' => 'ap+sta',
      _ => 'offline',
    };

/// One Wi-Fi network found by the ESP32 scan.
class WifiNetwork {
  const WifiNetwork({
    required this.ssid,
    required this.rssi,
    required this.secure,
  });

  final String ssid;
  final int rssi; // dBm, closer to 0 = stronger
  final bool secure; // needs a password

  factory WifiNetwork.fromJson(Map<String, dynamic> j) => WifiNetwork(
        ssid: (j['ssid'] as String?) ?? '',
        rssi: _int(j['rssi']) ?? -100,
        secure: j['secure'] != false,
      );
}

/// Answer of GET /api/wifi/scan. The ESP32 scans in the background, so the
/// app asks again until [scanning] becomes false.
class WifiScan {
  const WifiScan({required this.scanning, required this.networks});

  final bool scanning;
  final List<WifiNetwork> networks;

  factory WifiScan.fromJson(Map<String, dynamic> j) {
    final raw = j['networks'];
    final list = <WifiNetwork>[
      if (raw is List)
        for (final item in raw)
          if (item is Map) WifiNetwork.fromJson(Map<String, dynamic>.from(item)),
    ].where((n) => n.ssid.isNotEmpty).toList();
    return WifiScan(scanning: j['scanning'] == true, networks: list);
  }
}

/// Answer of GET /api/wifi/status.
class WifiStatus {
  const WifiStatus({
    required this.state,
    required this.connected,
    required this.ssid,
    required this.ip,
    required this.rssi,
    required this.mode,
    required this.apSsid,
    required this.target,
    required this.error,
    required this.saved,
  });

  /// "idle", "trying" (joining a new network) or "failed" (last attempt failed).
  final String state;
  final bool connected;
  final String ssid;
  final String ip;
  final int rssi;
  final String mode;
  final String apSsid;
  final String target; // network being joined while state == "trying"
  final String error;
  final bool saved; // a Wi-Fi is saved in the ESP32's flash

  factory WifiStatus.fromJson(Map<String, dynamic> j) => WifiStatus(
        state: (j['state'] as String?) ?? 'idle',
        connected: j['connected'] == true,
        ssid: (j['ssid'] as String?) ?? '',
        ip: (j['ip'] as String?) ?? '',
        rssi: _int(j['rssi']) ?? 0,
        mode: wifiModeOf(j['mode']),
        apSsid: (j['apSsid'] as String?) ?? '',
        target: (j['target'] as String?) ?? '',
        error: (j['error'] as String?) ?? '',
        saved: j['saved'] == true,
      );
}
