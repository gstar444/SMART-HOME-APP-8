#include "cloud.h"

#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_system.h>
#if __has_include(<esp_random.h>)
#include <esp_random.h>   // esp_random()/esp_fill_random() moved here in ESP-IDF 5
#endif

#include "automation.h"
#include "ca_roots.h"
#include "config_store.h"
#include "devices.h"
#include "net.h"
#include "snapshot.h"

static WiFiClientSecure tlsClient;
static PubSubClient mqtt(tlsClient);

static char tCmd[96], tAck[96], tState[96], tInv[96], tLwt[96];
static char status[40] = "disabled";
static String customCa;              // must outlive the TLS client: setCACert() keeps the pointer
static DedupeRing dedupe;
static bool dedupeOk[DedupeRing::N]; // result of each remembered command, to answer a re-delivery truthfully
static RateWindow cmdRate(10000UL, 20);

static bool reconfigure = true;
static bool invDirty = true;
static uint8_t attempt = 0;
static uint32_t nextTryAt = 0, lastStatePub = 0, lastInvPub = 0, lastPubSeq = 0;
static bool wasConnected = false;

static void setStatus(const char* s) { copyStr(status, sizeof(status), s); }
const char* cloudStatus() { return status; }
bool cloudConnected() { return mqtt.connected(); }
void cloudReconfigure() { reconfigure = true; }
void cloudInventoryChanged() { invDirty = true; }

// ------------------------------------------------------------------ publishing
static bool publishJson(const char* topic, JsonDocument& doc, bool retained) {
  String payload;
  serializeJson(doc, payload);
  // beginPublish/write/endPublish streams the payload, so it may be larger than the receive buffer.
  if (!mqtt.beginPublish(topic, payload.length(), retained)) return false;
  mqtt.write((const uint8_t*)payload.c_str(), payload.length());
  return mqtt.endPublish();
}

static void publishInventory() {
  JsonDocument doc;
  snapshotInventory(doc.to<JsonObject>(), false);
  if (publishJson(tInv, doc, true)) { invDirty = false; lastInvPub = millis(); }
}

static void publishState() {
  JsonDocument doc;
  snapshotState(doc.to<JsonObject>());
  if (publishJson(tState, doc, true)) { lastPubSeq = gStateSeq; lastStatePub = millis(); }
}

static void sendAck(const char* id, const char* err, bool duplicate) {
  JsonDocument doc;
  doc["v"] = 1;
  doc["id"] = id;
  doc["ok"] = (err == nullptr);
  if (err) doc["err"] = err;
  if (duplicate) doc["dup"] = true;
  doc["cfgRev"] = cfg.revision;
  String out;
  serializeJson(doc, out);
  mqtt.publish(tAck, out.c_str(), false);
}

// ------------------------------------------------------------------ command execution
// Allow-list of operations. Anything else is rejected with "unknown_op". Hardware details (pins,
// polarity), Wi-Fi, MQTT credentials, rules, token and firmware can NOT be changed from the cloud:
// a stolen app login can switch lights, not reconfigure or take over a board.
static const char* execSet(JsonDocument& d) {
  const char* dev = d["dev"].is<const char*>() ? d["dev"].as<const char*>() : "";
  if (!d["on"].is<bool>()) return "bad_on";
  int i = devIndex(dev);
  if (i < 0) return "unknown_device";
  bool on = d["on"].as<bool>();
  const CfgDevice& c = cfg.devices[i];
  bool isLock = !strcmp(c.type, "lock");
  if (!c.remote && !(isLock && on)) return "remote_not_allowed";   // locking is always allowed, unlocking needs remote=true
  return devSet(i, on, true);
}

static const char* execRoomUpsert(JsonDocument& d) {
  if (!cfg.mqtt.remoteConfig) return "remote_config_disabled";
  if (!d["room"].is<JsonObjectConst>()) return "bad_room";
  JsonObjectConst r = d["room"].as<JsonObjectConst>();
  CfgRoom in;
  memset(&in, 0, sizeof(in));
  copyStr(in.id, sizeof(in.id), r["id"].is<const char*>() ? r["id"].as<const char*>() : "");
  copyStr(in.name, sizeof(in.name), r["name"].is<const char*>() ? r["name"].as<const char*>() : "");
  cleanText(in.name);
  long rev = r["rev"].is<long>() ? r["rev"].as<long>() : 0;
  in.rev = rev > 0 ? (uint32_t)rev : 0;
  bool changed = false;
  const char* e = configRoomUpsert(in, &changed);
  if (!e && changed) invDirty = true;
  return e;
}

static const char* execRoomDelete(JsonDocument& d) {
  if (!cfg.mqtt.remoteConfig) return "remote_config_disabled";
  if (!d["room"].is<JsonObjectConst>()) return "bad_room";
  JsonObjectConst r = d["room"].as<JsonObjectConst>();
  const char* id = r["id"].is<const char*>() ? r["id"].as<const char*>() : "";
  long rev = r["rev"].is<long>() ? r["rev"].as<long>() : 0;
  bool changed = false;
  const char* e = configRoomDelete(id, rev > 0 ? (uint32_t)rev : 0, &changed);
  if (!e && changed) invDirty = true;
  return e;
}

// Cloud-side edits are limited to presentation: name, room, icon (icon must keep the device type).
static const char* execDeviceUpdate(JsonDocument& d) {
  if (!cfg.mqtt.remoteConfig) return "remote_config_disabled";
  const char* id = d["dev"].is<const char*>() ? d["dev"].as<const char*>() : "";
  int i = findDeviceCfg(cfg, id);
  if (i < 0) return "unknown_device";
  CfgDevice backup = cfg.devices[i];
  CfgDevice nd = backup;
  if (d["name"].is<const char*>()) { copyStr(nd.name, sizeof(nd.name), d["name"].as<const char*>()); cleanText(nd.name); }
  if (d["room"].is<const char*>()) copyStr(nd.room, sizeof(nd.room), d["room"].as<const char*>());
  if (d["icon"].is<const char*>()) copyStr(nd.icon, sizeof(nd.icon), d["icon"].as<const char*>());
  const char* e = validateDeviceStrict(cfg, nd, i, gTarget, gPsram, dhtReservedPin());
  if (e) return e;
  cfg.devices[i] = nd;
  if (!configSave()) { cfg.devices[i] = backup; return "save_failed"; }
  devicesReconcile();
  invDirty = true;
  return nullptr;
}

static const char* execute(JsonDocument& d) {
  if ((d["v"] | 0) != 1) return "bad_version";
  const char* op = d["op"].is<const char*>() ? d["op"].as<const char*>() : "";
  if (!strcmp(op, "set")) return execSet(d);
  if (!strcmp(op, "scene")) return devRunScene(d["scene"].is<const char*>() ? d["scene"].as<const char*>() : "", true);
  if (!strcmp(op, "dnd")) {
    if (!d["on"].is<bool>()) return "bad_on";
    devSetDnd(d["on"].as<bool>());
    return nullptr;
  }
  if (!strcmp(op, "room.upsert")) return execRoomUpsert(d);
  if (!strcmp(op, "room.delete")) return execRoomDelete(d);
  if (!strcmp(op, "device.update")) return execDeviceUpdate(d);
  if (!strcmp(op, "inventory.get")) { invDirty = true; lastPubSeq = gStateSeq - 1; return nullptr; }
  if (!strcmp(op, "ping")) return nullptr;
  return "unknown_op";
}

static void handleCommand(const char* payload, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, payload, len)) return;             // not JSON: nothing to acknowledge
  const char* id = doc["id"].is<const char*>() ? doc["id"].as<const char*>() : "";
  if (!validCmdId(id)) return;

  int pos = dedupe.find(id);
  if (pos >= 0) {                                              // broker re-delivery or app retry: never run twice
    sendAck(id, dedupeOk[pos] ? nullptr : "failed_earlier", true);
    return;
  }
  if (!cmdRate.allow(millis())) { sendAck(id, "rate_limited", false); return; }

  dedupe.checkAndAdd(id);
  int slot = (dedupe.head + DedupeRing::N - 1) % DedupeRing::N;
  const char* err = execute(doc);
  dedupeOk[slot] = (err == nullptr);
  sendAck(id, err, false);
  gStateSeq++;                                                 // publish the new state right away
}

static void onMessage(char* topic, uint8_t* payload, unsigned int len) {
  if (strcmp(topic, tCmd) != 0) return;                        // we only ever subscribe to our own cmd topic
  if (len == 0 || len > 600) return;
  handleCommand((const char*)payload, len);
}

// ------------------------------------------------------------------ connection management
static const char* pickCa() {
  // A PEM uploaded to /ca.pem overrides the preset (needed if a provider changes its chain).
  if (customCaLoad(customCa)) return customCa.c_str();
  if (!strcmp(cfg.mqtt.provider, "hivemq")) return ROOT_CA_ISRG_X1;
  if (!strcmp(cfg.mqtt.provider, "emqx")) return ROOT_CA_DIGICERT_G2;
  return nullptr;                                              // "custom" with no PEM: refuse to connect
}

static void buildTopics() {
  buildTopic(tCmd, sizeof(tCmd), cfg.mqtt.homeId, gBoardId, "cmd");
  buildTopic(tAck, sizeof(tAck), cfg.mqtt.homeId, gBoardId, "ack");
  buildTopic(tState, sizeof(tState), cfg.mqtt.homeId, gBoardId, "state");
  buildTopic(tInv, sizeof(tInv), cfg.mqtt.homeId, gBoardId, "inventory");
  buildTopic(tLwt, sizeof(tLwt), cfg.mqtt.homeId, gBoardId, "lwt");
}

static const char* mqttStateName(int s) {
  switch (s) {
    case -4: return "timeout";
    case -3: return "connection_lost";
    case -2: return "tls_or_network";   // TCP/TLS failed: unreachable host, wrong CA, expired/invalid certificate, bad clock
    case -1: return "disconnected";
    case 1: return "bad_protocol";
    case 2: return "bad_client_id";
    case 3: return "broker_unavailable";
    case 4: return "bad_credentials";
    case 5: return "not_authorized";
    default: return "unknown";
  }
}

static void scheduleRetry(uint32_t now) {
  if (attempt < 250) attempt++;
  nextTryAt = now + backoffMs((uint8_t)(attempt - 1), esp_random());
}

static void tryConnect(uint32_t now) {
  const char* ca = pickCa();
  if (!ca) { setStatus("no_ca"); nextTryAt = now + 30000UL; return; }

  tlsClient.stop();
  tlsClient.setCACert(ca);              // pinned root; the handshake fails if the chain does not verify
  tlsClient.setHandshakeTimeout(8);
  mqtt.setServer(cfg.mqtt.host, cfg.mqtt.port);   // by host NAME, so SNI is sent (HiveMQ/EMQX require it)
  setStatus("connecting");

  bool ok = mqtt.connect(gBoardId, cfg.mqtt.username, cfg.mqtt.password, tLwt, 1, true, "offline", true);
  if (!ok) {
    int st = mqtt.state();
    char reason[40];
    snprintf(reason, sizeof(reason), "error:%s", mqttStateName(st));
    setStatus(reason);
    char tlsErr[80] = "";
    int tlsCode = tlsClient.lastError(tlsErr, sizeof(tlsErr));
    Serial.printf("[mqtt] connect failed: %s (mqtt=%d, tls=%d %s) retry in backoff\n", reason, st, tlsCode, tlsErr);
    scheduleRetry(now);
    return;
  }

  attempt = 0;
  wasConnected = true;
  mqtt.subscribe(tCmd, 1);
  mqtt.publish(tLwt, "online", true);
  publishInventory();
  publishState();
  setStatus("connected");
  Serial.printf("[mqtt] connected to %s (TLS verified)\n", cfg.mqtt.host);
}

void cloudBegin() {
  mqtt.setBufferSize(1024);
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(5);
  mqtt.setCallback(onMessage);
  buildTopics();
}

void cloudTick(uint32_t now) {
  if (reconfigure) {
    reconfigure = false;
    if (mqtt.connected()) {
      mqtt.publish(tLwt, "offline", true);   // a clean DISCONNECT suppresses the will, so say it ourselves
      mqtt.disconnect();
    }
    wasConnected = false;
    buildTopics();
    attempt = 0;
    nextTryAt = 0;
    invDirty = true;
  }

  if (!cfg.mqtt.enabled) { setStatus("disabled"); return; }

  if (mqtt.connected()) {
    mqtt.loop();
    if (invDirty && (uint32_t)(now - lastInvPub) >= 2000UL) publishInventory();
    bool changed = gStateSeq != lastPubSeq && (uint32_t)(now - lastStatePub) >= 250UL;
    bool heartbeat = (uint32_t)(now - lastStatePub) >= 60000UL;
    if (changed || heartbeat) publishState();
    return;
  }

  if (wasConnected) {                       // we had a session and lost it
    wasConnected = false;
    setStatus("error:connection_lost");
    scheduleRetry(now);
    Serial.println("[mqtt] connection lost - local control is unaffected, retrying with backoff");
    return;
  }
  if (!netStaUp()) { setStatus("waiting_wifi"); return; }
  if (!timeValid()) { setStatus("waiting_time"); return; }   // certificate validity needs a correct clock
  if ((int32_t)(now - nextTryAt) >= 0) tryConnect(now);
}
