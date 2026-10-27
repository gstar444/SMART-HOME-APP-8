import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import 'mqtt_service.dart';
import 'secure_storage.dart';
import 'settings.dart';
import 'strings.dart';
import 'strings_cloud.dart';

/// Broker connection of the APP (HiveMQ Cloud, EMQX Cloud or a self-hosted broker).
///
/// There is deliberately no switch for plain MQTT or for skipping certificate checks: the
/// connection is always TLS and the broker certificate must verify. The password is kept in
/// secure storage only (never in shared_preferences).
class CloudSettingsPage extends StatefulWidget {
  const CloudSettingsPage({super.key});

  @override
  State<CloudSettingsPage> createState() => _CloudSettingsPageState();
}

class _CloudSettingsPageState extends State<CloudSettingsPage> {
  final TextEditingController _host = TextEditingController();
  final TextEditingController _port = TextEditingController(text: '8883');
  final TextEditingController _user = TextEditingController();
  final TextEditingController _pass = TextEditingController();
  final TextEditingController _home = TextEditingController(text: 'home');
  final TextEditingController _ca = TextEditingController();

  CloudProvider _provider = CloudProvider.hivemq;
  bool _showPass = false;
  bool _busy = false;
  bool _loaded = false;
  String? _message;
  bool _messageOk = false;

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    final saved = await context.read<SecureStore>().loadCloud();
    if (!mounted) return;
    setState(() {
      if (saved != null) {
        _provider = saved.provider;
        _host.text = saved.host;
        _port.text = '${saved.port}';
        _user.text = saved.username;
        _pass.text = saved.password;
        _home.text = saved.homeId;
        _ca.text = saved.customCaPem;
      }
      _loaded = true;
    });
  }

  @override
  void dispose() {
    for (final c in [_host, _port, _user, _pass, _home, _ca]) {
      c.dispose();
    }
    super.dispose();
  }

  CloudConfig _current() => CloudConfig(
        provider: _provider,
        host: _host.text.trim(),
        port: int.tryParse(_port.text.trim()) ?? 0,
        username: _user.text.trim(),
        password: _pass.text,
        homeId: _home.text.trim().toLowerCase(),
        customCaPem: _ca.text.trim(),
      );

  void _say(String text, {bool ok = false}) {
    if (!mounted) return;
    setState(() {
      _message = text;
      _messageOk = ok;
    });
  }

  Future<void> _test(Tr tr) async {
    final cfg = _current();
    setState(() => _busy = true);
    final err = await context.read<MqttService>().test(cfg);
    if (!mounted) return;
    setState(() => _busy = false);
    _say(err ?? tr.cloudTestOk, ok: err == null);
  }

  Future<void> _save(Tr tr) async {
    final cfg = _current();
    final problem = cfg.validate();
    if (problem != null) {
      _say(problem);
      return;
    }
    setState(() => _busy = true);
    final store = context.read<SecureStore>();
    final mqtt = context.read<MqttService>();
    await store.saveCloud(cfg);
    await mqtt.configure(cfg);
    if (!mounted) return;
    setState(() => _busy = false);
    _say(tr.cloudSaved, ok: true);
  }

  Future<void> _remove(Tr tr) async {
    final store = context.read<SecureStore>();
    final mqtt = context.read<MqttService>();
    final yes = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text(tr.cloudRemoveTitle),
        content: Text(tr.cloudRemoveBody),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx, false), child: Text(tr.cancel)),
          TextButton(onPressed: () => Navigator.pop(ctx, true), child: Text(tr.cloudRemove)),
        ],
      ),
    );
    if (yes != true) return;
    await store.clearCloud();
    await mqtt.configure(null);
    if (!mounted) return;
    setState(() {
      _host.clear();
      _user.clear();
      _pass.clear();
      _ca.clear();
      _port.text = '8883';
      _message = null;
    });
  }

  @override
  Widget build(BuildContext context) {
    final settings = context.watch<SettingsController>();
    final tr = Tr.of(settings.value);
    final mqtt = context.watch<MqttService>();
    final isCustom = _provider == CloudProvider.custom;

    return Scaffold(
      appBar: AppBar(title: Text(tr.cloudTitle)),
      body: !_loaded
          ? const Center(child: CircularProgressIndicator())
          : ListView(
              padding: const EdgeInsets.fromLTRB(16, 8, 16, 32),
              children: [
                _StatusTile(tr: tr, mqtt: mqtt),
                const SizedBox(height: 8),
                Text(tr.cloudSecurityNote, style: Theme.of(context).textTheme.bodySmall),
                const SizedBox(height: 16),
                DropdownButtonFormField<CloudProvider>(
                  initialValue: _provider,
                  decoration: InputDecoration(labelText: tr.cloudProvider),
                  items: [
                    for (final p in CloudProvider.values)
                      DropdownMenuItem(value: p, child: Text(tr.providerName(p))),
                  ],
                  onChanged: _busy
                      ? null
                      : (p) => setState(() {
                            _provider = p ?? _provider;
                            if (_provider != CloudProvider.custom) _port.text = '8883';
                          }),
                ),
                const SizedBox(height: 12),
                TextField(
                  controller: _host,
                  enabled: !_busy,
                  keyboardType: TextInputType.url,
                  autocorrect: false,
                  decoration: InputDecoration(labelText: tr.cloudHost, hintText: 'abc123.s1.eu.hivemq.cloud'),
                ),
                const SizedBox(height: 12),
                TextField(
                  controller: _port,
                  enabled: !_busy && isCustom,
                  keyboardType: TextInputType.number,
                  decoration: InputDecoration(labelText: tr.cloudPort, helperText: isCustom ? null : tr.cloudPortFixed),
                ),
                const SizedBox(height: 12),
                TextField(
                  controller: _user,
                  enabled: !_busy,
                  autocorrect: false,
                  decoration: InputDecoration(labelText: tr.cloudUsername, helperText: tr.cloudUsernameHelp),
                ),
                const SizedBox(height: 12),
                TextField(
                  controller: _pass,
                  enabled: !_busy,
                  obscureText: !_showPass,
                  autocorrect: false,
                  enableSuggestions: false,
                  decoration: InputDecoration(
                    labelText: tr.cloudPassword,
                    suffixIcon: IconButton(
                      icon: Icon(_showPass ? Icons.visibility_off : Icons.visibility),
                      onPressed: () => setState(() => _showPass = !_showPass),
                    ),
                  ),
                ),
                const SizedBox(height: 12),
                TextField(
                  controller: _home,
                  enabled: !_busy,
                  autocorrect: false,
                  decoration: InputDecoration(labelText: tr.cloudHomeId, helperText: tr.cloudHomeIdHelp),
                ),
                if (isCustom) ...[
                  const SizedBox(height: 12),
                  TextField(
                    controller: _ca,
                    enabled: !_busy,
                    minLines: 3,
                    maxLines: 8,
                    autocorrect: false,
                    decoration: InputDecoration(labelText: tr.cloudCustomCa, helperText: tr.cloudCustomCaHelp),
                  ),
                ],
                if (_message != null) ...[
                  const SizedBox(height: 16),
                  Text(
                    _message!,
                    style: TextStyle(color: _messageOk ? Colors.green : Theme.of(context).colorScheme.error),
                  ),
                ],
                const SizedBox(height: 20),
                Wrap(
                  spacing: 12,
                  runSpacing: 8,
                  children: [
                    FilledButton.icon(
                      onPressed: _busy ? null : () => _save(tr),
                      icon: const Icon(Icons.cloud_done_outlined),
                      label: Text(tr.cloudSave),
                    ),
                    OutlinedButton.icon(
                      onPressed: _busy ? null : () => _test(tr),
                      icon: _busy
                          ? const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(strokeWidth: 2))
                          : const Icon(Icons.network_check),
                      label: Text(tr.cloudTest),
                    ),
                    TextButton.icon(
                      onPressed: _busy ? null : () => _remove(tr),
                      icon: const Icon(Icons.delete_outline),
                      label: Text(tr.cloudRemove),
                    ),
                  ],
                ),
              ],
            ),
    );
  }
}

class _StatusTile extends StatelessWidget {
  const _StatusTile({required this.tr, required this.mqtt});

  final Tr tr;
  final MqttService mqtt;

  @override
  Widget build(BuildContext context) {
    final phase = mqtt.phase;
    final color = switch (phase) {
      MqttPhase.connected => Colors.green,
      MqttPhase.connecting || MqttPhase.reconnecting => Colors.orange,
      MqttPhase.failed => Theme.of(context).colorScheme.error,
      MqttPhase.disabled => Theme.of(context).disabledColor,
    };
    return Card(
      child: ListTile(
        leading: Icon(Icons.cloud_outlined, color: color),
        title: Text(tr.cloudPhase(phase)),
        subtitle: Text(
          phase == MqttPhase.connected
              ? tr.cloudBoardsSeen(mqtt.boards.length)
              : (mqtt.error ?? tr.cloudNotConfigured),
        ),
      ),
    );
  }
}
