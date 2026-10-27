import 'dart:async';
import 'dart:convert';

import 'package:flutter/material.dart';

import 'dashboard_widgets.dart';
import 'esp32_api.dart';
import 'models.dart';
import 'settings.dart';
import 'strings.dart';
import 'strings_cloud.dart';

const Color _okColor = Color(0xFF22C55E);
const Color _warnColor = Color(0xFFF59E0B);
const Color _badColor = Color(0xFFEF4444);

IconData _signalIcon(int rssi) {
  if (rssi >= -60) return Icons.wifi;
  if (rssi >= -75) return Icons.wifi_2_bar;
  return Icons.wifi_1_bar;
}

/// "Wi-Fi connection settings": see the ESP32's connection, pick a new Wi-Fi,
/// reset it, and reach the ESP32's setup hotspot when it has no Wi-Fi at all.
class WifiPage extends StatefulWidget {
  const WifiPage({super.key, required this.settings, required this.esp});

  final SettingsController settings;
  final LocalBoard esp;

  @override
  State<WifiPage> createState() => _WifiPageState();
}

class _WifiPageState extends State<WifiPage> {
  bool _scanning = false;
  bool _scanned = false;
  String? _scanError;
  List<WifiNetwork> _networks = const <WifiNetwork>[];

  SettingsController get _sc => widget.settings;
  LocalBoard get _esp => widget.esp;
  Tr get _tr => Tr.of(_sc.value);

  void _snack(String message) {
    if (!mounted) return;
    final messenger = ScaffoldMessenger.of(context);
    messenger.hideCurrentSnackBar();
    messenger.showSnackBar(SnackBar(content: Text(message)));
  }

  // ------------------------------------------------------------------ scan
  Future<void> _scan() async {
    if (_scanning) return;
    final tr = _tr;
    setState(() {
      _scanning = true;
      _scanError = null;
    });
    String? error;
    List<WifiNetwork>? found;
    var res = await _esp.wifiScan(rescan: true);
    for (var attempt = 0; attempt < 10; attempt++) {
      if (!mounted) return;
      final scan = res.data;
      if (scan == null) {
        error = res.error;
        break;
      }
      if (!scan.scanning) {
        found = scan.networks;
        break;
      }
      await Future<void>.delayed(const Duration(milliseconds: 1500));
      res = await _esp.wifiScan();
    }
    if (!mounted) return;
    final networks = found;
    final message = error ?? tr.scanFailed;
    setState(() {
      _scanning = false;
      _scanned = true;
      if (networks != null) {
        _networks = networks;
      } else {
        _scanError = message;
      }
    });
  }

  // --------------------------------------------------------------- connect
  Future<void> _pick(WifiNetwork n) async {
    final creds = await showDialog<_Creds>(
      context: context,
      builder: (_) => _CredentialsDialog(tr: _tr, ssid: n.ssid, secure: n.secure),
    );
    if (creds == null || !mounted) return;
    await _connect(creds);
  }

  Future<void> _manual() async {
    final creds = await showDialog<_Creds>(
      context: context,
      builder: (_) => _CredentialsDialog(tr: _tr, ssid: null, secure: false),
    );
    if (creds == null || !mounted) return;
    await _connect(creds);
  }

  Future<void> _connect(_Creds creds) async {
    final error = await _esp.wifiConnect(creds.ssid, creds.pass);
    if (!mounted) return;
    if (error != null) {
      _snack(error);
      return;
    }
    await showDialog<void>(
      context: context,
      barrierDismissible: false,
      builder: (_) => _ProvisionDialog(
        esp: _esp,
        settings: _sc,
        tr: _tr,
        ssid: creds.ssid,
      ),
    );
  }

  // ----------------------------------------------------------------- reset
  Future<void> _reset() async {
    final tr = _tr;
    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text(tr.wifiReset),
        content: Text(tr.wifiResetHelp),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx, false),
            child: Text(tr.cancel),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(ctx, true),
            child: Text(tr.wifiReset),
          ),
        ],
      ),
    );
    if (ok != true || !mounted) return;
    final error = await _esp.wifiReset();
    if (!mounted) return;
    if (error != null) {
      _snack(error);
      return;
    }
    await showDialog<void>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text(tr.wifiResetDone),
        content: Text(
          '${tr.setupModeHelp}'
          '${_esp.recoveryCode == null ? '' : '\n\n${tr.recoveryBody(_esp.recoveryHotspot ?? '', _esp.recoveryCode!)}'}',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx),
            child: Text(tr.close),
          ),
          FilledButton(
            onPressed: () async {
              await _sc.activateSetupDevice();
              if (ctx.mounted) Navigator.pop(ctx);
            },
            child: Text(tr.useSetupAddress),
          ),
        ],
      ),
    );
  }

  Future<void> _useSetupAddress() async {
    await _sc.activateSetupDevice();
    _snack(_tr.setupAddressSet);
  }

  // ------------------------------------------------------------------- UI
  Widget _row(BuildContext context, String label, String value) {
    final theme = Theme.of(context);
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 3),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          SizedBox(
            width: 120,
            child: Text(
              label,
              style: theme.textTheme.bodySmall?.copyWith(
                color: theme.colorScheme.onSurface.withValues(alpha: 0.62),
              ),
            ),
          ),
          Expanded(
            child: Text(
              value,
              style: theme.textTheme.bodyMedium
                  ?.copyWith(fontWeight: FontWeight.w600),
            ),
          ),
        ],
      ),
    );
  }

  Widget _statusCard(BuildContext context, Tr tr, Esp32State? st) {
    final theme = Theme.of(context);

    final Color color;
    final IconData icon;
    final String title;
    var connected = false;
    if (st != null && _esp.online && st.wifiConnected) {
      connected = true;
      color = _okColor;
      icon = _signalIcon(st.rssi);
      title = '${tr.wifiConnectedLabel}: ${st.ssid}';
    } else if (st != null && _esp.online) {
      color = _warnColor;
      icon = Icons.wifi_find;
      title = tr.wifiNotConnected;
    } else {
      color = _badColor;
      icon = Icons.wifi_off;
      title = tr.espUnreachable;
    }

    return GlassCard(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(icon, color: color, size: 30),
              const SizedBox(width: 12),
              Expanded(
                child: Text(
                  title,
                  style: theme.textTheme.titleMedium
                      ?.copyWith(fontWeight: FontWeight.w800),
                ),
              ),
              Container(
                width: 10,
                height: 10,
                decoration: BoxDecoration(shape: BoxShape.circle, color: color),
              ),
            ],
          ),
          const SizedBox(height: 10),
          if (st != null) ...[
            _row(context, tr.networkName, st.ssid.isEmpty ? '—' : st.ssid),
            _row(context, tr.ipAddress, st.ip.isEmpty ? '—' : st.ip),
            if (connected) _row(context, tr.signal, '${st.rssi} dBm'),
          ],
          _row(context, tr.appAddress, _sc.value.activeUrl),
        ],
      ),
    );
  }

  Widget _sectionTitle(BuildContext context, String text) {
    return Padding(
      padding: const EdgeInsets.fromLTRB(4, 22, 4, 8),
      child: Text(
        text,
        style: Theme.of(context).textTheme.titleSmall?.copyWith(
              color: Theme.of(context).colorScheme.primary,
              fontWeight: FontWeight.w700,
            ),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: Listenable.merge([_sc, _esp]),
      builder: (context, _) {
        final tr = _tr;
        final st = _esp.state;
        final currentSsid = (st != null && st.wifiConnected) ? st.ssid : '';
        final dark = Theme.of(context).brightness == Brightness.dark;

        return Scaffold(
          appBar: AppBar(title: Text(tr.wifiSettings)),
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
              child: Align(
                alignment: Alignment.topCenter,
                child: ConstrainedBox(
                  constraints: const BoxConstraints(maxWidth: 640),
                  child: ListView(
                    padding: const EdgeInsets.fromLTRB(16, 8, 16, 32),
                    children: [
                      _statusCard(context, tr, st),
                      if (st != null && st.apActive) ...[
                        const SizedBox(height: 12),
                        GlassCard(
                          child: Row(
                            children: [
                              const Icon(Icons.settings_input_antenna,
                                  color: _warnColor),
                              const SizedBox(width: 12),
                              Expanded(
                                child: Text(
                                  '${tr.setupHotspotOn(st.apSsid)}\n${tr.setupHotspotNote}',
                                  style: Theme.of(context).textTheme.bodySmall,
                                ),
                              ),
                            ],
                          ),
                        ),
                      ],
                      _sectionTitle(context, tr.availableNetworks),
                      FilledButton.icon(
                        onPressed: _scanning ? null : _scan,
                        icon: _scanning
                            ? const SizedBox(
                                width: 18,
                                height: 18,
                                child: CircularProgressIndicator(strokeWidth: 2),
                              )
                            : const Icon(Icons.search),
                        label: Text(_scanning ? tr.scanning : tr.scanNetworks),
                      ),
                      const SizedBox(height: 8),
                      if (_scanError != null)
                        Padding(
                          padding: const EdgeInsets.symmetric(vertical: 8),
                          child: Text(
                            _scanError!,
                            style: Theme.of(context)
                                .textTheme
                                .bodySmall
                                ?.copyWith(color: _badColor),
                          ),
                        )
                      else if (_scanned && !_scanning && _networks.isEmpty)
                        Padding(
                          padding: const EdgeInsets.symmetric(vertical: 8),
                          child: Text(
                            tr.noNetworks,
                            style: Theme.of(context).textTheme.bodySmall,
                          ),
                        ),
                      for (final n in _networks)
                        Padding(
                          padding: const EdgeInsets.only(bottom: 8),
                          child: GlassCard(
                            padding: EdgeInsets.zero,
                            radius: 16,
                            onTap: () => _pick(n),
                            child: ListTile(
                              leading: Icon(_signalIcon(n.rssi)),
                              title: Text(n.ssid),
                              subtitle: Text(
                                n.ssid == currentSsid
                                    ? '${tr.wifiConnectedLabel}  ·  ${n.rssi} dBm'
                                    : '${n.rssi} dBm',
                              ),
                              trailing: Icon(
                                n.ssid == currentSsid
                                    ? Icons.check_circle
                                    : (n.secure
                                        ? Icons.lock_outline
                                        : Icons.lock_open_outlined),
                                size: 20,
                                color: n.ssid == currentSsid ? _okColor : null,
                              ),
                            ),
                          ),
                        ),
                      const SizedBox(height: 8),
                      OutlinedButton.icon(
                        onPressed: _manual,
                        icon: const Icon(Icons.edit_outlined),
                        label: Text(tr.otherNetwork),
                      ),
                      _sectionTitle(context, tr.setupModeTitle),
                      Padding(
                        padding: const EdgeInsets.symmetric(horizontal: 4),
                        child: Text(
                          tr.setupModeHelp,
                          style: Theme.of(context).textTheme.bodySmall,
                        ),
                      ),
                      const SizedBox(height: 10),
                      OutlinedButton.icon(
                        onPressed: _useSetupAddress,
                        icon: const Icon(Icons.settings_input_antenna),
                        label: Text(tr.useSetupAddress),
                      ),
                      _sectionTitle(context, tr.wifiReset),
                      Padding(
                        padding: const EdgeInsets.symmetric(horizontal: 4),
                        child: Text(
                          tr.wifiResetHelp,
                          style: Theme.of(context).textTheme.bodySmall,
                        ),
                      ),
                      const SizedBox(height: 10),
                      OutlinedButton.icon(
                        onPressed: _reset,
                        style: OutlinedButton.styleFrom(foregroundColor: _badColor),
                        icon: const Icon(Icons.restart_alt),
                        label: Text(tr.wifiReset),
                      ),
                    ],
                  ),
                ),
              ),
            ),
          ),
        );
      },
    );
  }
}

// ---------------------------------------------------------------- dialogs
class _Creds {
  const _Creds(this.ssid, this.pass);

  final String ssid;
  final String pass;
}

/// Asks for the password of a chosen network, or for name + password when
/// [ssid] is null ("other network").
class _CredentialsDialog extends StatefulWidget {
  const _CredentialsDialog({
    required this.tr,
    required this.ssid,
    required this.secure,
  });

  final Tr tr;
  final String? ssid;
  final bool secure;

  @override
  State<_CredentialsDialog> createState() => _CredentialsDialogState();
}

class _CredentialsDialogState extends State<_CredentialsDialog> {
  final TextEditingController _ssid = TextEditingController();
  final TextEditingController _pass = TextEditingController();
  bool _hidden = true;
  String? _ssidError;
  String? _passError;

  @override
  void dispose() {
    _ssid.dispose();
    _pass.dispose();
    super.dispose();
  }

  void _submit() {
    final tr = widget.tr;
    final fixed = widget.ssid;
    final ssid = fixed ?? _ssid.text.trim();
    final pass = _pass.text;

    String? ssidError;
    String? passError;
    if (ssid.isEmpty) {
      ssidError = tr.ssidRequired;
    } else if (utf8.encode(ssid).length > 32) {
      ssidError = tr.ssidTooLong;
    }
    if (pass.isEmpty) {
      // A network picked from the scan that is secured needs a password;
      // for a typed-in name an empty password means an open network.
      if (fixed != null && widget.secure) passError = tr.passwordRequired;
    } else if (pass.length < 8 || pass.length > 63) {
      passError = tr.passwordLength;
    }
    if (ssidError != null || passError != null) {
      setState(() {
        _ssidError = ssidError;
        _passError = passError;
      });
      return;
    }
    Navigator.pop(context, _Creds(ssid, pass));
  }

  @override
  Widget build(BuildContext context) {
    final tr = widget.tr;
    final fixed = widget.ssid;
    final needsPassword = fixed == null || widget.secure;
    return AlertDialog(
      title: Text(fixed == null ? tr.otherNetwork : tr.connectTo(fixed)),
      content: SingleChildScrollView(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            if (fixed == null) ...[
              TextField(
                controller: _ssid,
                autofocus: true,
                decoration: InputDecoration(
                  labelText: tr.wifiName,
                  errorText: _ssidError,
                  border: const OutlineInputBorder(),
                ),
              ),
              const SizedBox(height: 12),
            ],
            if (needsPassword)
              TextField(
                controller: _pass,
                autofocus: fixed != null,
                obscureText: _hidden,
                autocorrect: false,
                enableSuggestions: false,
                onSubmitted: (_) => _submit(),
                decoration: InputDecoration(
                  labelText: tr.wifiPassword,
                  helperText: fixed == null ? tr.wifiPasswordOpenHint : null,
                  errorText: _passError,
                  border: const OutlineInputBorder(),
                  suffixIcon: IconButton(
                    tooltip: _hidden ? tr.showPassword : tr.hidePassword,
                    icon: Icon(
                      _hidden ? Icons.visibility_outlined : Icons.visibility_off_outlined,
                    ),
                    onPressed: () => setState(() => _hidden = !_hidden),
                  ),
                ),
              ),
          ],
        ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.pop(context),
          child: Text(tr.cancel),
        ),
        FilledButton(onPressed: _submit, child: Text(tr.connect)),
      ],
    );
  }
}

enum _Phase { trying, success, failed, moved }

/// Shown after "Connect": follows the ESP32 while it switches networks.
class _ProvisionDialog extends StatefulWidget {
  const _ProvisionDialog({
    required this.esp,
    required this.settings,
    required this.tr,
    required this.ssid,
  });

  final LocalBoard esp;
  final SettingsController settings;
  final Tr tr;
  final String ssid;

  @override
  State<_ProvisionDialog> createState() => _ProvisionDialogState();
}

class _ProvisionDialogState extends State<_ProvisionDialog> {
  _Phase _phase = _Phase.trying;
  String _ip = '';
  String _detail = '';
  bool _addressUpdated = false;
  bool _stopped = false;

  @override
  void initState() {
    super.initState();
    unawaited(_follow());
  }

  @override
  void dispose() {
    _stopped = true;
    super.dispose();
  }

  void _finish(_Phase phase, {String detail = ''}) {
    if (!mounted) return;
    setState(() {
      _phase = phase;
      _detail = detail;
    });
  }

  Future<void> _follow() async {
    final started = DateTime.now();
    var unreachable = 0;
    while (!_stopped &&
        DateTime.now().difference(started) < const Duration(seconds: 45)) {
      await Future<void>.delayed(const Duration(seconds: 2));
      if (_stopped) return;
      final res = await widget.esp.wifiStatus();
      if (_stopped) return;
      final st = res.data;
      if (st == null) {
        // The ESP32 left the old network, so no answer is normal for a while.
        unreachable++;
        continue;
      }
      unreachable = 0;
      if (st.state == 'failed') {
        _finish(_Phase.failed, detail: widget.tr.connectFailed);
        return;
      }
      if (st.state == 'idle' && st.connected && st.ssid == widget.ssid) {
        await _success(st);
        return;
      }
    }
    if (_stopped) return;
    if (unreachable >= 2) {
      _finish(_Phase.moved);
    } else {
      _finish(_Phase.failed, detail: widget.tr.timeoutText);
    }
  }

  Future<void> _success(WifiStatus st) async {
    var updated = false;
    final ip = st.ip;
    if (ip.isNotEmpty && ip != '0.0.0.0') {
      final current = Uri.tryParse(widget.settings.value.activeUrl)?.host;
      if (current != ip) {
        await widget.settings.setActiveUrl('http://$ip/');
        updated = true;
      }
    }
    if (!mounted) return;
    setState(() {
      _phase = _Phase.success;
      _ip = ip;
      _addressUpdated = updated;
    });
  }

  Future<void> _enterIp() async {
    final tr = widget.tr;
    final field = TextEditingController();
    String? error;
    final text = await showDialog<String>(
      context: context,
      builder: (ctx) => StatefulBuilder(
        builder: (ctx, refresh) => AlertDialog(
          title: Text(tr.enterNewIp),
          content: TextField(
            controller: field,
            autofocus: true,
            keyboardType: TextInputType.url,
            decoration: InputDecoration(
              hintText: '192.168.1.50',
              errorText: error,
              border: const OutlineInputBorder(),
            ),
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(ctx),
              child: Text(tr.cancel),
            ),
            FilledButton(
              onPressed: () {
                final url = normalizeUrl(field.text);
                if (url == null) {
                  refresh(() => error = tr.invalidUrl);
                  return;
                }
                Navigator.pop(ctx, url);
              },
              child: Text(tr.save),
            ),
          ],
        ),
      ),
    );
    if (text == null) return;
    await widget.settings.setActiveUrl(text);
    if (mounted) Navigator.pop(context);
  }

  @override
  Widget build(BuildContext context) {
    final tr = widget.tr;
    final theme = Theme.of(context);

    final String title;
    final Widget body;
    final List<Widget> actions;

    switch (_phase) {
      case _Phase.trying:
        title = tr.connectingTitle;
        body = Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const SizedBox(height: 4),
            const CircularProgressIndicator(),
            const SizedBox(height: 16),
            Text(tr.connectingHelp, style: theme.textTheme.bodySmall),
          ],
        );
        actions = [
          TextButton(onPressed: () => Navigator.pop(context), child: Text(tr.close)),
        ];
      case _Phase.success:
        title = tr.connectedOk;
        body = Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Icon(Icons.check_circle, color: _okColor, size: 44),
            const SizedBox(height: 12),
            Text('${widget.ssid}\n${tr.newIpIs(_ip)}',
                style: theme.textTheme.titleSmall),
            if (_addressUpdated) ...[
              const SizedBox(height: 8),
              Text(tr.addressUpdated, style: theme.textTheme.bodySmall),
            ],
            const SizedBox(height: 8),
            Text(tr.fixedIpTip, style: theme.textTheme.bodySmall),
          ],
        );
        actions = [
          FilledButton(onPressed: () => Navigator.pop(context), child: Text(tr.ok)),
        ];
      case _Phase.failed:
        title = tr.wifiNotConnected;
        body = Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const Icon(Icons.error_outline, color: _badColor, size: 44),
            const SizedBox(height: 12),
            Text(_detail, style: theme.textTheme.bodyMedium),
          ],
        );
        actions = [
          FilledButton(onPressed: () => Navigator.pop(context), child: Text(tr.ok)),
        ];
      case _Phase.moved:
        title = tr.movedTitle;
        body = Text(tr.movedHelp, style: theme.textTheme.bodyMedium);
        actions = [
          TextButton(onPressed: () => Navigator.pop(context), child: Text(tr.close)),
          FilledButton(onPressed: _enterIp, child: Text(tr.enterNewIp)),
        ];
    }

    return AlertDialog(
      title: Text(title),
      content: SingleChildScrollView(child: body),
      actions: actions,
    );
  }
}
