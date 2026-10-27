#pragma once
// Recording stand-in for PubSubClient: what the firmware connects with, subscribes to and publishes is kept in
// globals, and a test can deliver a message to the firmware's callback with g_mqttCb(topic, payload, len).
#include <functional>
#include "Arduino.h"
#include "WiFiClientSecure.h"
struct MqttPub { std::string topic, payload; bool retained; };
struct MqttConn { std::string id, user, pass, willTopic, willMsg; int willQos = -1; bool willRetain = false, clean = false; };
extern std::function<void(char*, uint8_t*, unsigned int)> g_mqttCb;
extern std::vector<MqttPub> g_mqttPub;
extern std::vector<std::string> g_mqttSubs;
extern MqttConn g_mqttConn;
extern std::string g_mqttHost;
extern int g_mqttPort, g_mqttConnectCalls, g_mqttState, g_mqttDisconnects;
extern bool g_mqttConnectResult, g_mqttConnected;
struct PubSubClient : public Print {
  MqttPub cur;
  PubSubClient(WiFiClientSecure&) {}
  PubSubClient& setServer(const char* host, uint16_t port) { g_mqttHost = host; g_mqttPort = port; return *this; }
  PubSubClient& setCallback(std::function<void(char*, uint8_t*, unsigned int)> cb) { g_mqttCb = cb; return *this; }
  bool setBufferSize(uint16_t) { return true; }
  PubSubClient& setKeepAlive(uint16_t) { return *this; }
  PubSubClient& setSocketTimeout(uint16_t) { return *this; }
  bool connect(const char* id, const char* user, const char* pass, const char* wt, uint8_t wq, bool wr, const char* wm, bool clean) {
    g_mqttConnectCalls++;
    g_mqttConn = MqttConn{id ? id : "", user ? user : "", pass ? pass : "", wt ? wt : "", wm ? wm : "", wq, wr, clean};
    g_mqttConnected = g_mqttConnectResult;
    return g_mqttConnectResult;
  }
  void disconnect() { g_mqttConnected = false; g_mqttDisconnects++; }
  bool connected() { return g_mqttConnected; }
  int state() { return g_mqttState; }
  bool loop() { return true; }
  bool subscribe(const char* t, uint8_t = 0) { g_mqttSubs.push_back(t); return true; }
  bool publish(const char* t, const char* p, bool r = false) { g_mqttPub.push_back({t, p, r}); return true; }
  bool beginPublish(const char* t, unsigned int, bool r) { cur = MqttPub{t, "", r}; return true; }
  size_t write(const uint8_t* b, size_t n) override { cur.payload.append((const char*)b, n); return n; }
  bool endPublish() { g_mqttPub.push_back(cur); return true; }
};
