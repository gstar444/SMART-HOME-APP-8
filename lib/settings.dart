import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';

const String defaultEsp32Url = 'http://192.168.1.37/';
/// Address of the ESP32's own setup hotspot ("SmartHome-Setup").
const String setupEsp32Url = 'http://192.168.4.1/';
// Backwards-compatible constant used by the project's URL smoke test.
const String esp32Url = defaultEsp32Url;
const int defaultAccent = 0xFF2F7BFF;
const List<String> defaultRooms = <String>['Room 1', 'Room 2', 'Room 3'];

const String _prefsKey = 'settings_v2';

/// Turns user input such as "192.168.1.37" or "esp32.local" into a full URL.
/// Returns null when the text cannot be a valid http/https address.
String? normalizeUrl(String input) {
  var text = input.trim();
  if (text.isEmpty) return null;
  if (!text.contains('://')) text = 'http://$text';
  final uri = Uri.tryParse(text);
  if (uri == null || !uri.hasAuthority || uri.host.isEmpty) return null;
  if (uri.scheme != 'http' && uri.scheme != 'https') return null;
  return text;
}

class Device {
  const Device({required this.name, required this.url, this.boardId = ''});

  final String name;

  /// Local address of the board (http://`<ip>`). Only used for local control and provisioning.
  final String url;

  /// Board id (sh-xxxxxx) once the board has been claimed or recognised. '' = not known yet.
  final String boardId;

  /// Key of this board's access token in secure storage.
  String get tokenKey => boardId.isNotEmpty ? boardId : 'url:$url';

  Device copyWith({String? name, String? url, String? boardId}) =>
      Device(name: name ?? this.name, url: url ?? this.url, boardId: boardId ?? this.boardId);

  Map<String, dynamic> toJson() => {'name': name, 'url': url, if (boardId.isNotEmpty) 'boardId': boardId};

  factory Device.fromJson(Map<String, dynamic> json) => Device(
        name: (json['name'] as String?) ?? 'ESP32',
        url: (json['url'] as String?) ?? defaultEsp32Url,
        boardId: (json['boardId'] as String?) ?? '',
      );
}

class AppSettings {
  const AppSettings({
    this.title = 'SMART HOME',
    this.devices = const [Device(name: 'ESP32', url: defaultEsp32Url)],
    this.activeDevice = 0,
    this.themeMode = ThemeMode.dark,
    this.accent = defaultAccent,
    this.showToolbar = true,
    this.pageZoom = 100,
    this.autoRefreshSeconds = 0,
    this.timeoutSeconds = 15,
    this.javaScript = true,
    this.hindi = true,
    this.pollSeconds = 2,
    this.cameraUrl = '',
    this.rooms = defaultRooms,
    this.roomOf = const <String, int>{},
  });

  final String title;
  final List<Device> devices;
  final int activeDevice;
  final ThemeMode themeMode;
  final int accent;
  final bool showToolbar;
  final int pageZoom; // percent, 70..150
  final int autoRefreshSeconds; // 0 = off
  final int timeoutSeconds;
  final bool javaScript;
  final bool hindi;
  final int pollSeconds; // how often the dashboard asks the ESP32 for state

  /// Optional camera page/stream (for example an ESP32-CAM), chosen by the user in the app.
  /// Boards no longer supply it: a board must not be able to make the app open arbitrary pages.
  final String cameraUrl;
  /// Legacy (app 3.x): room tabs used to live only in the app. Since 4.0 rooms are stored on the
  /// boards and synchronised through MQTT/REST, so the dashboard no longer reads this.
  final List<String> rooms;
  final Map<String, int> roomOf; // legacy, unused since 4.0

  Device get device {
    if (devices.isEmpty) return const Device(name: 'ESP32', url: defaultEsp32Url);
    return devices[activeDevice.clamp(0, devices.length - 1).toInt()];
  }

  String get activeUrl => device.url;

  AppSettings copyWith({
    String? title,
    List<Device>? devices,
    int? activeDevice,
    ThemeMode? themeMode,
    int? accent,
    bool? showToolbar,
    int? pageZoom,
    int? autoRefreshSeconds,
    int? timeoutSeconds,
    bool? javaScript,
    bool? hindi,
    int? pollSeconds,
    String? cameraUrl,
    List<String>? rooms,
    Map<String, int>? roomOf,
  }) {
    return AppSettings(
      title: title ?? this.title,
      devices: devices ?? this.devices,
      activeDevice: activeDevice ?? this.activeDevice,
      themeMode: themeMode ?? this.themeMode,
      accent: accent ?? this.accent,
      showToolbar: showToolbar ?? this.showToolbar,
      pageZoom: pageZoom ?? this.pageZoom,
      autoRefreshSeconds: autoRefreshSeconds ?? this.autoRefreshSeconds,
      timeoutSeconds: timeoutSeconds ?? this.timeoutSeconds,
      javaScript: javaScript ?? this.javaScript,
      hindi: hindi ?? this.hindi,
      pollSeconds: pollSeconds ?? this.pollSeconds,
      cameraUrl: cameraUrl ?? this.cameraUrl,
      rooms: rooms ?? this.rooms,
      roomOf: roomOf ?? this.roomOf,
    );
  }

  Map<String, dynamic> toJson() => {
        'title': title,
        'devices': devices.map((d) => d.toJson()).toList(),
        'activeDevice': activeDevice,
        'themeMode': themeMode.index,
        'accent': accent,
        'showToolbar': showToolbar,
        'pageZoom': pageZoom,
        'autoRefreshSeconds': autoRefreshSeconds,
        'timeoutSeconds': timeoutSeconds,
        'javaScript': javaScript,
        'hindi': hindi,
        'pollSeconds': pollSeconds,
        'cameraUrl': cameraUrl,
        'rooms': rooms,
        'roomOf': roomOf,
      };

  factory AppSettings.fromJson(Map<String, dynamic> json) {
    const d = AppSettings();
    final rawDevices = json['devices'];
    final devices = <Device>[
      if (rawDevices is List)
        for (final item in rawDevices)
          if (item is Map) Device.fromJson(Map<String, dynamic>.from(item)),
    ];
    final rawRooms = json['rooms'];
    final rooms = <String>[
      if (rawRooms is List)
        for (final r in rawRooms)
          if (r is String && r.trim().isNotEmpty) r,
    ];
    final rawRoomOf = json['roomOf'];
    final roomOf = <String, int>{
      if (rawRoomOf is Map)
        for (final e in rawRoomOf.entries)
          if (e.key is String && e.value is int) e.key as String: e.value as int,
    };
    final modeIndex = (json['themeMode'] as int?) ?? d.themeMode.index;
    return AppSettings(
      title: (json['title'] as String?) ?? d.title,
      devices: devices.isEmpty ? d.devices : devices,
      activeDevice: (json['activeDevice'] as int?) ?? 0,
      themeMode: ThemeMode.values[modeIndex.clamp(0, ThemeMode.values.length - 1).toInt()],
      accent: (json['accent'] as int?) ?? d.accent,
      showToolbar: (json['showToolbar'] as bool?) ?? d.showToolbar,
      pageZoom: ((json['pageZoom'] as int?) ?? d.pageZoom).clamp(70, 150).toInt(),
      autoRefreshSeconds: (json['autoRefreshSeconds'] as int?) ?? d.autoRefreshSeconds,
      timeoutSeconds:
          ((json['timeoutSeconds'] as int?) ?? d.timeoutSeconds).clamp(5, 120).toInt(),
      javaScript: (json['javaScript'] as bool?) ?? d.javaScript,
      hindi: (json['hindi'] as bool?) ?? d.hindi,
      pollSeconds: ((json['pollSeconds'] as int?) ?? d.pollSeconds).clamp(1, 30).toInt(),
      cameraUrl: _validCameraUrl((json['cameraUrl'] as String?) ?? '') ? (json['cameraUrl'] as String) : '',
      rooms: rooms.isEmpty ? d.rooms : rooms,
      roomOf: roomOf,
    );
  }
}

/// Holds the settings, saves every change immediately and notifies listeners.
class SettingsController extends ChangeNotifier {
  SettingsController(this._prefs, this._value);

  final SharedPreferences _prefs;
  AppSettings _value;

  AppSettings get value => _value;

  static Future<SettingsController> load() async {
    final prefs = await SharedPreferences.getInstance();
    AppSettings settings = const AppSettings();
    final raw = prefs.getString(_prefsKey);
    if (raw != null) {
      try {
        final decoded = jsonDecode(raw) as Map<String, dynamic>;
        settings = AppSettings.fromJson(decoded);
        if (decoded.containsKey('apiKey')) {
          // App 3.x kept the firmware API key in plain shared_preferences. Re-save without it
          // (the 4.x firmware uses a different, per-board token held in secure storage).
          await prefs.setString(_prefsKey, jsonEncode(settings.toJson()));
        }
      } catch (_) {
        settings = const AppSettings();
      }
    } else {
      // Migrate values saved by the previous version of the app.
      final oldUrl = prefs.getString('esp32_url');
      final oldTitle = prefs.getString('app_title');
      final oldDark = prefs.getBool('dark_mode');
      final oldAccent = prefs.getInt('accent_color');
      settings = AppSettings(
        title: oldTitle ?? 'SMART HOME',
        devices: [Device(name: 'ESP32', url: oldUrl ?? defaultEsp32Url)],
        themeMode: oldDark == null
            ? ThemeMode.dark
            : (oldDark ? ThemeMode.dark : ThemeMode.light),
        accent: oldAccent ?? defaultAccent,
      );
    }
    return SettingsController(prefs, settings);
  }

  Future<void> update(AppSettings next) async {
    _value = next;
    notifyListeners();
    await _prefs.setString(_prefsKey, jsonEncode(next.toJson()));
  }

  Future<void> reset() => update(const AppSettings());

  /// Points the currently selected ESP32 entry at another address
  /// (used after the ESP32 got a new IP from a new router).
  Future<void> setActiveUrl(String url) {
    final s = _value;
    if (s.devices.isEmpty) {
      return update(s.copyWith(
        devices: [Device(name: 'ESP32', url: url)],
        activeDevice: 0,
      ));
    }
    final i = s.activeDevice.clamp(0, s.devices.length - 1).toInt();
    final list = [...s.devices];
    list[i] = list[i].copyWith(url: url);
    return update(s.copyWith(devices: list, activeDevice: i));
  }

  /// Adds (once) and selects an "ESP32 Setup" entry for the setup hotspot,
  /// so the app can talk to an ESP32 that has no router connection yet.
  Future<void> activateSetupDevice() {
    final s = _value;
    final i = s.devices.indexWhere((d) => Uri.tryParse(d.url)?.host == '192.168.4.1');
    if (i >= 0) return update(s.copyWith(activeDevice: i));
    return update(s.copyWith(
      devices: [...s.devices, const Device(name: 'ESP32 Setup', url: setupEsp32Url)],
      activeDevice: s.devices.length,
    ));
  }
}

/// A camera address must be an explicit http(s) URL (no other schemes such as file: or javascript:).
bool _validCameraUrl(String v) {
  if (v.isEmpty) return false;
  final u = Uri.tryParse(v);
  return u != null && (u.scheme == 'http' || u.scheme == 'https') && u.host.isNotEmpty;
}

/// Same rule for the settings screen (empty = no camera).
bool isAcceptableCameraUrl(String v) => v.isEmpty || _validCameraUrl(v);
