#pragma once
#include "Arduino.h"
#define WL_CONNECTED 3
#define WL_DISCONNECTED 6
#define WIFI_STA 1
#define WIFI_AP_STA 3
#define WIFI_SCAN_RUNNING (-1)
#define WIFI_AUTH_OPEN 0
struct IPAddress { String toString() const { return String("192.168.1.50"); } };
struct WiFiClass {
  int status() { return WL_DISCONNECTED; }
  IPAddress localIP() { return IPAddress(); }
  IPAddress softAPIP() { return IPAddress(); }
  String SSID() { return String(""); }
  String SSID(int) { return String(""); }
  int RSSI() { return -60; }
  int RSSI(int) { return -60; }
  int encryptionType(int) { return 3; }
  void persistent(bool) {}
  void setHostname(const char*) {}
  void mode(int) {}
  void setAutoReconnect(bool) {}
  void begin(const char*, const char*) {}
  void disconnect() {}
  bool softAP(const char*, const char*) { return true; }
  void softAPdisconnect(bool) {}
  int softAPgetStationNum() { return 0; }
  int scanNetworks(bool) { return -1; }
  int scanComplete() { return -2; }
  void scanDelete() {}
};
extern WiFiClass WiFi;
