// Host test of cloud.cpp: the REAL MQTT module with recording stand-ins for the TLS client and PubSubClient.
// What it can show: which CA is installed, that no connection is attempted without one, connect/will/subscribe
// parameters, backoff, and - most important - how every incoming command is validated and answered.
// What it can NOT show: that a real TLS handshake verifies a certificate chain or a host name (see docs/TEST_REPORT.md).
#define SH_HOST_TEST 1
#include <string>
#include "Arduino.h"
#include "ArduinoJson.h"
#include "LittleFS.h"
#include "PubSubClient.h"
#include "WiFiClientSecure.h"
#include "automation.h"
#include "ca_roots.h"
#include "cloud.h"
#include "config_store.h"
#include "devices.h"
#include "net.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { auto x_ = (a); auto y_ = (b); if (x_ == y_) g_pass++; else { g_fail++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #a " == " #b); } } while (0)

static bool g_sta = true;
bool netStaUp() { return g_sta; }
String netIp() { return String("192.168.1.50"); }
void netRestartSoon(uint32_t) {}

static const std::string CMD = "sh/v1/myhome/sh-aabbcc/cmd";
static const std::string ACK = "sh/v1/myhome/sh-aabbcc/ack";
static const std::string LWT = "sh/v1/myhome/sh-aabbcc/lwt";

static void configureBoard(const char* provider, bool enable = true) {
  g_fs.clear(); g_pins.clear(); g_pinMode.clear(); g_millis += 100000;   // time only moves forward
  g_mqttPub.clear(); g_mqttSubs.clear(); g_mqttConnectCalls = 0; g_mqttDisconnects = 0; g_mqttConnected = false;
  g_mqttConnectResult = true; g_mqttState = 0; g_tlsCa.clear(); g_tlsCaCalls = 0; g_tlsInsecureCalls = 0; g_sta = true;
  memset(&cfg, 0, sizeof(cfg)); gTarget = TARGET_ESP32; gPsram = false;
  configLoad();
  copyStr(gBoardId, sizeof(gBoardId), "sh-aabbcc");
  copyStr(cfg.mqtt.provider, sizeof(cfg.mqtt.provider), provider);
  copyStr(cfg.mqtt.host, sizeof(cfg.mqtt.host), "abc123.s1.eu.hivemq.cloud");
  copyStr(cfg.mqtt.username, sizeof(cfg.mqtt.username), "board-aabbcc");
  copyStr(cfg.mqtt.password, sizeof(cfg.mqtt.password), "mqtt-secret-456");
  copyStr(cfg.mqtt.homeId, sizeof(cfg.mqtt.homeId), "myhome");
  cfg.mqtt.enabled = enable; cfg.mqtt.port = 8883;
  copyStr(cfg.wifi.password, sizeof(cfg.wifi.password), "wifi-secret-123");
}
static void addDev(const char* json) {
  JsonDocument b; deserializeJson(b, String(json)); CfgDevice d;
  const char* e = parseDeviceJson(b.as<JsonObjectConst>(), nullptr, false, d);
  if (!e) e = validateDeviceStrict(cfg, d, -1, gTarget, gPsram, dhtReservedPin());
  if (e) { printf("  setup problem: %s\n", e); g_fail++; return; }
  cfg.devices[cfg.nDevices++] = d;
}
static void startBoard(const char* provider = "hivemq") {
  configureBoard(provider);
  addDev(R"({"id":"light","name":"Light","icon":"light","pin":23})");
  addDev(R"({"id":"fan","name":"Fan","icon":"fan","pin":22,"remote":false})");
  addDev(R"({"id":"door","name":"Door","icon":"lock","pin":21})");                // remote false by default
  addDev(R"({"id":"siren","name":"Siren","icon":"siren","pin":18})");
  addDev(R"({"id":"pir","name":"PIR","icon":"pir","pin":34})");
  devicesBegin(); automationBegin();
  cloudBegin(); cloudReconfigure();
  cloudTick(g_millis);
}
static const MqttPub* find(const std::string& topic, size_t from = 0) {
  for (size_t i = from; i < g_mqttPub.size(); i++) if (g_mqttPub[i].topic == topic) return &g_mqttPub[i];
  return nullptr;
}
// deliver a command to the firmware exactly like the MQTT client would
static void deliver(const std::string& topic, const std::string& payload) {
  std::vector<uint8_t> buf(payload.begin(), payload.end()); buf.push_back(0);
  std::string t = topic;
  g_mqttCb(&t[0], buf.data(), (unsigned)payload.size());
}
struct Ack { bool seen = false, ok = false, dup = false; std::string err, id; };
static Ack send(const std::string& payload, const std::string& topic = CMD, bool spaced = true) {
  if (spaced) g_millis += 1000;
  g_mqttPub.clear();
  deliver(topic, payload);
  Ack a; const MqttPub* p = find(ACK);
  if (!p) return a;
  JsonDocument d; deserializeJson(d, String(p->payload));
  a.seen = true; a.ok = d["ok"].as<bool>(); a.dup = d["dup"].as<bool>();
  const char* e = d["err"].as<const char*>(); a.err = e ? e : "";
  const char* i = d["id"].as<const char*>(); a.id = i ? i : "";
  CHECK_EQ(d["v"].as<int>(), 1);
  return a;
}
static std::string cmd(const char* id, const char* op, const char* rest = "") {
  return std::string("{\"v\":1,\"id\":\"") + id + "\",\"op\":\"" + op + "\"" + (rest[0] ? "," : "") + rest + "}";
}

static void test_connection_and_ca() {
  printf("cloud: connection parameters, root CA selection, no insecure path\n");
  startBoard("hivemq");
  CHECK_EQ(g_mqttConnectCalls, 1);
  CHECK_EQ(std::string(cloudStatus()), "connected");
  CHECK_EQ(g_mqttHost, std::string("abc123.s1.eu.hivemq.cloud"));
  CHECK_EQ(g_mqttPort, 8883);
  CHECK_EQ(g_mqttConn.id, std::string("sh-aabbcc"));
  CHECK_EQ(g_mqttConn.user, std::string("board-aabbcc"));
  CHECK_EQ(g_mqttConn.pass, std::string("mqtt-secret-456"));
  CHECK_EQ(g_mqttConn.willTopic, LWT);                       // the Last Will goes to this board's own lwt topic
  CHECK_EQ(g_mqttConn.willMsg, std::string("offline"));
  CHECK_EQ(g_mqttConn.willQos, 1);
  CHECK(g_mqttConn.willRetain && g_mqttConn.clean);
  CHECK_EQ(g_tlsCa, std::string(ROOT_CA_ISRG_X1));            // HiveMQ: pinned ISRG Root X1
  CHECK(g_tlsCaCalls >= 1);
  CHECK_EQ(g_tlsInsecureCalls, 0);                            // the insecure switch is never used
  // the board listens to exactly one topic: its own command topic
  CHECK_EQ(g_mqttSubs.size(), (size_t)1);
  CHECK_EQ(g_mqttSubs[0], CMD);
  // on connect: retained online + inventory + state, nothing secret inside
  const MqttPub* lwt = find(LWT);
  CHECK(lwt && lwt->retained && lwt->payload == "online");
  const MqttPub* inv = find("sh/v1/myhome/sh-aabbcc/inventory");
  const MqttPub* st = find("sh/v1/myhome/sh-aabbcc/state");
  CHECK(inv && inv->retained && st && st->retained);
  for (const MqttPub& p : g_mqttPub) {
    CHECK(p.payload.find("mqtt-secret-456") == std::string::npos && p.payload.find("wifi-secret-123") == std::string::npos);
    CHECK(p.topic.rfind("sh/v1/myhome/sh-aabbcc/", 0) == 0);  // it only ever publishes under its own prefix
  }
  CHECK(inv->payload.find("\"pin\"") == std::string::npos);   // hardware details are not sent over the cloud
  CHECK(inv->payload.find("\"board\":\"sh-aabbcc\"") != std::string::npos);
  CHECK(st->payload.find("\"energy\":\"estimated\"") != std::string::npos);

  startBoard("emqx");
  CHECK_EQ(g_tlsCa, std::string(ROOT_CA_DIGICERT_G2));
  CHECK_EQ(g_tlsInsecureCalls, 0);

  // an uploaded PEM OVERRIDES the preset (a provider changed its chain) - it never switches verification off
  configureBoard("hivemq"); g_fs["/ca.pem"] = "-----BEGIN CERTIFICATE-----\n" + std::string(300, 'Q') + "\n-----END CERTIFICATE-----\n";
  cloudBegin(); cloudReconfigure(); cloudTick(g_millis);
  CHECK(g_tlsCa.find("QQQQQQQQ") != std::string::npos && g_tlsCa != std::string(ROOT_CA_ISRG_X1));
  CHECK_EQ(g_mqttConnectCalls, 1);

  // provider "custom" with no PEM: refuses to connect at all
  configureBoard("custom"); cloudBegin(); cloudReconfigure(); cloudTick(g_millis);
  CHECK_EQ(std::string(cloudStatus()), "no_ca");
  CHECK_EQ(g_mqttConnectCalls, 0);
  CHECK_EQ(g_tlsCaCalls, 0);
  g_fs["/ca.pem"] = "-----BEGIN CERTIFICATE-----\n" + std::string(300, 'Z') + "\n-----END CERTIFICATE-----\n";
  g_millis += 31000; cloudTick(g_millis);                      // retried after 30 s once a CA exists
  CHECK_EQ(g_mqttConnectCalls, 1);
  CHECK(g_tlsCa.find("ZZZZZZZZ") != std::string::npos);
}

static void test_gating_and_backoff() {
  printf("cloud: waits for Wi-Fi, stays quiet when disabled, backs off on failure\n");
  configureBoard("hivemq", false);
  cloudBegin(); cloudReconfigure(); cloudTick(g_millis);
  CHECK_EQ(std::string(cloudStatus()), "disabled");
  CHECK_EQ(g_mqttConnectCalls, 0);

  configureBoard("hivemq"); g_sta = false;
  cloudBegin(); cloudReconfigure(); cloudTick(g_millis);
  CHECK_EQ(std::string(cloudStatus()), "waiting_wifi");
  CHECK_EQ(g_mqttConnectCalls, 0);
  g_sta = true; g_millis += 10; cloudTick(g_millis);
  CHECK_EQ(g_mqttConnectCalls, 1);

  // failures: reported with a safe reason, retried with exponentially growing delays
  configureBoard("hivemq"); g_mqttConnectResult = false; g_mqttState = -2;
  cloudBegin(); cloudReconfigure(); cloudTick(g_millis);
  CHECK_EQ(g_mqttConnectCalls, 1);
  CHECK_EQ(std::string(cloudStatus()), "error:tls_or_network");
  g_millis += 3000; cloudTick(g_millis); CHECK_EQ(g_mqttConnectCalls, 1);        // not yet (first delay is >= 5 s)
  g_millis += 4000; cloudTick(g_millis); CHECK_EQ(g_mqttConnectCalls, 2);        // 7 s later: second attempt
  g_millis += 9000; cloudTick(g_millis); CHECK_EQ(g_mqttConnectCalls, 2);        // second delay is >= 10 s
  g_millis += 4000; cloudTick(g_millis); CHECK_EQ(g_mqttConnectCalls, 3);
  g_mqttState = 4; g_millis += 600000; cloudTick(g_millis);
  CHECK_EQ(std::string(cloudStatus()), "error:bad_credentials");
  g_mqttState = 5; g_millis += 600000; cloudTick(g_millis);
  CHECK_EQ(std::string(cloudStatus()), "error:not_authorized");
  for (int i = 0; i < 6; i++) { g_millis += 400000; cloudTick(g_millis); }       // the delay is capped at ~6 minutes
  CHECK(g_mqttConnectCalls >= 8);
  // recovery
  g_mqttConnectResult = true; g_millis += 400000; cloudTick(g_millis);
  CHECK_EQ(std::string(cloudStatus()), "connected");

  // a lost session is noticed and retried (relays are not involved at all)
  g_mqttConnected = false; int before = g_mqttConnectCalls;
  g_millis += 100; cloudTick(g_millis);
  CHECK_EQ(std::string(cloudStatus()), "error:connection_lost");
  g_millis += 400000; cloudTick(g_millis);
  CHECK_EQ(g_mqttConnectCalls, before + 1);

  // changing the broker settings says "offline" explicitly (a clean DISCONNECT would suppress the will), then reconnects
  g_mqttPub.clear(); int dis = g_mqttDisconnects;
  cloudReconfigure(); g_millis += 100; cloudTick(g_millis);
  const MqttPub* off = find(LWT);
  CHECK(off && off->payload == "offline" && off->retained);
  CHECK_EQ(g_mqttDisconnects, dis + 1);
}

static void test_commands_accepted() {
  printf("cloud: accepted commands act and are acknowledged\n");
  startBoard();
  Ack a = send(cmd("c1", "set", "\"dev\":\"light\",\"on\":true"));
  CHECK(a.seen && a.ok && a.id == "c1");
  CHECK(rt[0].on);
  g_millis += 300; cloudTick(g_millis);
  const MqttPub* st = find("sh/v1/myhome/sh-aabbcc/state");
  CHECK(st && st->payload.find("\"light\":{\"on\":true") != std::string::npos);      // new state is published right after

  CHECK(send(cmd("c2", "ping")).ok);
  CHECK(send(cmd("c3", "inventory.get")).ok);
  CHECK(send(cmd("c4", "dnd", "\"on\":true")).ok);
  CHECK(gDnd);
  Ack siren = send(cmd("c5", "set", "\"dev\":\"siren\",\"on\":true"));
  CHECK(!siren.ok && siren.err == "dnd");
  send(cmd("c6", "dnd", "\"on\":false"));

  // locks: locking is always allowed from the cloud, unlocking only for devices explicitly marked remote
  Ack lockIt = send(cmd("c7", "set", "\"dev\":\"door\",\"on\":true"));
  CHECK(lockIt.ok && rt[2].on);
  Ack unlock = send(cmd("c8", "set", "\"dev\":\"door\",\"on\":false"));
  CHECK(!unlock.ok && unlock.err == "remote_not_allowed" && rt[2].on);                // still locked
  cfg.devices[2].remote = true;
  CHECK(send(cmd("c9", "set", "\"dev\":\"door\",\"on\":false")).ok);
  CHECK(!rt[2].on);
  rt[2].on = true;

  // scenes from the cloud only touch cloud-enabled devices
  rt[0].on = true; rt[1].on = true; devSet(1, true, false);
  Ack scene = send(cmd("c10", "scene", "\"scene\":\"alloff\""));
  CHECK(scene.ok && !rt[0].on && rt[1].on);                                          // the fan (remote:false) is left alone
  CHECK(!send(cmd("c11", "scene", "\"scene\":\"nope\"")).ok);

  // room and name changes (allowed by default), with the conflict rule
  Ack room = send(cmd("c12", "room.upsert", "\"room\":{\"id\":\"den\",\"name\":\"Den\",\"rev\":2}"));
  CHECK(room.ok && findRoom(cfg, "den") >= 0 && roomLive(cfg, "den"));
  CHECK(send(cmd("c13", "room.upsert", "\"room\":{\"id\":\"den\",\"name\":\"Old name\",\"rev\":1}")).ok);   // stale: accepted as a no-op
  CHECK_EQ(std::string(cfg.rooms[findRoom(cfg, "den")].name), "Den");
  CHECK(send(cmd("c14", "device.update", "\"dev\":\"light\",\"name\":\"Reading lamp\",\"room\":\"den\"")).ok);
  CHECK(std::string(cfg.devices[0].name) == "Reading lamp" && std::string(cfg.devices[0].room) == "den");
  Ack del = send(cmd("c15", "room.delete", "\"room\":{\"id\":\"den\",\"rev\":3}"));
  CHECK(del.ok && !roomLive(cfg, "den") && cfg.devices[0].room[0] == '\0');
  Ack last = send(cmd("c16", "room.delete", "\"room\":{\"id\":\"room1\",\"rev\":9}"));
  CHECK(!last.ok && last.err == "last_room");
}

static void test_commands_rejected() {
  printf("cloud: everything else is refused\n");
  startBoard();
  // not allow-listed
  int n = 0;
  for (const char* op : {"wifi.set", "mqtt.set", "config.replace", "factory-reset", "auth.rotate", "device.add", "device.delete", "ota", "restart", "exec"}) {
    Ack a = send(cmd(("x" + std::to_string(n++)).c_str(), op));
    CHECK(a.seen && !a.ok && a.err == "unknown_op");
  }
  Ack ver = send("{\"v\":2,\"id\":\"v1\",\"op\":\"ping\"}");
  CHECK(ver.seen && !ver.ok && ver.err == "bad_version");
  CHECK(!send("{\"id\":\"v2\",\"op\":\"ping\"}").ok);

  // hardware fields are ignored, never applied
  const int pinBefore = cfg.devices[0].pin;
  Ack hw = send(cmd("h1", "device.update", "\"dev\":\"light\",\"pin\":5,\"watts\":4000,\"invert\":true,\"bootState\":\"on\",\"enabled\":false"));
  CHECK(hw.ok);
  CHECK(cfg.devices[0].pin == pinBefore && cfg.devices[0].watts == 12 && cfg.devices[0].enabled && cfg.devices[0].bootState == BOOT_OFF);
  CHECK(!send(cmd("h2", "device.update", "\"dev\":\"light\",\"icon\":\"lock\"")).ok);                 // would change the device type
  CHECK_EQ(send(cmd("h5", "device.update", "\"dev\":\"nobody\",\"name\":\"x\"")).err, std::string("unknown_device"));
  CHECK_EQ(send(cmd("h6", "device.update", "\"dev\":\"light\",\"room\":\"ghost\"")).err, std::string("device_room"));
  CHECK_EQ(send(cmd("s1", "set", "\"dev\":\"pir\",\"on\":true")).err, std::string("read_only"));
  CHECK_EQ(send(cmd("s2", "set", "\"dev\":\"light\",\"on\":\"yes\"")).err, std::string("bad_on"));
  CHECK_EQ(send(cmd("s3", "set", "\"dev\":\"nobody\",\"on\":true")).err, std::string("unknown_device"));
  CHECK_EQ(send(cmd("s4", "set", "\"dev\":\"fan\",\"on\":true")).err, std::string("remote_not_allowed"));
  CHECK(!rt[1].on);

  // remote configuration can be switched off per board
  cfg.mqtt.remoteConfig = false;
  CHECK_EQ(send(cmd("r1", "room.upsert", "\"room\":{\"id\":\"x\",\"name\":\"X\",\"rev\":1}")).err, std::string("remote_config_disabled"));
  CHECK_EQ(send(cmd("r2", "device.update", "\"dev\":\"light\",\"name\":\"Hacked\"")).err, std::string("remote_config_disabled"));
  CHECK(std::string(cfg.devices[0].name) == "Light");
  CHECK(send(cmd("r3", "set", "\"dev\":\"light\",\"on\":true")).ok);                                      // relays still work
  cfg.mqtt.remoteConfig = true;
}

static void test_input_hygiene() {
  printf("cloud: malformed, oversized, replayed and flooded input\n");
  startBoard();
  g_mqttPub.clear();
  deliver(CMD, "not json at all");
  deliver(CMD, "");
  deliver(CMD, "{\"v\":1,\"op\":\"ping\"}");                                         // no id
  deliver(CMD, "{\"v\":1,\"id\":\"has space\",\"op\":\"ping\"}");                      // invalid id
  deliver(CMD, "{\"v\":1,\"id\":\"" + std::string(25, 'a') + "\",\"op\":\"ping\"}");    // id too long
  deliver(CMD, "{\"v\":1,\"id\":\"big\",\"op\":\"ping\",\"pad\":\"" + std::string(700, 'x') + "\"}");   // > 600 bytes
  deliver("sh/v1/myhome/sh-aabbcc/state", cmd("w1", "set", "\"dev\":\"light\",\"on\":true"));      // not our command topic
  deliver("sh/v1/myhome/sh-OTHER/cmd", cmd("w2", "set", "\"dev\":\"light\",\"on\":true"));         // another board's topic
  deliver("sh/v1/otherhome/sh-aabbcc/cmd", cmd("w3", "set", "\"dev\":\"light\",\"on\":true"));
  CHECK(find(ACK) == nullptr);                                                         // none of these is even answered
  CHECK(!rt[0].on);

  // replay / re-delivery: an id runs once; the repeat is answered with dup and does nothing
  Ack first = send(cmd("dup1", "set", "\"dev\":\"light\",\"on\":true"));
  CHECK(first.ok && !first.dup && rt[0].on);
  devSet(0, false, true);                                                              // somebody switched it off locally
  Ack again = send(cmd("dup1", "set", "\"dev\":\"light\",\"on\":true"));
  CHECK(again.seen && again.ok && again.dup);
  CHECK(!rt[0].on);                                                                    // NOT executed a second time
  // a command that failed is not turned into a success by repeating it
  CHECK(!send(cmd("dup2", "set", "\"dev\":\"fan\",\"on\":true")).ok);
  Ack failedAgain = send(cmd("dup2", "set", "\"dev\":\"fan\",\"on\":true"));
  CHECK(failedAgain.dup && !failedAgain.ok && failedAgain.err == "failed_earlier");

  // flood: 20 commands per 10 s, the rest are rate limited (and may be retried later)
  startBoard();
  int ok = 0, limited = 0;
  for (int i = 0; i < 30; i++) {
    Ack a = send(cmd(("f" + std::to_string(i)).c_str(), "ping"), CMD, false);
    if (a.ok) ok++; else if (a.err == "rate_limited") limited++;
  }
  CHECK_EQ(ok, 20);
  CHECK_EQ(limited, 10);
  g_millis += 10001;
  CHECK(send(cmd("f-later", "ping")).ok);
  CHECK(send(cmd("f19", "ping")).dup);                                                  // a recent id is remembered ...
  Ack evicted = send(cmd("f0", "ping"));
  CHECK(evicted.ok && !evicted.dup);                                                    // ... the memory is only 16 ids: f0 has fallen out (documented replay limit)
}

int main() {
  test_connection_and_ca();
  test_gating_and_backoff();
  test_commands_accepted();
  test_commands_rejected();
  test_input_hygiene();
  printf("\n%d checks passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
