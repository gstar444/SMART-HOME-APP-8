// Host test of api.cpp: the REAL request handlers, driven through a functional WebServer stand-in, on top of
// config_store/devices/automation/snapshot. net.* and cloud.* are replaced by recording stubs (they need Wi-Fi/TLS).
// The JSON library is the stand-in from stubs/ (see its header): this tests our logic, not ArduinoJson.
#define SH_HOST_TEST 1
#include <string>
#include "Arduino.h"
#include "ArduinoJson.h"
#include "LittleFS.h"
#include "WebServer.h"
#include "api.h"
#include "automation.h"
#include "cloud.h"
#include "config_store.h"
#include "devices.h"
#include "net.h"
#include "sha256.h"

WebServer& apiServerForTest();

static int g_pass = 0, g_fail = 0;
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { auto x_ = (a); auto y_ = (b); if (x_ == y_) g_pass++; else { g_fail++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #a " == " #b); } } while (0)

// ---- recording stand-ins for the modules that need a radio
static int g_reconfigure = 0, g_inventoryChanged = 0, g_restarts = 0, g_tzApplied = 0;
static std::string g_lastConnectSsid;
void cloudReconfigure() { g_reconfigure++; }
void cloudInventoryChanged() { g_inventoryChanged++; }
const char* cloudStatus() { return "disabled"; }
bool cloudConnected() { return false; }
void cloudBegin() {}
void cloudTick(uint32_t) {}
bool netStaUp() { return true; }
String netIp() { return String("192.168.1.50"); }
void netRestartSoon(uint32_t) { g_restarts++; }
void netApplyTimezone() { g_tzApplied++; }
const char* netApSsid() { return "SmartHome-AABBCC"; }
bool netRequestConnect(const char* ssid, const char* pass, const char** err) {
  const char* e = validateWifi(ssid, pass);
  if (e) { *err = e; return false; }
  g_lastConnectSsid = ssid;
  return true;
}
void netWifiStatusToJson(JsonObject o) { o["state"] = "idle"; o["connected"] = true; o["ssid"] = "Home"; o["mode"] = "station"; }
void netScanRequest() {}
void netScanToJson(JsonObject o) { o["scanning"] = false; o["networks"].to<JsonArray>(); }
void netForgetWifi() {
  cfg.wifi.ssid[0] = '\0'; cfg.wifi.password[0] = '\0';
  copyStr(cfg.auth.claimCode, sizeof(cfg.auth.claimCode), "ZZZZZ-ZZZZZ");
  configSave();
  netRestartSoon(1500);
}

// ---- helpers
static const std::string TOKEN = "k3J9xQ_abcdEFGH-1234567890abcdefghijklmnop";   // 43 chars, valid token alphabet
static const std::string TOKEN2 = "Z9y8X7w6V5u4T3s2R1q0P_o-N-mLkJiHgFeDcBa123";
struct Resp { int status; std::string body; JsonDocument doc; std::string err() { const char* e = doc["error"].as<const char*>(); return e ? e : ""; } };

static Resp call(HTTPMethod m, const std::string& uri, const std::string& token = "", const std::string& body = "", const std::string& rawAuth = "") {
  std::map<std::string, std::string> h;
  if (!token.empty()) h["Authorization"] = "Bearer " + token;
  if (!rawAuth.empty()) h["Authorization"] = rawAuth;
  WebServer& s = apiServerForTest();
  s.dispatch(m, uri, h, body);
  Resp r; r.status = s.status; r.body = s.body; deserializeJson(r.doc, String(s.body));
  return r;
}
static Resp GET(const std::string& u, const std::string& t = "") { return call(HTTP_GET, u, t); }
static Resp POST(const std::string& u, const std::string& t, const std::string& b) { return call(HTTP_POST, u, t, b); }
static std::string tokenHash(const std::string& t) { uint8_t d[32]; char hex[65]; sha256((const uint8_t*)t.data(), t.size(), d); toHex(d, 32, hex); return hex; }
static bool has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

static void freshBoard() {                       // unclaimed board with a claim code, no devices
  g_fs.clear(); g_pins.clear(); g_pinMode.clear(); g_pinEvents.clear(); g_millis = 1000;
  memset(&cfg, 0, sizeof(cfg)); gTarget = TARGET_ESP32; gPsram = false;
  configLoad();
  copyStr(cfg.auth.claimCode, sizeof(cfg.auth.claimCode), "ABCDE-FGHJK");
  configSave();
  devicesBegin();
}
static void claimedBoard() {
  freshBoard();
  Resp r = POST("/api/v2/claim", "", "{\"code\":\"ABCDE-FGHJK\",\"token\":\"" + TOKEN + "\"}");
  CHECK_EQ(r.status, 200);
}
static std::string dev(const char* name, const char* icon, int pin) {
  return std::string("{\"name\":\"") + name + "\",\"icon\":\"" + icon + "\",\"pin\":" + std::to_string(pin) + "}";
}

static void test_unclaimed_and_claim() {
  printf("api: unclaimed board and the claim flow\n");
  freshBoard();
  Resp info = GET("/api/v2/info");
  CHECK_EQ(info.status, 200);
  CHECK(!info.doc["claimed"].as<bool>());
  CHECK_EQ(std::string(info.doc["board"].as<const char*>()), std::string(gBoardId));
  CHECK(!has(info.body, "ABCDE") && !has(info.body, "claimCode") && !has(info.body, "tokenHash"));   // nothing secret, nothing about the home

  // nothing but info/claim works while unclaimed, token or not
  CHECK_EQ(GET("/api/v2/state", TOKEN).status, 403);
  CHECK_EQ(GET("/api/v2/state", TOKEN).err(), "not_claimed");
  CHECK_EQ(POST("/api/v2/set", TOKEN, "{\"id\":\"x\",\"on\":true}").status, 403);
  CHECK_EQ(POST("/api/v2/config/mqtt", TOKEN, "{}").status, 403);

  const std::string good = "{\"code\":\"ABCDE-FGHJK\",\"token\":\"" + TOKEN + "\"}";
  CHECK_EQ(POST("/api/v2/claim", "", "{\"code\":\"AAAAA-AAAAA\",\"token\":\"" + TOKEN + "\"}").status, 403);   // wrong code
  CHECK_EQ(POST("/api/v2/claim", "", "{\"code\":\"AAAAA-AAAAA\",\"token\":\"" + TOKEN + "\"}").err(), "bad_code");
  CHECK_EQ(POST("/api/v2/claim", "", "{\"token\":\"" + TOKEN + "\"}").status, 403);                            // no code
  CHECK_EQ(POST("/api/v2/claim", "", "{\"code\":\"ABCDE-FGHJK\",\"token\":\"short\"}").err(), "bad_token");    // right code, weak token
  CHECK_EQ(POST("/api/v2/claim", "", "{\"code\":\"ABCDE-FGHJK\",\"token\":\"" + std::string(40, '!') + "\"}").err(), "bad_token");
  CHECK_EQ(POST("/api/v2/claim", "", "not json").status, 400);
  CHECK(!cfg.auth.claimed);

  Resp ok = POST("/api/v2/claim", "", "{\"code\":\"abcde fghjk\",\"token\":\"" + TOKEN + "\"}");   // case and separators are forgiven
  CHECK_EQ(ok.status, 200);
  CHECK(cfg.auth.claimed);
  CHECK_EQ(std::string(cfg.auth.tokenHash), tokenHash(TOKEN));
  CHECK(!has(std::string(cfg.auth.tokenHash), TOKEN));            // only the hash is stored
  CHECK(!has(ok.body, TOKEN) && !has(ok.body, "tokenHash"));
  CHECK(cfg.auth.claimCode[0] != '\0');                           // kept until Wi-Fi is confirmed (reboot mid-setup still works)
  CHECK_EQ(POST("/api/v2/claim", "", good).status, 409);          // claiming twice is refused
  CHECK_EQ(POST("/api/v2/claim", "", good).err(), "already_claimed");
  CHECK(GET("/api/v2/info").doc["claimed"].as<bool>());
  // the stored hash survives a reboot
  Config saved; memcpy(&saved, &cfg, sizeof(Config)); memset(&cfg, 0, sizeof(cfg)); configLoad();
  CHECK(cfg.auth.claimed && std::string(cfg.auth.tokenHash) == tokenHash(TOKEN));
}

static void test_claim_lockout() {
  printf("api: claim code brute force is locked out\n");
  freshBoard();
  const std::string body = "{\"code\":\"ABCDE-FGHJK\",\"token\":\"" + TOKEN + "\"}";
  for (int i = 0; i < 5; i++) CHECK_EQ(POST("/api/v2/claim", "", "{\"code\":\"00000-00000\",\"token\":\"" + TOKEN + "\"}").status, 403);
  Resp locked = POST("/api/v2/claim", "", body);                  // even the RIGHT code is refused now
  CHECK_EQ(locked.status, 429);
  CHECK(!cfg.auth.claimed);
  CHECK(apiServerForTest().resHeaders.count("Retry-After") == 1);
  g_millis += 299000; CHECK_EQ(POST("/api/v2/claim", "", body).status, 429);   // still locked just before 5 minutes
  g_millis += 2000;   CHECK_EQ(POST("/api/v2/claim", "", body).status, 200);   // then it works
}

static void test_authentication() {
  printf("api: every way of failing authentication, then the lock-out\n");
  claimedBoard();
  // Failed attempts are interleaved with successes: a success resets the counter, so the (correct) lock-out
  // below does not trigger early.
  auto ok = [&]() { CHECK_EQ(GET("/api/v2/state", TOKEN).status, 200); };
  CHECK_EQ(GET("/api/v2/state").status, 401);                                  // no header
  CHECK_EQ(GET("/api/v2/state").err(), "unauthorized");
  CHECK_EQ(GET("/api/v2/state", TOKEN2).status, 401);                          // well-formed but wrong
  CHECK_EQ(call(HTTP_GET, "/api/v2/state", "", "", "Basic dXNlcjpwYXNz").status, 401);
  ok();
  CHECK_EQ(call(HTTP_GET, "/api/v2/state", "", "", "Bearer").status, 401);
  CHECK_EQ(call(HTTP_GET, "/api/v2/state", "", "", "Bearer short").status, 401);   // fails the token format check, never hashed
  CHECK_EQ(call(HTTP_GET, "/api/v2/state", "", "", TOKEN).status, 401);             // token without the scheme
  ok();
  CHECK_EQ(call(HTTP_GET, "/api/v2/state?key=" + TOKEN).status, 401);               // the token is not accepted in the URL
  CHECK_EQ(call(HTTP_GET, "/api/v2/state?token=" + TOKEN + "&Authorization=Bearer%20" + TOKEN).status, 401);
  ok();
  CHECK_EQ(GET("/api/v2/state", TOKEN).doc["ok"].as<bool>(), true);
  // a near-miss (one character off) is just as wrong
  std::string off = TOKEN; off[10] = off[10] == 'a' ? 'b' : 'a';
  CHECK_EQ(GET("/api/v2/state", off).status, 401);
  ok();

  // lock-out: 5 failures within a minute, then even the right token is refused, then it recovers
  for (int i = 0; i < 5; i++) CHECK_EQ(GET("/api/v2/state", TOKEN2).status, 401);
  CHECK_EQ(GET("/api/v2/state", TOKEN).status, 429);
  CHECK(apiServerForTest().resHeaders.count("Retry-After") == 1);
  g_millis += 59000; CHECK_EQ(GET("/api/v2/state", TOKEN).status, 429);
  g_millis += 2000;  CHECK_EQ(GET("/api/v2/state", TOKEN).status, 200);

  // the public routes stay public
  CHECK_EQ(GET("/api/v2/info").status, 200);
}

static void test_routing() {
  printf("api: methods, legacy routes, unknown routes\n");
  claimedBoard();
  CHECK_EQ(POST("/api/v2/device/add", TOKEN, dev("Lamp", "light", 23)).status, 200);
  const std::string id = cfg.devices[0].id;
  CHECK(!rt[0].on);

  // writes are POST only: a GET to a write route is "not found", whatever the credentials
  CHECK_EQ(GET("/api/v2/set", TOKEN).status, 404);
  CHECK_EQ(GET("/api/v2/device/add", TOKEN).status, 404);
  CHECK_EQ(GET("/api/v2/config/mqtt", TOKEN).status, 404);
  CHECK_EQ(call(HTTP_GET, "/api/v2/set?id=" + id + "&on=1", TOKEN).status, 404);
  CHECK(!rt[0].on);                                                           // and nothing happened
  CHECK_EQ(POST("/api/v2/state", TOKEN, "{}").status, 404);                     // reads are GET only
  CHECK_EQ(POST("/api/v2/info", "", "{}").status, 404);

  // the v3 routes (query-string API, no auth by default) are gone
  Resp old = GET("/api/state");
  CHECK_EQ(old.status, 410);
  CHECK_EQ(call(HTTP_GET, "/api/set?id=" + id + "&on=1&key=whatever").status, 410);
  CHECK_EQ(call(HTTP_POST, "/api/wifi/connect?ssid=x&pass=y").status, 410);
  CHECK(!rt[0].on);
  CHECK_EQ(GET("/nothing").status, 404);
  CHECK_EQ(GET("/api/v2/nothing", TOKEN).status, 404);

  // the root page shows no data at all
  Resp root = call(HTTP_GET, "/");
  CHECK_EQ(root.status, 200);
  CHECK(!has(root.body, "Lamp") && !has(root.body, "tokenHash") && !has(root.body, "192.168"));
}

static void test_no_secret_leaks() {
  printf("api: secrets never come back out\n");
  claimedBoard();
  copyStr(cfg.wifi.ssid, sizeof(cfg.wifi.ssid), "HomeNet");
  copyStr(cfg.wifi.password, sizeof(cfg.wifi.password), "wifi-secret-123");
  CHECK_EQ(POST("/api/v2/config/mqtt", TOKEN, "{\"enabled\":true,\"provider\":\"hivemq\",\"host\":\"abc123.s1.eu.hivemq.cloud\",\"username\":\"board-1\",\"password\":\"mqtt-secret-456\",\"homeId\":\"myhome\"}").status, 200);
  const std::string hash = cfg.auth.tokenHash;
  const char* secrets[] = {"wifi-secret-123", "mqtt-secret-456", "ABCDE-FGHJK"};
  const char* uris[] = {"/api/v2/config", "/api/v2/state", "/api/v2/wifi/status", "/api/v2/wifi/scan"};
  for (const char* u : uris) {
    Resp r = GET(u, TOKEN);
    CHECK_EQ(r.status, 200);
    for (const char* s : secrets) CHECK(!has(r.body, s));
    CHECK(!has(r.body, hash) && !has(r.body, TOKEN) && !has(r.body, "tokenHash") && !has(r.body, "claimCode"));
  }
  Resp cfgDoc = GET("/api/v2/config", TOKEN);
  CHECK(cfgDoc.doc["mqtt"]["hasPassword"].as<bool>());
  CHECK(cfgDoc.doc["wifi"]["hasPassword"].as<bool>());
  CHECK(cfgDoc.doc["mqtt"]["password"].isNull() && cfgDoc.doc["wifi"]["password"].isNull());
  CHECK_EQ(std::string(cfgDoc.doc["mqtt"]["host"].as<const char*>()), "abc123.s1.eu.hivemq.cloud");
  CHECK(GET("/api/v2/state", TOKEN).doc["mqtt"]["hasPassword"].as<bool>());
  // error answers do not echo secrets either
  Resp bad = POST("/api/v2/config/mqtt", TOKEN, "{\"port\":1883,\"password\":\"mqtt-secret-789\"}");
  CHECK_EQ(bad.status, 400);
  CHECK(!has(bad.body, "mqtt-secret-789"));
  CHECK_EQ(std::string(cfg.mqtt.password), "mqtt-secret-456");        // a rejected request changed nothing
  // the owner gets the recovery code, and only the owner
  CHECK_EQ(POST("/api/v2/wifi/reset", "", "{}").status, 401);
  Resp reset = POST("/api/v2/wifi/reset", TOKEN, "{}");
  CHECK_EQ(reset.status, 200);
  CHECK_EQ(std::string(reset.doc["code"].as<const char*>()), "ZZZZZ-ZZZZZ");
  CHECK_EQ(std::string(reset.doc["hotspot"].as<const char*>()), "SmartHome-AABBCC");
  CHECK(cfg.wifi.ssid[0] == '\0' && g_restarts >= 1);
}

static void test_devices_over_http() {
  printf("api: devices, validation, control, rules\n");
  claimedBoard();
  Resp add = POST("/api/v2/device/add", TOKEN, dev("Lamp", "light", 23));
  CHECK_EQ(add.status, 200);
  CHECK_EQ(std::string(add.doc["id"].as<const char*>()), "d1");
  CHECK(cfg.nDevices == 1 && g_pins[23] == (cfg.board.relayActiveLow ? HIGH : LOW));   // initialised OFF immediately

  CHECK_EQ(POST("/api/v2/device/add", TOKEN, dev("Same pin", "fan", 23)).err(), "pin_duplicate");
  CHECK_EQ(POST("/api/v2/device/add", TOKEN, dev("Flash", "fan", 6)).err(), "pin_not_allowed");
  CHECK_EQ(POST("/api/v2/device/add", TOKEN, dev("Strapping", "fan", 12)).err(), "pin_not_allowed");
  CHECK_EQ(POST("/api/v2/device/add", TOKEN, dev("Boot pulse", "fan", 5)).err(), "pin_not_allowed");
  CHECK_EQ(POST("/api/v2/device/add", TOKEN, dev("DHT", "fan", 4)).err(), "pin_reserved");
  CHECK_EQ(POST("/api/v2/device/add", TOKEN, "{\"name\":\"Hack\",\"icon\":\"fan\",\"pin\":5,\"riskyPinOk\":true}").err(), "pin_not_allowed");   // a client can not whitelist a pin
  CHECK_EQ(POST("/api/v2/device/add", TOKEN, "{\"icon\":\"fan\",\"pin\":25}").err(), "device_name");
  CHECK_EQ(POST("/api/v2/device/add", TOKEN, "{\"name\":\"X\",\"icon\":\"toaster\",\"pin\":25}").err(), "device_icon");
  CHECK_EQ(POST("/api/v2/device/add", TOKEN, "{\"name\":\"X\",\"icon\":\"heater\",\"pin\":25,\"bootState\":\"on\"}").err(), "boot_state_unsafe");
  CHECK_EQ(POST("/api/v2/device/add", TOKEN, "{\"name\":\"X\",\"icon\":\"fan\"}").err(), "pin_required");
  CHECK_EQ(POST("/api/v2/device/add", TOKEN, "{\"name\":\"X\",\"icon\":\"fan\",\"pin\":25,\"room\":\"nope\"}").err(), "device_room");
  CHECK_EQ(POST("/api/v2/device/add", TOKEN, "").err(), "body_required");
  CHECK_EQ(POST("/api/v2/device/add", TOKEN, "{oops").err(), "bad_json");
  CHECK_EQ(POST("/api/v2/device/add", TOKEN, "[1,2]").err(), "bad_json");
  CHECK_EQ(POST("/api/v2/device/add", TOKEN, std::string(9000, 'x')).status, 413);
  CHECK(cfg.nDevices == 1);                                                   // nothing sneaked in

  // update: partial, keeps what is not mentioned; the pin may move to another valid pin
  CHECK_EQ(POST("/api/v2/device/update", TOKEN, "{\"id\":\"d1\",\"name\":\"Reading lamp\"}").status, 200);
  CHECK_EQ(std::string(cfg.devices[0].name), "Reading lamp");
  CHECK(cfg.devices[0].pin == 23 && cfg.devices[0].watts == 12);
  CHECK_EQ(POST("/api/v2/device/update", TOKEN, "{\"id\":\"d1\",\"pin\":22}").status, 200);
  CHECK_EQ(cfg.devices[0].pin, 22);
  CHECK_EQ(POST("/api/v2/device/update", TOKEN, "{\"id\":\"d1\",\"pin\":6}").err(), "pin_not_allowed");
  CHECK_EQ(cfg.devices[0].pin, 22);
  CHECK_EQ(POST("/api/v2/device/update", TOKEN, "{\"id\":\"zz\",\"name\":\"x\"}").status, 404);

  // control
  CHECK_EQ(POST("/api/v2/set", TOKEN, "{\"id\":\"d1\",\"on\":true}").status, 200);
  CHECK(rt[0].on && g_pins[22] == (cfg.board.relayActiveLow ? LOW : HIGH));
  CHECK_EQ(POST("/api/v2/set", TOKEN, "{\"id\":\"d1\",\"on\":\"yes\"}").err(), "bad_on");
  CHECK_EQ(POST("/api/v2/set", TOKEN, "{\"id\":\"nope\",\"on\":true}").status, 404);
  CHECK(rt[0].on);

  // DND silences the siren; locks and the never-unlock rule
  CHECK_EQ(POST("/api/v2/device/add", TOKEN, dev("Siren", "siren", 18)).status, 200);
  CHECK_EQ(POST("/api/v2/device/add", TOKEN, dev("Door", "lock", 21)).status, 200);
  CHECK_EQ(POST("/api/v2/dnd", TOKEN, "{\"on\":true}").status, 200);
  CHECK_EQ(POST("/api/v2/set", TOKEN, "{\"id\":\"d2\",\"on\":true}").status, 409);
  CHECK_EQ(POST("/api/v2/set", TOKEN, "{\"id\":\"d2\",\"on\":true}").err(), "dnd");
  CHECK_EQ(POST("/api/v2/dnd", TOKEN, "{\"on\":false}").status, 200);
  CHECK_EQ(POST("/api/v2/scene", TOKEN, "{\"scene\":\"goodnight\"}").status, 200);
  CHECK(rt[2].on && !rt[0].on);                                               // locked, lamp off
  CHECK_EQ(POST("/api/v2/scene", TOKEN, "{\"scene\":\"nope\"}").status, 404);
  CHECK_EQ(POST("/api/v2/schedule/upsert", TOKEN, "{\"id\":\"s1\",\"name\":\"Open\",\"days\":[1],\"time\":\"07:00\",\"action\":{\"target\":\"d3\",\"state\":\"off\"}}").err(), "action_would_unlock");
  CHECK_EQ(POST("/api/v2/schedule/upsert", TOKEN, "{\"id\":\"s1\",\"name\":\"Lock\",\"days\":[1],\"time\":\"23:00\",\"action\":{\"target\":\"d3\",\"state\":\"lock\"}}").status, 200);
  CHECK_EQ(POST("/api/v2/automation/upsert", TOKEN, "{\"id\":\"a1\",\"name\":\"Loop\",\"trigger\":{\"type\":\"device\",\"device\":\"d1\",\"state\":\"on\"},\"actions\":[{\"target\":\"d1\",\"state\":\"off\"}]}").err(), "automation_loop");
  CHECK_EQ(POST("/api/v2/automation/upsert", TOKEN, "{\"id\":\"a1\",\"name\":\"Ok\",\"trigger\":{\"type\":\"device\",\"device\":\"d1\",\"state\":\"on\"},\"actions\":[{\"target\":\"d2\",\"state\":\"off\"}]}").status, 200);
  CHECK_EQ(POST("/api/v2/device/delete", TOKEN, "{\"id\":\"d3\"}").err(), "device_in_use");   // the schedule still needs the door
  CHECK_EQ(POST("/api/v2/schedule/delete", TOKEN, "{\"id\":\"s1\"}").status, 200);
  CHECK_EQ(POST("/api/v2/device/delete", TOKEN, "{\"id\":\"d3\"}").status, 200);
  CHECK(cfg.nDevices == 2);
  CHECK_EQ(POST("/api/v2/device/delete", TOKEN, "{\"id\":\"d1\"}").err(), "device_in_use");     // automation a1 uses d1 as its trigger
  CHECK_EQ(POST("/api/v2/automation/delete", TOKEN, "{\"id\":\"a1\"}").status, 200);
  CHECK_EQ(POST("/api/v2/device/delete", TOKEN, "{\"id\":\"d1\"}").status, 200);
}

static void test_config_endpoints() {
  printf("api: board/mqtt/ca/replace/rotate/factory reset\n");
  claimedBoard();
  g_reconfigure = 0;
  CHECK_EQ(POST("/api/v2/config/mqtt", TOKEN, "{\"enabled\":true,\"host\":\"abc123.s1.eu.hivemq.cloud\",\"username\":\"u\",\"password\":\"p\",\"port\":1883}").err(), "mqtt_port");   // no plaintext port for presets
  CHECK_EQ(POST("/api/v2/config/mqtt", TOKEN, "{\"enabled\":true,\"username\":\"u\",\"password\":\"p\"}").err(), "mqtt_host");
  CHECK_EQ(POST("/api/v2/config/mqtt", TOKEN, "{\"enabled\":true,\"host\":\"10.0.0.1\",\"username\":\"u\",\"password\":\"p\"}").status, 400);   // an IP address has no certificate to verify
  CHECK_EQ(POST("/api/v2/config/mqtt", TOKEN, "{\"homeId\":\"A/B\"}").err(), "mqtt_home_id");
  CHECK_EQ(POST("/api/v2/config/mqtt", TOKEN, "{\"provider\":\"insecure\"}").err(), "mqtt_provider");
  CHECK(!cfg.mqtt.enabled && g_reconfigure == 0);
  CHECK_EQ(POST("/api/v2/config/mqtt", TOKEN, "{\"enabled\":true,\"provider\":\"emqx\",\"host\":\"x1.ala.eu-central-1.emqxsl.com\",\"username\":\"u\",\"password\":\"p\",\"homeId\":\"myhome\"}").status, 200);
  CHECK(cfg.mqtt.enabled && cfg.mqtt.port == 8883 && g_reconfigure == 1);

  CHECK_EQ(POST("/api/v2/config/ca", TOKEN, "tiny").err(), "ca_size");
  std::string key = "-----BEGIN CERTIFICATE-----\n" + std::string(300, 'A') + "\n-----END CERTIFICATE-----\n-----BEGIN PRIVATE KEY-----\nxx\n-----END PRIVATE KEY-----\n";
  CHECK_EQ(POST("/api/v2/config/ca", TOKEN, key).err(), "ca_contains_private_key");
  CHECK_EQ(POST("/api/v2/config/ca", TOKEN, std::string(300, 'A')).err(), "ca_format");
  CHECK_EQ(POST("/api/v2/config/ca", TOKEN, "-----BEGIN CERTIFICATE-----\n" + std::string(300, 'A') + "\n-----END CERTIFICATE-----\n").status, 200);
  CHECK(g_fs.count("/ca.pem") == 1);
  CHECK_EQ(POST("/api/v2/config/ca/remove", TOKEN, "{}").status, 200);
  CHECK(g_fs.count("/ca.pem") == 0);

  g_tzApplied = 0;
  CHECK_EQ(POST("/api/v2/config/board", TOKEN, "{\"tz\":\"bad tz!\"}").err(), "timezone");
  CHECK_EQ(POST("/api/v2/config/board", TOKEN, "{\"profile\":\"esp32-s3\"}").err(), "profile_mismatch");   // this firmware runs on a classic ESP32
  CHECK_EQ(POST("/api/v2/config/board", TOKEN, "{\"name\":\"Lounge\",\"tz\":\"IST-5:30\",\"psram\":true}").status, 200);
  CHECK(std::string(cfg.board.tz) == "IST-5:30" && cfg.board.psram && gPsram && g_tzApplied == 1);
  CHECK_EQ(POST("/api/v2/device/add", TOKEN, dev("Lamp", "light", 16)).err(), "pin_not_allowed");       // PSRAM flag now reserves GPIO16

  CHECK_EQ(POST("/api/v2/config/replace", TOKEN, "{\"devices\":[{\"id\":\"x\",\"name\":\"X\",\"icon\":\"fan\",\"pin\":6}]}").err(), "pin_not_allowed");
  CHECK_EQ(POST("/api/v2/config/replace", TOKEN, "{\"auth\":{\"claimed\":false,\"tokenHash\":\"\"},\"rooms\":[{\"id\":\"a\",\"name\":\"A\",\"rev\":1}]}").status, 200);
  CHECK(cfg.auth.claimed && std::string(cfg.auth.tokenHash) == tokenHash(TOKEN));                       // a client can not reset or replace the credentials
  CHECK_EQ(GET("/api/v2/state", TOKEN).status, 200);

  // token rotation: the old token dies with the request
  CHECK_EQ(POST("/api/v2/auth/rotate", TOKEN, "{\"token\":\"short\"}").err(), "bad_token");
  CHECK_EQ(POST("/api/v2/auth/rotate", "", "{\"token\":\"" + TOKEN2 + "\"}").status, 401);
  CHECK_EQ(POST("/api/v2/auth/rotate", TOKEN, "{\"token\":\"" + TOKEN2 + "\"}").status, 200);
  CHECK_EQ(GET("/api/v2/state", TOKEN).status, 401);
  CHECK_EQ(GET("/api/v2/state", TOKEN2).status, 200);

  // wifi
  CHECK_EQ(POST("/api/v2/wifi/connect", TOKEN2, "{\"ssid\":\"New\",\"password\":\"short\"}").err(), "password_length");
  CHECK_EQ(POST("/api/v2/wifi/connect", TOKEN2, "{\"ssid\":\"New\",\"password\":\"longenough1\"}").status, 200);
  CHECK(g_lastConnectSsid == "New");
  CHECK_EQ(call(HTTP_POST, "/api/v2/wifi/connect?ssid=Evil&password=longenough1", TOKEN2).status, 400);  // query parameters are ignored: no body, no connect
  CHECK(g_lastConnectSsid == "New");

  // factory reset needs the explicit confirmation
  g_restarts = 0;
  CHECK_EQ(POST("/api/v2/factory-reset", TOKEN2, "{}").err(), "confirm_required");
  CHECK_EQ(POST("/api/v2/factory-reset", TOKEN2, "{\"confirm\":\"yes\"}").status, 400);
  CHECK(g_fs.count("/config.json") == 1 && g_restarts == 0);
  CHECK_EQ(POST("/api/v2/factory-reset", TOKEN2, "{\"confirm\":\"RESET\"}").status, 200);
  CHECK(g_fs.count("/config.json") == 0 && g_restarts == 1);
}

int main() {
  apiBegin();     // registers the real routes once
  test_unclaimed_and_claim();
  test_claim_lockout();
  test_authentication();
  test_routing();
  test_no_secret_leaks();
  test_devices_over_http();
  test_config_endpoints();
  printf("\n%d checks passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
