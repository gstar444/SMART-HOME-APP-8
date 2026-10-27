import 'package:flutter/material.dart';
import 'package:webview_flutter/webview_flutter.dart';

// Firmware 4.x boards no longer serve a web UI (the app is the UI), so the former "ESP32 web page"
// screen and its zoom/JavaScript/auto-refresh/timeout/toolbar settings were removed. Only the
// camera viewer remains.

/// Minimal full-screen WebView, used for the camera stream (ESP32-CAM URL).
class SimpleWebScreen extends StatefulWidget {
  const SimpleWebScreen({super.key, required this.title, required this.url});

  final String title;
  final String url;

  @override
  State<SimpleWebScreen> createState() => _SimpleWebScreenState();
}

class _SimpleWebScreenState extends State<SimpleWebScreen> {
  late final WebViewController _web;

  @override
  void initState() {
    super.initState();
    _web = WebViewController()
      ..setJavaScriptMode(JavaScriptMode.unrestricted)
      ..loadRequest(Uri.parse(widget.url));
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Text(widget.title),
        actions: [
          IconButton(icon: const Icon(Icons.refresh), onPressed: _web.reload),
        ],
      ),
      body: WebViewWidget(controller: _web),
    );
  }
}
