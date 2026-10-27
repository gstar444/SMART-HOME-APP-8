import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'dashboard_widgets.dart';
import 'device_icons.dart';
import 'models.dart';
import 'strings.dart';

/// The ESP32 stores a device name in 47 bytes (about 15 Hindi letters).
const int maxNameBytes = 47;

/// What the "Create Device" form returns.
class DeviceDraft {
  const DeviceDraft({
    required this.name,
    required this.icon,
    required this.pin,
    required this.room,
    this.watts,
  });

  final String name;
  final String icon;
  final int pin;
  final int room;
  final int? watts; // null = let the ESP32 pick a default for the icon
}

/// Opens the "Create Device" form (or the edit form when [editing] is given).
/// Returns null when the user cancels.
Future<DeviceDraft?> showDeviceEditor(
  BuildContext context, {
  required Tr tr,
  required Esp32State state,
  required List<String> rooms,
  required int room,
  Esp32Device? editing,
  String? presetIcon,
}) {
  return showDialog<DeviceDraft>(
    context: context,
    builder: (_) => _DeviceEditorDialog(
      tr: tr,
      state: state,
      rooms: rooms,
      room: room,
      editing: editing,
      presetIcon: presetIcon,
    ),
  );
}

class _DeviceEditorDialog extends StatefulWidget {
  const _DeviceEditorDialog({
    required this.tr,
    required this.state,
    required this.rooms,
    required this.room,
    this.editing,
    this.presetIcon,
  });

  final Tr tr;
  final Esp32State state;
  final List<String> rooms;
  final int room;
  final Esp32Device? editing;
  final String? presetIcon;

  @override
  State<_DeviceEditorDialog> createState() => _DeviceEditorDialogState();
}

class _DeviceEditorDialogState extends State<_DeviceEditorDialog> {
  late final TextEditingController _name;
  late final TextEditingController _watts;
  late String _icon;
  late int _room;
  int? _pin;
  bool _nameTouched = false;
  String? _nameError;
  String? _pinError;

  Tr get _tr => widget.tr;
  bool get _isEditing => widget.editing != null;

  @override
  void initState() {
    super.initState();
    final d = widget.editing;
    if (d != null) {
      final choices = iconsForType(d.type);
      _icon = choices.contains(d.icon) ? d.icon : choices.last;
    } else {
      _icon = widget.presetIcon ?? 'light';
    }
    _room = widget.room.clamp(0, widget.rooms.length - 1).toInt();
    _name = TextEditingController(text: d?.name ?? _suggestName(_icon));
    _watts = TextEditingController(
      text: (d != null && d.watts > 0) ? d.watts.round().toString() : '',
    );
    _pin = (d != null && d.pin >= 0) ? d.pin : _firstFreePin();
  }

  @override
  void dispose() {
    _name.dispose();
    _watts.dispose();
    super.dispose();
  }

  String _suggestName(String icon) {
    final n = widget.state.devices.where((d) => d.icon == icon).length + 1;
    return '${_tr.iconName(icon)} $n';
  }

  /// GPIO pin -> name of the device that already uses it.
  Map<int, String> get _usedPins {
    final map = <int, String>{};
    final myId = widget.editing?.id;
    for (final d in widget.state.devices) {
      if (d.pin < 0 || d.id == myId) continue;
      map[d.pin] = d.name;
    }
    return map;
  }

  /// A PIR sensor may also use the input-only pins (34, 35, 36, 39).
  List<int> get _pinChoices {
    final out = widget.state.pinsOut;
    if (typeForIcon(_icon) != 'pir') return out;
    final all = <int>{...out, ...widget.state.pinsIn}.toList()..sort();
    return all;
  }

  int? _firstFreePin() {
    final used = _usedPins;
    for (final p in _pinChoices) {
      if (!used.containsKey(p)) return p;
    }
    return null;
  }

  void _pickIcon(String key) {
    setState(() {
      _icon = key;
      if (!_isEditing && !_nameTouched) _name.text = _suggestName(key);
      final current = _pin;
      if (current == null || !_pinChoices.contains(current)) {
        _pin = _firstFreePin();
      }
    });
  }

  void _save() {
    final name = _name.text.trim();
    String? nameError;
    String? pinError;
    if (name.isEmpty) {
      nameError = _tr.nameRequired;
    } else if (utf8.encode(name).length > maxNameBytes) {
      nameError = _tr.nameTooLong;
    }
    final pin = _pin;
    if (pin == null) pinError = _tr.ctlNoPin;
    if (nameError != null || pinError != null || pin == null) {
      setState(() {
        _nameError = nameError;
        _pinError = pinError;
      });
      return;
    }
    final isSwitch = typeForIcon(_icon) == 'switch';
    final watts = int.tryParse(_watts.text.trim());
    Navigator.pop(
      context,
      DeviceDraft(
        name: name,
        icon: _icon,
        pin: pin,
        room: _room,
        watts: isSwitch ? watts : null,
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final used = _usedPins;
    final pins = _pinChoices;
    final icons = _isEditing ? iconsForType(widget.editing!.type) : allIcons;
    final labelStyle = Theme.of(context).textTheme.labelLarge;

    return AlertDialog(
      title: Text(_isEditing ? _tr.ctlEdit : _tr.ctlCreate),
      content: SingleChildScrollView(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            TextField(
              controller: _name,
              autofocus: !_isEditing,
              textCapitalization: TextCapitalization.sentences,
              onChanged: (_) => _nameTouched = true,
              decoration: InputDecoration(
                labelText: _tr.deviceName,
                errorText: _nameError,
                border: const OutlineInputBorder(),
              ),
            ),
            const SizedBox(height: 16),
            Text(_tr.ctlIcon, style: labelStyle),
            const SizedBox(height: 8),
            Wrap(
              spacing: 6,
              runSpacing: 8,
              children: [
                for (final key in icons)
                  _IconChoice(
                    glyph: glyphFor(key, true),
                    label: _tr.iconName(key),
                    selected: key == _icon,
                    onTap: () => _pickIcon(key),
                  ),
              ],
            ),
            const SizedBox(height: 16),
            InputDecorator(
              decoration: InputDecoration(
                labelText: _tr.ctlRoom,
                border: const OutlineInputBorder(),
                contentPadding:
                    const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
              ),
              child: DropdownButtonHideUnderline(
                child: DropdownButton<int>(
                  isExpanded: true,
                  value: _room,
                  items: [
                    for (var i = 0; i < widget.rooms.length; i++)
                      DropdownMenuItem<int>(
                        value: i,
                        child: Text(widget.rooms[i], overflow: TextOverflow.ellipsis),
                      ),
                  ],
                  onChanged: (v) {
                    if (v != null) setState(() => _room = v);
                  },
                ),
              ),
            ),
            const SizedBox(height: 16),
            InputDecorator(
              decoration: InputDecoration(
                labelText: _tr.ctlPin,
                errorText: _pinError,
                border: const OutlineInputBorder(),
                contentPadding:
                    const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
              ),
              child: DropdownButtonHideUnderline(
                child: DropdownButton<int>(
                  isExpanded: true,
                  value: pins.contains(_pin) ? _pin : null,
                  hint: Text(_tr.ctlNoPin),
                  items: [
                    for (final p in pins)
                      DropdownMenuItem<int>(
                        value: p,
                        enabled: !used.containsKey(p),
                        child: Text(
                          used.containsKey(p)
                              ? 'GPIO $p  ·  ${_tr.ctlPinUsed(used[p] ?? '')}'
                              : 'GPIO $p',
                          overflow: TextOverflow.ellipsis,
                        ),
                      ),
                  ],
                  onChanged: (v) {
                    if (v != null) {
                      setState(() {
                        _pin = v;
                        _pinError = null;
                      });
                    }
                  },
                ),
              ),
            ),
            if (typeForIcon(_icon) == 'switch') ...[
              const SizedBox(height: 16),
              TextField(
                controller: _watts,
                keyboardType: TextInputType.number,
                inputFormatters: [FilteringTextInputFormatter.digitsOnly],
                decoration: InputDecoration(
                  labelText: _tr.ctlWatts,
                  helperText: _tr.ctlWattsHelp,
                  border: const OutlineInputBorder(),
                ),
              ),
            ],
          ],
        ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.pop(context),
          child: Text(_tr.cancel),
        ),
        FilledButton(
          onPressed: _save,
          child: Text(_isEditing ? _tr.save : _tr.ctlCreate),
        ),
      ],
    );
  }
}

class _IconChoice extends StatelessWidget {
  const _IconChoice({
    required this.glyph,
    required this.label,
    required this.selected,
    required this.onTap,
  });

  final GlyphBuilder glyph;
  final String label;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final fg = selected
        ? scheme.onPrimary
        : scheme.onSurface.withValues(alpha: 0.75);
    return InkWell(
      borderRadius: BorderRadius.circular(14),
      onTap: onTap,
      child: SizedBox(
        width: 66,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Container(
              width: 48,
              height: 48,
              alignment: Alignment.center,
              decoration: BoxDecoration(
                color: selected ? scheme.primary : scheme.surfaceContainerHighest,
                borderRadius: BorderRadius.circular(14),
              ),
              child: glyph(fg, 24),
            ),
            const SizedBox(height: 4),
            Text(
              label,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              textAlign: TextAlign.center,
              style: Theme.of(context).textTheme.labelSmall,
            ),
          ],
        ),
      ),
    );
  }
}
