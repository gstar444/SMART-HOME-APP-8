import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import 'boards_page.dart';
import 'cloud_settings_page.dart';
import 'esp32_api.dart';
import 'secure_storage.dart';
import 'settings.dart';
import 'strings.dart';
import 'strings_cloud.dart';
import 'wifi_page.dart';

const List<int> _accentChoices = <int>[
  0xFF2F7BFF, // dashboard blue (default)
  0xFF0F766E, // teal
  0xFF2563EB, // blue
  0xFF7C3AED, // violet
  0xFFC2410C, // orange
  0xFFBE123C, // rose
  0xFF4D7C0F, // olive green
  0xFF334155, // slate
  0xFF292720, // ink
];

const List<int> _pollChoices = <int>[1, 2, 3, 5, 10, 30];

class SettingsPage extends StatelessWidget {
  const SettingsPage({super.key, required this.controller, this.esp});

  final SettingsController controller;

  /// Needed for the Wi-Fi connection settings entry.
  final LocalBoard? esp;

  @override
  Widget build(BuildContext context) {
    final espController = esp;
    return ListenableBuilder(
      listenable: controller,
      builder: (context, _) {
        final s = controller.value;
        final tr = Tr.of(s);
        void change(AppSettings next) => controller.update(next);

        return Scaffold(
          appBar: AppBar(title: Text(tr.settings)),
          body: ListView(
            padding: const EdgeInsets.only(bottom: 32),
            children: [
              _Section(tr.sectionGeneral),
              ListTile(
                leading: const Icon(Icons.home_outlined),
                title: Text(tr.appName),
                subtitle: Text(s.title),
                onTap: () async {
                  final text = await _askText(context, tr, tr.appName, s.title);
                  if (text != null && text.trim().isNotEmpty) {
                    change(s.copyWith(title: text.trim()));
                  }
                },
              ),
              if (espController != null)
                ListTile(
                  leading: const Icon(Icons.wifi),
                  title: Text(tr.wifiSettings),
                  subtitle: Text(tr.wifiSettingsHelp),
                  trailing: const Icon(Icons.chevron_right),
                  onTap: () => Navigator.of(context).push(
                    MaterialPageRoute<void>(
                      builder: (_) => WifiPage(settings: controller, esp: espController),
                    ),
                  ),
                ),
              _Section(tr.sectionDevices),
              for (var i = 0; i < s.devices.length; i++)
                ListTile(
                  leading: Icon(
                    i == s.activeDevice
                        ? Icons.radio_button_checked
                        : Icons.radio_button_unchecked,
                    color: i == s.activeDevice
                        ? Theme.of(context).colorScheme.primary
                        : null,
                  ),
                  title: Text(s.devices[i].name),
                  subtitle: Text(s.devices[i].url),
                  onTap: () => change(s.copyWith(activeDevice: i)),
                  trailing: Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      IconButton(
                        tooltip: tr.editDevice,
                        icon: const Icon(Icons.edit_outlined),
                        onPressed: () async {
                          final edited =
                              await _editDevice(context, tr, s.devices[i]);
                          if (edited != null) {
                            final list = [...s.devices]..[i] = edited;
                            change(s.copyWith(devices: list));
                          }
                        },
                      ),
                      IconButton(
                        tooltip: tr.delete,
                        icon: const Icon(Icons.delete_outline),
                        onPressed: s.devices.length > 1
                            ? () {
                                context.read<SecureStore>().deleteBoardToken(s.devices[i].tokenKey);
                                final list = [...s.devices]..removeAt(i);
                                var active = s.activeDevice;
                                if (active >= list.length) active = list.length - 1;
                                if (i < active) active -= 1;
                                change(s.copyWith(devices: list, activeDevice: active));
                              }
                            : null,
                      ),
                    ],
                  ),
                ),
              ListTile(
                leading: const Icon(Icons.add_circle_outline),
                title: Text(tr.addDevice),
                onTap: () async {
                  final created = await _editDevice(context, tr, null);
                  if (created != null) {
                    change(s.copyWith(
                      devices: [...s.devices, created],
                      activeDevice: s.devices.length,
                    ));
                  }
                },
              ),
              ListTile(
                leading: const Icon(Icons.developer_board),
                title: Text(tr.boardsTitle),
                subtitle: Text(tr.boardsMenuHelp),
                trailing: const Icon(Icons.chevron_right),
                onTap: () => Navigator.of(context).push(
                  MaterialPageRoute<void>(builder: (_) => const BoardsPage()),
                ),
              ),
              ListTile(
                leading: const Icon(Icons.cloud_outlined),
                title: Text(tr.cloudTitle),
                subtitle: Text(tr.cloudMenuHelp),
                trailing: const Icon(Icons.chevron_right),
                onTap: () => Navigator.of(context).push(
                  MaterialPageRoute<void>(builder: (_) => const CloudSettingsPage()),
                ),
              ),
              _Section(tr.sectionAppearance),
              Padding(
                padding: const EdgeInsets.fromLTRB(16, 4, 16, 8),
                child: SegmentedButton<ThemeMode>(
                  segments: [
                    ButtonSegment(
                      value: ThemeMode.system,
                      icon: const Icon(Icons.brightness_auto_outlined),
                      label: Text(tr.themeSystem),
                    ),
                    ButtonSegment(
                      value: ThemeMode.light,
                      icon: const Icon(Icons.light_mode_outlined),
                      label: Text(tr.themeLight),
                    ),
                    ButtonSegment(
                      value: ThemeMode.dark,
                      icon: const Icon(Icons.dark_mode_outlined),
                      label: Text(tr.themeDark),
                    ),
                  ],
                  selected: {s.themeMode},
                  onSelectionChanged: (set) => change(s.copyWith(themeMode: set.first)),
                ),
              ),
              Padding(
                padding: const EdgeInsets.fromLTRB(16, 8, 16, 4),
                child: Text(tr.accent, style: Theme.of(context).textTheme.titleSmall),
              ),
              Padding(
                padding: const EdgeInsets.fromLTRB(16, 4, 16, 8),
                child: Wrap(
                  spacing: 12,
                  runSpacing: 10,
                  children: [
                    for (final value in _accentChoices)
                      InkWell(
                        customBorder: const CircleBorder(),
                        onTap: () => change(s.copyWith(accent: value)),
                        child: CircleAvatar(
                          radius: 20,
                          backgroundColor: Color(value),
                          child: s.accent == value
                              ? const Icon(Icons.check, color: Colors.white)
                              : null,
                        ),
                      ),
                  ],
                ),
              ),
              ListTile(
                leading: const Icon(Icons.palette_outlined),
                title: Text(tr.customColour),
                subtitle: Text(
                  '#${(s.accent & 0xFFFFFF).toRadixString(16).padLeft(6, '0').toUpperCase()}',
                ),
                onTap: () async {
                  final value = await _askHex(context, tr, s.accent);
                  if (value != null) change(s.copyWith(accent: value));
                },
              ),
              _Section(tr.sectionBehaviour),
              ListTile(
                leading: const Icon(Icons.speed_outlined),
                title: Text(tr.pollInterval),
                trailing: _IntDropdown(
                  value: s.pollSeconds,
                  items: _pollChoices,
                  label: tr.seconds,
                  onChanged: (v) => change(s.copyWith(pollSeconds: v)),
                ),
              ),
              ListTile(
                leading: const Icon(Icons.videocam_outlined),
                title: Text(tr.cameraUrlTitle),
                subtitle: Text(s.cameraUrl.isEmpty ? tr.cameraUrlNone : s.cameraUrl),
                onTap: () async {
                  final text = await _askText(context, tr, tr.cameraUrlTitle, s.cameraUrl, helper: tr.cameraUrlHelp);
                  if (text == null || !context.mounted) return;
                  final v = text.trim();
                  if (isAcceptableCameraUrl(v)) {
                    change(s.copyWith(cameraUrl: v));
                  } else {
                    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(tr.invalidUrl)));
                  }
                },
              ),
              _Section(tr.sectionLanguage),
              Padding(
                padding: const EdgeInsets.fromLTRB(16, 4, 16, 8),
                child: SegmentedButton<bool>(
                  segments: const [
                    ButtonSegment(value: true, label: Text('हिन्दी')),
                    ButtonSegment(value: false, label: Text('English')),
                  ],
                  selected: {s.hindi},
                  onSelectionChanged: (set) => change(s.copyWith(hindi: set.first)),
                ),
              ),
              _Section(tr.sectionAbout),
              Padding(
                padding: const EdgeInsets.symmetric(horizontal: 16),
                child: Text(tr.securityNote, style: Theme.of(context).textTheme.bodySmall),
              ),
              const SizedBox(height: 8),
              Padding(
                padding: const EdgeInsets.symmetric(horizontal: 16),
                child: Text(tr.settingsHint, style: Theme.of(context).textTheme.bodySmall),
              ),
              const SizedBox(height: 12),
              Padding(
                padding: const EdgeInsets.symmetric(horizontal: 16),
                child: Align(
                  alignment: Alignment.centerLeft,
                  child: TextButton.icon(
                    icon: const Icon(Icons.restore),
                    label: Text(tr.resetAll),
                    onPressed: () async {
                      final ok = await showDialog<bool>(
                        context: context,
                        builder: (ctx) => AlertDialog(
                          title: Text(tr.resetAll),
                          content: Text(tr.resetConfirm),
                          actions: [
                            TextButton(
                              onPressed: () => Navigator.pop(ctx, false),
                              child: Text(tr.cancel),
                            ),
                            FilledButton(
                              onPressed: () => Navigator.pop(ctx, true),
                              child: Text(tr.ok),
                            ),
                          ],
                        ),
                      );
                      if (ok == true) await controller.reset();
                    },
                  ),
                ),
              ),
            ],
          ),
        );
      },
    );
  }
}

class _Section extends StatelessWidget {
  const _Section(this.text);

  final String text;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.fromLTRB(16, 22, 16, 6),
      child: Text(
        text,
        style: Theme.of(context).textTheme.titleSmall?.copyWith(
              color: Theme.of(context).colorScheme.primary,
              fontWeight: FontWeight.w700,
            ),
      ),
    );
  }
}

class _IntDropdown extends StatelessWidget {
  const _IntDropdown({
    required this.value,
    required this.items,
    required this.label,
    required this.onChanged,
  });

  final int value;
  final List<int> items;
  final String Function(int) label;
  final ValueChanged<int> onChanged;

  @override
  Widget build(BuildContext context) {
    return DropdownButton<int>(
      value: items.contains(value) ? value : items.first,
      underline: const SizedBox.shrink(),
      items: [
        for (final item in items)
          DropdownMenuItem<int>(value: item, child: Text(label(item))),
      ],
      onChanged: (v) {
        if (v != null) onChanged(v);
      },
    );
  }
}

Future<String?> _askText(
  BuildContext context,
  Tr tr,
  String title,
  String initial, {
  String? helper,
  bool obscure = false,
}) {
  final field = TextEditingController(text: initial);
  return showDialog<String>(
    context: context,
    builder: (ctx) => AlertDialog(
      title: Text(title),
      content: TextField(
        controller: field,
        autofocus: true,
        obscureText: obscure,
        decoration: InputDecoration(
          helperText: helper,
          helperMaxLines: 3,
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

Future<int?> _askHex(BuildContext context, Tr tr, int current) {
  final field = TextEditingController(
    text: (current & 0xFFFFFF).toRadixString(16).padLeft(6, '0').toUpperCase(),
  );
  String? error;
  return showDialog<int>(
    context: context,
    builder: (ctx) => StatefulBuilder(
      builder: (ctx, refresh) => AlertDialog(
        title: Text(tr.customColour),
        content: TextField(
          controller: field,
          autofocus: true,
          maxLength: 6,
          textCapitalization: TextCapitalization.characters,
          decoration: InputDecoration(
            prefixText: '#',
            errorText: error,
            border: const OutlineInputBorder(),
          ),
        ),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx), child: Text(tr.cancel)),
          FilledButton(
            onPressed: () {
              final parsed = int.tryParse(field.text.trim(), radix: 16);
              if (field.text.trim().length != 6 || parsed == null) {
                refresh(() => error = tr.invalidHex);
                return;
              }
              Navigator.pop(ctx, 0xFF000000 | parsed);
            },
            child: Text(tr.save),
          ),
        ],
      ),
    ),
  );
}

Future<Device?> _editDevice(BuildContext context, Tr tr, Device? existing) {
  final name = TextEditingController(text: existing?.name ?? '');
  final url = TextEditingController(text: existing?.url ?? '');
  String? nameError;
  String? urlError;
  return showDialog<Device>(
    context: context,
    builder: (ctx) => StatefulBuilder(
      builder: (ctx, refresh) => AlertDialog(
        title: Text(existing == null ? tr.addDevice : tr.editDevice),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            TextField(
              controller: name,
              decoration: InputDecoration(
                labelText: tr.deviceName,
                errorText: nameError,
                border: const OutlineInputBorder(),
              ),
            ),
            const SizedBox(height: 12),
            TextField(
              controller: url,
              keyboardType: TextInputType.url,
              decoration: InputDecoration(
                labelText: tr.deviceUrl,
                hintText: tr.deviceUrlHint,
                errorText: urlError,
                border: const OutlineInputBorder(),
              ),
            ),
          ],
        ),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx), child: Text(tr.cancel)),
          FilledButton(
            onPressed: () {
              final normalized = normalizeUrl(url.text);
              final trimmedName = name.text.trim();
              if (trimmedName.isEmpty || normalized == null) {
                refresh(() {
                  nameError = trimmedName.isEmpty ? tr.nameRequired : null;
                  urlError = normalized == null ? tr.invalidUrl : null;
                });
                return;
              }
              Navigator.pop(
                ctx,
                existing?.copyWith(name: trimmedName, url: normalized) ?? Device(name: trimmedName, url: normalized),
              );
            },
            child: Text(tr.save),
          ),
        ],
      ),
    ),
  );
}
