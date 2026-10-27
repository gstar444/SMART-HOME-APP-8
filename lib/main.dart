import 'dart:async';

import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'dashboard_page.dart';
import 'esp32_api.dart';
import 'home_controller.dart';
import 'mqtt_service.dart';
import 'mqtt_transport.dart';
import 'secure_storage.dart';
import 'settings.dart';
import 'snapshot_cache.dart';

export 'settings.dart' show esp32Url, defaultEsp32Url;

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();

  // Start the slow Android Keystore read NOW: it runs while SharedPreferences loads below, instead of
  // after it (the first secure-storage access can take a few hundred milliseconds on a cold start).
  final store = SecureStore();
  final cloudSettings = store.loadCloud();

  // Settings are read once before the first frame, so the board address is known immediately.
  final controller = await SettingsController.load();
  final cache = SnapshotCache(await SharedPreferences.getInstance()); // already in memory: instant

  // Everything else is created synchronously and cheaply; nothing here waits for the network.
  final local = Esp32Controller(controller, store); // loads its token and polls asynchronously
  final mqtt = MqttService(transportFactory: MqttClientTransport.new);
  // The controller starts from the last known state of the home (marked "not live"), so the dashboard
  // is filled on the very first frame instead of showing a spinner until a board or the broker answers.
  final home = HomeController(settings: controller, mqtt: mqtt, local: local, cache: cache);
  runApp(SmartHomeApp(controller: controller, store: store, local: local, mqtt: mqtt, home: home));

  // The cloud connection starts after the first frame, so the TLS handshake never delays the UI.
  unawaited(() async {
    final cloud = await cloudSettings;
    if (cloud != null) await mqtt.configure(cloud);
  }());
}

class SmartHomeApp extends StatelessWidget {
  const SmartHomeApp({
    super.key,
    required this.controller,
    required this.store,
    required this.local,
    required this.mqtt,
    required this.home,
  });

  final SettingsController controller;
  final SecureStore store;
  final Esp32Controller local;
  final MqttService mqtt;
  final HomeController home;

  @override
  Widget build(BuildContext context) {
    // Providers sit ABOVE MaterialApp, so every pushed screen (settings, boards, cloud) can read them.
    return MultiProvider(
      providers: [
        ChangeNotifierProvider<SettingsController>.value(value: controller),
        Provider<SecureStore>.value(value: store),
        ChangeNotifierProvider<Esp32Controller>.value(value: local),
        ChangeNotifierProvider<MqttService>.value(value: mqtt),
        ChangeNotifierProvider<HomeController>.value(value: home),
      ],
      child: ListenableBuilder(
        listenable: controller,
        builder: (context, _) {
          final s = controller.value;
          final accent = Color(s.accent);
          return MaterialApp(
            debugShowCheckedModeBanner: false,
            title: s.title,
            themeMode: s.themeMode,
            theme: buildTheme(accent, Brightness.light),
            darkTheme: buildTheme(accent, Brightness.dark),
            // The dashboard lives *below* MaterialApp, so dialogs, snack bars and
            // navigation get a valid Navigator / ScaffoldMessenger context.
            home: DashboardPage(settings: controller, esp: home),
          );
        },
      ),
    );
  }
}
