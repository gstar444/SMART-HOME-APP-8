import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'boards_page.dart';
import 'cloud_settings_page.dart';
import 'dashboard_widgets.dart';
import 'device_editor.dart';
import 'device_icons.dart';
import 'home_controller.dart';
import 'models.dart';
import 'mqtt_service.dart';
import 'settings.dart';
import 'settings_page.dart';
import 'strings.dart';
import 'strings_cloud.dart';
import 'voice.dart';
import 'web_page.dart';
import 'wifi_page.dart';

/// Order in which control groups are shown. Unknown groups follow after these.
const List<String> _groupOrder = ['Lighting', 'Climate', 'Security', 'Scenes'];

/// Everything needed to draw one tile.
class _Tile {
  _Tile({
    required this.group,
    required this.glyph,
    required this.label,
    this.caption,
    this.active = false,
    this.outline = false,
    this.busy = false,
    this.span = 1,
    this.onTap,
    this.onLongPress,
  });

  final String group;
  final GlyphBuilder glyph;
  final String label;
  final String? caption;
  final bool active;
  final bool outline;
  final bool busy;
  final int span;
  final VoidCallback? onTap;
  final VoidCallback? onLongPress;
}

GlyphBuilder _iconGlyph(IconData icon) =>
    (color, size) => Icon(icon, color: color, size: size);

GlyphBuilder _shapeGlyph(String kind) =>
    (color, size) => Glyph(kind, color: color, size: size);

class DashboardPage extends StatefulWidget {
  const DashboardPage({super.key, required this.settings, required this.esp});

  final SettingsController settings;
  final HomeController esp;

  @override
  State<DashboardPage> createState() => _DashboardPageState();
}

class _DashboardPageState extends State<DashboardPage> {
  int _room = 0;
  bool _listening = false;
  bool _showEnergy = true;

  SettingsController get _sc => widget.settings;
  AppSettings get _s => _sc.value;
  HomeController get _esp => widget.esp;

  // ------------------------------------------------------------- helpers
  void _snack(String message) {
    if (!mounted) return;
    final messenger = ScaffoldMessenger.of(context);
    messenger.hideCurrentSnackBar();
    messenger.showSnackBar(SnackBar(content: Text(message)));
  }

  /// Tab index of a device. Rooms are stored on the boards and merged by HomeController.
  int _roomOfDevice(Esp32Device d, List<String> rooms) => d.room.clamp(0, rooms.length - 1).toInt();

  void _openSettings() {
    Navigator.of(context).push(
      MaterialPageRoute<void>(
        builder: (_) => SettingsPage(controller: _sc, esp: _esp.local),
      ),
    );
  }

  void _openWifi() {
    Navigator.of(context).push(
      MaterialPageRoute<void>(
        builder: (_) => WifiPage(settings: _sc, esp: _esp.local),
      ),
    );
  }

  void _openBoards() {
    Navigator.of(context).push(MaterialPageRoute<void>(builder: (_) => const BoardsPage()));
  }

  void _openCloud() {
    Navigator.of(context).push(MaterialPageRoute<void>(builder: (_) => const CloudSettingsPage()));
  }

  // ------------------------------------------------------------- actions
  Future<void> _toggle(Esp32Device d) async {
    final tr = Tr.of(_s);
    if (!_esp.online) {
      _snack(tr.errNetwork);
      return;
    }
    if (d.isLock && d.on) {
      final ok = await showDialog<bool>(
        context: context,
        builder: (ctx) => AlertDialog(
          title: Text(tr.unlockQuestion(d.name)),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(ctx, false),
              child: Text(tr.cancel),
            ),
            FilledButton(
              onPressed: () => Navigator.pop(ctx, true),
              child: Text(tr.unlock),
            ),
          ],
        ),
      );
      if (ok != true) return;
    }
    HapticFeedback.selectionClick();
    final error = await _esp.setDevice(d.id, !d.on);
    if (error != null) _snack(error);
  }

  Future<void> _toggleDnd(Esp32State st) async {
    if (!_esp.online) {
      _snack(Tr.of(_s).errNetwork);
      return;
    }
    HapticFeedback.selectionClick();
    final error = await _esp.setDnd(!st.dnd);
    if (error != null) _snack(error);
  }

  Future<void> _scene(String id) async {
    final tr = Tr.of(_s);
    if (!_esp.online) {
      _snack(tr.errNetwork);
      return;
    }
    HapticFeedback.selectionClick();
    final error = await _esp.runScene(id);
    _snack(error ?? tr.sceneName(id));
  }

  void _openRoutines() {
    final tr = Tr.of(_s);
    showModalBottomSheet<void>(
      context: context,
      showDragHandle: true,
      builder: (ctx) => SafeArea(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Padding(
              padding: const EdgeInsets.fromLTRB(20, 0, 20, 8),
              child: Align(
                alignment: Alignment.centerLeft,
                child: Text(
                  tr.routinesTitle,
                  style: Theme.of(ctx).textTheme.titleMedium,
                ),
              ),
            ),
            for (final id in const ['alloff', 'goodnight', 'dinner'])
              ListTile(
                leading: Icon(
                  id == 'alloff'
                      ? Icons.power_settings_new
                      : (id == 'goodnight' ? Icons.bedtime : Icons.restaurant),
                ),
                title: Text(tr.sceneName(id)),
                onTap: () {
                  Navigator.pop(ctx);
                  _scene(id);
                },
              ),
            const SizedBox(height: 8),
          ],
        ),
      ),
    );
  }

  void _openCamera(Esp32State st) {
    final tr = Tr.of(_s);
    if (st.camera.isEmpty) {
      _snack(tr.cameraNotSet);
      return;
    }
    Navigator.of(context).push(
      MaterialPageRoute<void>(
        builder: (_) => SimpleWebScreen(title: tr.securityCam, url: st.camera),
      ),
    );
  }

  void _showTemp(Esp32State st) {
    final tr = Tr.of(_s);
    final t = st.temp;
    if (t == null) {
      _snack('${tr.tempSensor}: —');
      return;
    }
    final h = st.hum;
    _snack(
      '${tr.tempSensor}: ${t.toStringAsFixed(1)} °C'
      '${h == null ? '' : '  ·  ${tr.humidityText(h.round())}'}',
    );
  }

  // ------------------------------------------------- add / edit / remove
  Future<void> _addDevice({String? icon}) async {
    final tr = Tr.of(_s);
    final st = _esp.local.state; // pins and limits come from the board that will be wired
    if (st == null || !_esp.localOnline) {
      _snack(tr.localOnly);
      return;
    }
    if (st.devices.length >= st.maxDevices) {
      _snack(tr.ctlLimit(st.maxDevices));
      return;
    }
    final draft = await showDeviceEditor(
      context,
      tr: tr,
      state: st,
      rooms: _esp.roomNames,
      room: _room,
      presetIcon: icon,
    );
    if (draft == null || !mounted) return;
    final error = await _esp.addDevice(
      name: draft.name,
      icon: draft.icon,
      pin: draft.pin,
      room: draft.room,
      watts: draft.watts,
    );
    _snack(error ?? tr.ctlAdded);
  }

  Future<void> _editControl(Esp32Device d) async {
    final tr = Tr.of(_s);
    final st = _esp.local.state;
    final mine = _esp.localDevice(d); // the local copy knows the GPIO pin; the cloud copy does not
    if (st == null || mine == null || !_esp.localOnline) {
      _snack(tr.localOnly);
      return;
    }
    final draft = await showDeviceEditor(
      context,
      tr: tr,
      state: st,
      rooms: _esp.roomNames,
      room: _roomOfDevice(d, _esp.roomNames),
      editing: mine,
    );
    if (draft == null || !mounted) return;
    final error = await _esp.updateDevice(
      d.id,
      name: draft.name,
      icon: draft.icon,
      pin: draft.pin,
      room: draft.room,
      watts: draft.watts,
    );
    _snack(error ?? tr.ctlSaved);
  }

  Future<void> _deleteControl(Esp32Device d) async {
    final tr = Tr.of(_s);
    if (!_esp.localOnline) {
      _snack(tr.localOnly);
      return;
    }
    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text(tr.ctlDeleteQ(d.name)),
        content: Text(tr.ctlDeleteHelp),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx, false),
            child: Text(tr.cancel),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(ctx, true),
            child: Text(tr.delete),
          ),
        ],
      ),
    );
    if (ok != true || !mounted) return;
    final error = await _esp.deleteDevice(d.id);
    _snack(error ?? tr.ctlDeleted);
  }

  /// Long-press on a tile: edit (name, icon, GPIO), move room or remove.
  void _deviceMenu(Esp32Device d) {
    final tr = Tr.of(_s);
    showModalBottomSheet<void>(
      context: context,
      showDragHandle: true,
      builder: (ctx) => SafeArea(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            ListTile(
              leading: const Icon(Icons.edit_outlined),
              title: Text(tr.ctlMenuEdit),
              onTap: () {
                Navigator.pop(ctx);
                _editControl(d);
              },
            ),
            ListTile(
              leading: const Icon(Icons.drive_file_move_outlined),
              title: Text(tr.ctlMenuMove),
              onTap: () {
                Navigator.pop(ctx);
                _moveDevice(d);
              },
            ),
            ListTile(
              leading: const Icon(Icons.delete_outline),
              title: Text(tr.ctlMenuDelete),
              onTap: () {
                Navigator.pop(ctx);
                _deleteControl(d);
              },
            ),
          ],
        ),
      ),
    );
  }

  Future<void> _voice() async {
    if (_listening) return;
    final tr = Tr.of(_s);
    final st = _esp.state;
    setState(() => _listening = true);
    String? heard;
    try {
      heard = await listenOnce(hindi: _s.hindi, prompt: tr.voicePrompt);
    } on VoiceUnavailable {
      _snack(tr.voiceUnavailable);
    } finally {
      if (mounted) setState(() => _listening = false);
    }
    if (!mounted || heard == null || heard.trim().isEmpty) return;
    if (st == null || !_esp.online) {
      _snack(tr.errNetwork);
      return;
    }
    final action = parseVoice(heard, st.devices);
    if (action == null) {
      _snack('${tr.voiceHeard(heard)}\n${tr.voiceNoMatch}');
      return;
    }
    String? error;
    if (action.scene != null) {
      error = await _esp.runScene(action.scene!);
    } else {
      for (final entry in action.set.entries) {
        error = await _esp.setDevice(entry.key, entry.value) ?? error;
      }
      if (action.set.isEmpty && action.lockBlocked) {
        _snack(tr.voiceNoUnlock);
        return;
      }
    }
    _snack(error ?? tr.voiceDone(heard));
  }

  // --------------------------------------------------------------- rooms
  Future<String?> _askName(String title, String initial) {
    final tr = Tr.of(_s);
    final field = TextEditingController(text: initial);
    return showDialog<String>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text(title),
        content: TextField(
          controller: field,
          autofocus: true,
          decoration: InputDecoration(
            labelText: tr.roomName,
            border: const OutlineInputBorder(),
          ),
        ),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx), child: Text(tr.cancel)),
          FilledButton(
            onPressed: () => Navigator.pop(ctx, field.text),
            child: Text(tr.save),
          ),
        ],
      ),
    );
  }

  Future<void> _addRoom() async {
    final tr = Tr.of(_s);
    final name = await _askName(tr.addRoom, 'Room ${_esp.roomNames.length + 1}');
    if (name == null || name.trim().isEmpty) return;
    final error = await _esp.addRoom(name);
    if (error != null) {
      _snack(error);
      return;
    }
    // the new tab appears when the boards report it; jump to it as soon as it does
    if (mounted) setState(() => _room = _esp.roomNames.length);
  }

  Future<void> _renameRoom(int index) async {
    final tr = Tr.of(_s);
    final name = await _askName(tr.renameRoom, _esp.roomNames[index]);
    if (name == null || name.trim().isEmpty) return;
    final error = await _esp.renameRoom(index, name);
    if (error != null) _snack(error);
  }

  Future<void> _deleteRoom(int index) async {
    final tr = Tr.of(_s);
    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text(tr.deleteRoom),
        content: Text(_esp.roomNames[index]),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx, false),
            child: Text(tr.cancel),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(ctx, true),
            child: Text(tr.delete),
          ),
        ],
      ),
    );
    if (ok != true) return;
    final error = await _esp.deleteRoom(index);
    if (error != null) {
      _snack(error);
      return;
    }
    if (mounted) setState(() => _room = math.max(0, math.min(_room, _esp.roomNames.length - 2)));
  }

  void _roomMenu(int index) {
    final tr = Tr.of(_s);
    showModalBottomSheet<void>(
      context: context,
      showDragHandle: true,
      builder: (ctx) => SafeArea(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            ListTile(
              leading: const Icon(Icons.edit_outlined),
              title: Text(tr.renameRoom),
              onTap: () {
                Navigator.pop(ctx);
                _renameRoom(index);
              },
            ),
            if (_esp.roomNames.length > 1)
              ListTile(
                leading: const Icon(Icons.delete_outline),
                title: Text(tr.deleteRoom),
                onTap: () {
                  Navigator.pop(ctx);
                  _deleteRoom(index);
                },
              ),
          ],
        ),
      ),
    );
  }

  Future<void> _moveDevice(Esp32Device d) async {
    final tr = Tr.of(_s);
    final rooms = _esp.roomNames;
    final picked = await showDialog<int>(
      context: context,
      builder: (ctx) => SimpleDialog(
        title: Text(tr.moveToRoom),
        children: [
          for (var i = 0; i < rooms.length; i++)
            SimpleDialogOption(
              onPressed: () => Navigator.pop(ctx, i),
              child: Text(rooms[i]),
            ),
        ],
      ),
    );
    if (picked == null) return;
    final error = await _esp.moveDevice(d.id, picked);
    if (error != null) _snack(error);
  }

  // --------------------------------------------------------------- tiles
  List<_Tile> _buildTiles(Tr tr, Esp32State st, int room) {
    final tiles = <_Tile>[];

    for (final d in st.devices) {
      if (_roomOfDevice(d, _esp.roomNames) != room) continue;
      String caption;
      if (d.isLock) {
        caption = d.on ? tr.capLocked : tr.capUnlocked;
      } else if (d.type == 'pir') {
        caption = d.on ? tr.capMotion : tr.capClear;
      } else {
        caption = d.on ? tr.capOn : tr.capOff;
      }
      if (!d.available) caption = tr.capOffline;
      tiles.add(_Tile(
        group: d.group,
        glyph: glyphFor(d.icon, d.on && d.available),
        label: d.name,
        caption: caption,
        active: d.on && d.available,
        outline: d.icon == 'heater',
        span: d.isLock ? 2 : 1,
        busy: _esp.isBusy(d.id),
        onTap: d.controllable ? () => _toggle(d) : null,
        onLongPress: () => _deviceMenu(d),
      ));
    }

    // House-level tiles are shown in every room.
    final temp = st.temp;
    tiles.add(_Tile(
      group: 'Climate',
      glyph: (color, size) => Row(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(Icons.device_thermostat, color: color, size: size),
          Text(
            temp == null ? '—' : '${temp.round()}°C',
            style: TextStyle(fontSize: size * 0.3, color: color),
          ),
        ],
      ),
      label: tr.tempSensor,
      onTap: () => _showTemp(st),
    ));
    tiles.add(_Tile(
      group: 'Security',
      glyph: _iconGlyph(Icons.videocam),
      label: tr.securityCam,
      caption: st.camera.isEmpty ? '—' : tr.capView,
      onTap: () => _openCamera(st),
    ));
    tiles.add(_Tile(
      group: 'Scenes',
      glyph: _iconGlyph(Icons.bedtime),
      label: tr.dnd,
      caption: st.dnd ? tr.capOn : tr.capOff,
      active: st.dnd,
      busy: _esp.isBusy('dnd'),
      onTap: () => _toggleDnd(st),
    ));
    tiles.add(_Tile(
      group: 'Scenes',
      glyph: _iconGlyph(Icons.manage_history),
      label: tr.routines,
      onTap: _openRoutines,
    ));
    tiles.add(_Tile(
      group: 'Scenes',
      glyph: _shapeGlyph('candle'),
      label: tr.moodDinner,
      caption: tr.capRun,
      busy: _esp.isBusy('scene:dinner'),
      onTap: () => _scene('dinner'),
    ));
    return tiles;
  }

  Widget _tileGrid(
    List<_Tile> tiles,
    double tileWidth,
    Color accent,
    bool dim,
    bool wide,
    double usable,
  ) {
    final tr = Tr.of(_s);
    final groups = <String>[
      ..._groupOrder.where((g) => tiles.any((t) => t.group == g)),
      ...tiles
          .map((t) => t.group)
          .where((g) => !_groupOrder.contains(g))
          .toSet(),
    ];
    final boxes = <Widget>[];
    for (final g in groups) {
      final items = <_Tile>[
        ...tiles.where((t) => t.group == g),
        _Tile(
          group: g,
          glyph: _iconGlyph(Icons.add),
          label: tr.addControl,
          onTap: () => _addDevice(),
        ),
      ];
      final cells = items.fold<int>(0, (sum, t) => sum + t.span);
      final boxWidth = wide
          ? math.min(usable, cells * tileWidth + (cells - 1) * 12 + 26)
          : usable;
      boxes.add(
        GroupBox(
          title: tr.group(g),
          width: boxWidth,
          child: Wrap(
            spacing: 12,
            runSpacing: 16,
            alignment: WrapAlignment.center,
            children: [
              for (final t in items)
                DeviceTile(
                  width: tileWidth,
                  glyph: t.glyph,
                  label: t.label,
                  caption: t.caption,
                  accent: accent,
                  active: t.active,
                  outline: t.outline,
                  busy: t.busy,
                  span: t.span,
                  dim: dim && t.group != 'Scenes',
                  onTap: t.onTap,
                  onLongPress: t.onLongPress,
                ),
            ],
          ),
        ),
      );
    }
    return Wrap(spacing: 14, runSpacing: 16, children: boxes);
  }

  // ------------------------------------------------------------- sections
  Widget _topBar(Tr tr) {
    final s = _s;
    return Row(
      children: [
        ClipRRect(
          borderRadius: BorderRadius.circular(8),
          child: Image.asset(
            'assets/images/app_logo.png',
            width: 30,
            height: 30,
            cacheWidth: 90,
          ),
        ),
        const SizedBox(width: 10),
        Expanded(
          child: Text(
            s.title,
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: Theme.of(context)
                .textTheme
                .titleMedium
                ?.copyWith(fontWeight: FontWeight.w800, letterSpacing: 0.5),
          ),
        ),
        if (s.devices.length > 1)
          PopupMenuButton<int>(
            tooltip: tr.switchDevice,
            icon: const Icon(Icons.devices_other_outlined),
            onSelected: (i) => _sc.update(s.copyWith(activeDevice: i)),
            itemBuilder: (_) => [
              for (var i = 0; i < s.devices.length; i++)
                CheckedPopupMenuItem<int>(
                  value: i,
                  checked: i == s.activeDevice,
                  child: Text(s.devices[i].name),
                ),
            ],
          ),
        IconButton(
          visualDensity: VisualDensity.compact,
          tooltip: tr.wifiSettings,
          icon: Icon(_esp.online ? Icons.wifi : Icons.wifi_off),
          onPressed: _openWifi,
        ),
        IconButton(
          visualDensity: VisualDensity.compact,
          tooltip: tr.refresh,
          icon: const Icon(Icons.refresh),
          onPressed: _esp.refresh,
        ),
        IconButton(
          visualDensity: VisualDensity.compact,
          tooltip: tr.cloudIconTip,
          icon: Icon(switch (_esp.mqtt.phase) {
            MqttPhase.connected => Icons.cloud_done_outlined,
            MqttPhase.connecting || MqttPhase.reconnecting => Icons.cloud_sync_outlined,
            MqttPhase.failed => Icons.cloud_off_outlined,
            MqttPhase.disabled => Icons.cloud_outlined,
          }),
          onPressed: _openCloud,
        ),
        IconButton(
          visualDensity: VisualDensity.compact,
          tooltip: tr.settings,
          icon: const Icon(Icons.tune),
          onPressed: _openSettings,
        ),
      ],
    );
  }

  Widget _header(Tr tr, Esp32State? st, Color accent, bool dark, bool wide) {
    final online = _esp.online;
    final ssid = (st == null || st.ssid.isEmpty) ? '—' : st.ssid;
    final wifiCard = StatusCard(
      icon: Icons.memory,
      title: (st != null && st.apActive && !st.wifiConnected)
          ? tr.setupHotspotOn(st.apSsid)
          : '${tr.connectedTo}$ssid',
      subtitle: '${tr.status}${online ? tr.online : tr.offline}',
      ok: online,
      onTap: _openWifi,
    );
    final systemCard = StatusCard(
      icon: Icons.memory,
      title: tr.systemState,
      subtitle: online
          ? '${tr.status}${tr.online}${st == null ? '' : '  ·  ${st.rssi} dBm'}'
          : '${tr.status}${tr.offline}',
      ok: online,
    );
    final voice = VoiceButton(
      label: tr.voiceAssistant,
      accent: accent,
      listening: _listening,
      onTap: _voice,
    );
    final toggle = ThemeToggle(
      dark: dark,
      onChanged: (_) => _sc.update(
        _s.copyWith(themeMode: dark ? ThemeMode.light : ThemeMode.dark),
      ),
    );
    final clock = ClockCard(tr: tr, glow: accent);

    if (wide) {
      return Row(
        crossAxisAlignment: CrossAxisAlignment.center,
        children: [
          SizedBox(width: 210, child: clock),
          const SizedBox(width: 16),
          voice,
          const SizedBox(width: 16),
          Expanded(
            child: Column(
              children: [wifiCard, const SizedBox(height: 10), systemCard],
            ),
          ),
          const SizedBox(width: 16),
          toggle,
        ],
      );
    }
    return Column(
      children: [
        Row(
          crossAxisAlignment: CrossAxisAlignment.center,
          children: [
            Expanded(child: clock),
            const SizedBox(width: 10),
            voice,
            const SizedBox(width: 10),
            toggle,
          ],
        ),
        const SizedBox(height: 12),
        wifiCard,
        const SizedBox(height: 10),
        systemCard,
      ],
    );
  }

  Widget _roomBar(Tr tr, Color accent, int room) {
    final rooms = _esp.roomNames;
    final muted = Theme.of(context).colorScheme.onSurface.withValues(alpha: 0.62);
    return Row(
      children: [
        GestureDetector(
          onTap: () => setState(() => _showEnergy = !_showEnergy),
          child: Padding(
            padding: const EdgeInsets.all(2),
            child: Glyph(
              'floorplan',
              color: _showEnergy ? accent : muted,
              size: 34,
            ),
          ),
        ),
        const SizedBox(width: 12),
        Expanded(
          child: SingleChildScrollView(
            scrollDirection: Axis.horizontal,
            child: Row(
              children: [
                for (var i = 0; i < rooms.length; i++)
                  Padding(
                    padding: const EdgeInsets.only(right: 8),
                    child: RoomChip(
                      label: rooms[i],
                      selected: i == room,
                      accent: accent,
                      onTap: () => setState(() => _room = i),
                      onLongPress: () => _roomMenu(i),
                    ),
                  ),
              ],
            ),
          ),
        ),
        InkWell(
          borderRadius: BorderRadius.circular(12),
          onTap: _addRoom,
          child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                Icon(Icons.add, size: 24, color: muted),
                Text(tr.addRoom, style: TextStyle(fontSize: 10.5, color: muted)),
              ],
            ),
          ),
        ),
      ],
    );
  }

  /// "Add Bulb", "Add Fan" and "Add Device" shortcuts.
  Widget _addBar(Tr tr) {
    return Wrap(
      spacing: 8,
      runSpacing: 8,
      children: [
        FilledButton.tonalIcon(
          onPressed: () => _addDevice(icon: 'light'),
          icon: const Icon(Icons.lightbulb_outline),
          label: Text(tr.addBulb),
        ),
        FilledButton.tonalIcon(
          onPressed: () => _addDevice(icon: 'fan'),
          icon: const Icon(Icons.toys),
          label: Text(tr.addFan),
        ),
        FilledButton.tonalIcon(
          onPressed: () => _addDevice(),
          icon: const Icon(Icons.add),
          label: Text(tr.addAnyDevice),
        ),
      ],
    );
  }

  Widget _energy(Tr tr, Esp32State st, int room, Color accent) {
    final ids = <String>{};
    var watts = 0.0;
    var wh = 0.0;
    for (final d in st.devices) {
      if (!d.isSwitch || _roomOfDevice(d, _esp.roomNames) != room) continue;
      ids.add(d.id);
      if (d.on) watts += d.watts;
      wh += d.wh;
    }
    final series = <double>[
      for (final sample in _esp.samples)
        ids.fold<double>(0, (sum, id) => sum + (sample[id] ?? 0)),
    ];
    return EnergyCard(
      tr: tr,
      room: _esp.roomNames[room],
      kwh: wh / 1000,
      watts: watts,
      series: series,
      accent: accent,
    );
  }

  Widget _offlineBanner(Tr tr) {
    return GlassCard(
      padding: const EdgeInsets.fromLTRB(14, 8, 8, 8),
      child: Row(
        children: [
          const Icon(Icons.wifi_off, color: Color(0xFFEF4444)),
          const SizedBox(width: 10),
          Expanded(
            child: Text(
              _esp.error ?? tr.errNetwork,
              style: Theme.of(context).textTheme.bodySmall,
            ),
          ),
          TextButton(onPressed: _esp.refresh, child: Text(tr.retry)),
        ],
      ),
    );
  }

  Widget _emptyOffline(Tr tr) {
    return GlassCard(
      padding: const EdgeInsets.all(22),
      child: Column(
        children: [
          const Icon(Icons.wifi_off_rounded, size: 48, color: Color(0xFFEF4444)),
          const SizedBox(height: 12),
          Text(
            tr.cantReach,
            textAlign: TextAlign.center,
            style: Theme.of(context).textTheme.titleMedium,
          ),
          const SizedBox(height: 6),
          Text(
            _esp.error ?? tr.cantReachHelp,
            textAlign: TextAlign.center,
            style: Theme.of(context).textTheme.bodySmall,
          ),
          const SizedBox(height: 16),
          Wrap(
            spacing: 10,
            runSpacing: 8,
            alignment: WrapAlignment.center,
            children: [
              FilledButton.icon(
                onPressed: _esp.refresh,
                icon: const Icon(Icons.refresh),
                label: Text(tr.retry),
              ),
              OutlinedButton.icon(
                onPressed: _openWifi,
                icon: const Icon(Icons.wifi),
                label: Text(tr.wifiSettings),
              ),
              OutlinedButton.icon(
                onPressed: _openSettings,
                icon: const Icon(Icons.tune),
                label: Text(tr.openSettings),
              ),
              OutlinedButton.icon(
                onPressed: _openBoards,
                icon: const Icon(Icons.developer_board),
                label: Text(tr.boardsTitle),
              ),
            ],
          ),
        ],
      ),
    );
  }

  Widget _splash(Tr tr) {
    return Center(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          ClipRRect(
            borderRadius: BorderRadius.circular(24),
            child: Image.asset(
              'assets/images/app_logo.png',
              width: 112,
              height: 112,
              cacheWidth: 336,
            ),
          ),
          const SizedBox(height: 22),
          const SizedBox(
            width: 26,
            height: 26,
            child: CircularProgressIndicator(strokeWidth: 2.6),
          ),
          const SizedBox(height: 14),
          Text(tr.connecting),
        ],
      ),
    );
  }

  // ---------------------------------------------------------------- build
  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: Listenable.merge([_sc, _esp]),
      builder: (context, _) {
        final s = _s;
        final tr = Tr.of(s);
        final st = _esp.state;
        final accent = Color(s.accent);
        final dark = Theme.of(context).brightness == Brightness.dark;
        final room = _room.clamp(0, _esp.roomNames.length - 1).toInt();

        return AnnotatedRegion<SystemUiOverlayStyle>(
          value: dark ? SystemUiOverlayStyle.light : SystemUiOverlayStyle.dark,
          child: Scaffold(
            body: DecoratedBox(
              decoration: BoxDecoration(
                gradient: LinearGradient(
                  begin: Alignment.topCenter,
                  end: Alignment.bottomCenter,
                  colors: dark
                      ? const [Color(0xFF0C1117), Color(0xFF141B24)]
                      : const [Color(0xFFF5F8FC), Color(0xFFE8EEF7)],
                ),
              ),
              child: SafeArea(
                child: !_esp.loaded
                    ? _splash(tr)
                    : LayoutBuilder(
                        builder: (context, c) {
                          final width = math.min(c.maxWidth, 1000.0);
                          final usable = width - 32;
                          final wide = c.maxWidth >= 720;
                          final inner = usable - 26; // inside a group box
                          final cols = (inner / 76).floor().clamp(3, 6).toInt();
                          final tileWidth = wide ? 96.0 : (inner - (cols - 1) * 12) / cols;
                          return RefreshIndicator(
                            onRefresh: _esp.refresh,
                            child: Align(
                              alignment: Alignment.topCenter,
                              child: ConstrainedBox(
                                constraints: const BoxConstraints(maxWidth: 1000),
                                child: ListView(
                                  physics: const AlwaysScrollableScrollPhysics(),
                                  padding: const EdgeInsets.fromLTRB(16, 8, 16, 28),
                                  children: [
                                    _topBar(tr),
                                    const SizedBox(height: 8),
                                    _header(tr, st, accent, dark, wide),
                                    const SizedBox(height: 16),
                                    if (st == null)
                                      _emptyOffline(tr)
                                    else ...[
                                      if (!_esp.online) ...[
                                        _offlineBanner(tr),
                                        const SizedBox(height: 14),
                                      ],
                                      _roomBar(tr, accent, room),
                                      const SizedBox(height: 10),
                                      _addBar(tr),
                                      const SizedBox(height: 14),
                                      if (_showEnergy) ...[
                                      _energy(tr, st, room, accent),
                                      const SizedBox(height: 18),
                                      ],
                                      _tileGrid(
                                        _buildTiles(tr, st, room),
                                        tileWidth,
                                        accent,
                                        !_esp.online,
                                        wide,
                                        usable,
                                      ),
                                    ],
                                  ],
                                ),
                              ),
                            ),
                          );
                        },
                      ),
              ),
            ),
          ),
        );
      },
    );
  }
}
