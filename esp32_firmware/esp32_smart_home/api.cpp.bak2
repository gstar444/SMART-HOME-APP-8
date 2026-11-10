#include "api.h"

#include <WebServer.h>
#include <WiFi.h>

#include "automation.h"
#include "cloud.h"
#include "config_store.h"
#include "devices.h"
#include "net.h"
#include "sha256.h"
#include "snapshot.h"

static WebServer server(80);
static FailLimiter authLimiter(5, 60000UL, 60000UL);
static FailLimiter claimLimiter(5, 300000UL, 300000UL);

// ------------------------------------------------------------------ response helpers
static void sendJson(int code, JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("X-Content-Type-Options", "nosniff");
  server.send(code, "application/json", out);
}
static void sendOk() {
  JsonDocument d;
  d["ok"] = true;
  sendJson(200, d);
}
static void sendErr(int code, const char* err) {
  JsonDocument d;
  d["ok"] = false;
  d["error"] = err;
  sendJson(code, d);
}
static int statusFor(const char* e) {
  if (!strncmp(e, "unknown_", 8)) return 404;
  if (!strcmp(e, "save_failed")) return 500;
  static const char* const conflicts[] = {"disabled", "pin_fault", "read_only", "dnd", "busy", "last_room",
                                          "device_in_use", "config_read_only", "pin_duplicate", "device_id_duplicate",
                                          "not_claimed", "already_claimed"};
  if (!strncmp(e, "too_many", 8)) return 409;
  for (size_t i = 0; i < sizeof(conflicts) / sizeof(conflicts[0]); i++)
    if (!strcmp(e, conflicts[i])) return 409;
  return 400;
}
static void sendErrFor(const char* e) { sendErr(statusFor(e), e); }

static bool readJson(JsonDocument& doc) {
  if (!server.hasArg("plain")) { sendErr(400, "body_required"); return false; }
  const String& body = server.arg("plain");
  if (body.length() > 8192) { sendErr(413, "body_too_large"); return false; }
  if (deserializeJson(doc, body)) { sendErr(400, "bad_json"); return false; }
  if (!doc.is<JsonObject>()) { sendErr(400, "bad_json"); return false; }
  return true;
}
#define BODY(name) JsonDocument name; if (!readJson(name)) return;
#define AUTH() do { if (!authorized()) return; } while (0)

// ------------------------------------------------------------------ authentication
static bool authorized() {
  uint32_t now = millis();
  if (!cfg.auth.claimed) { sendErr(403, "not_claimed"); return false; }
  if (authLimiter.locked(now)) { server.sendHeader("Retry-After", "60"); sendErr(429, "too_many_attempts"); return false; }
  bool ok = false;
  String h = server.header("Authorization");
  if (h.length() > 7 && h.startsWith("Bearer ")) {
    const char* tok = h.c_str() + 7;
    if (validTokenText(tok)) {
      uint8_t digest[32], stored[32];
      sha256((const uint8_t*)tok, strlen(tok), digest);
      ok = fromHex(cfg.auth.tokenHash, stored, 32) && ctEqual(digest, stored, 32);   // constant time
    }
  }
  if (!ok) { authLimiter.fail(now); sendErr(401, "unauthorized"); return false; }
  authLimiter.success();
  return true;
}

static bool storeToken(const char* tok) {
  uint8_t digest[32];
  sha256((const uint8_t*)tok, strlen(tok), digest);
  toHex(digest, 32, cfg.auth.tokenHash);
  return true;
}

// ------------------------------------------------------------------ generic list helpers
template <typename T>
static const char* upsertAndSave(T* arr, uint8_t& n, uint8_t cap, int found, const T& item) {
  if (found < 0 && n >= cap) return "too_many_items";
  int i = found >= 0 ? found : n;
  T old = arr[i];
  uint8_t oldN = n;
  arr[i] = item;
  if (found < 0) n++;
  if (configSave()) return nullptr;
  arr[i] = old;                       // roll back so RAM never disagrees with flash
  n = oldN;
  return "save_failed";
}
template <typename T>
static bool removeAndSave(T* arr, uint8_t& n, int i) {
  T removed = arr[i];
  memmove(&arr[i], &arr[i + 1], sizeof(T) * (n - i - 1));
  n--;
  if (configSave()) return true;
  memmove(&arr[i + 1], &arr[i], sizeof(T) * (n - i));
  arr[i] = removed;
  n++;
  return false;
}
static const char* idFromBody(JsonDocument& b) {
  return b["id"].is<const char*>() ? b["id"].as<const char*>() : "";
}

// ------------------------------------------------------------------ public routes
static void hRoot() {
  server.sendHeader("Cache-Control", "no-store");
  String page = F("<!doctype html><meta charset=utf-8><title>Smart Home board</title><body style='font-family:sans-serif'><h3>Smart Home board ");
  page += gBoardId;
  page += F("</h3><p>Use the Smart Home app. This page shows no data.</p>");
  server.send(200, "text/html", page);
}

static void hInfo() {   // no secrets, nothing about the home: lets the app recognise a board before it is claimed
  JsonDocument d;
  d["ok"] = true;
  d["api"] = 2;
  d["board"] = (const char*)gBoardId;
  d["fw"] = FW_VERSION;
  d["chip"] = targetName(gTarget);
  d["claimed"] = cfg.auth.claimed;
  sendJson(200, d);
}

static void hClaim() {
  uint32_t now = millis();
  if (cfg.auth.claimed) { sendErr(409, "already_claimed"); return; }
  if (claimLimiter.locked(now)) { server.sendHeader("Retry-After", "300"); sendErr(429, "too_many_attempts"); return; }
  BODY(b);
  const char* code = b["code"].is<const char*>() ? b["code"].as<const char*>() : "";
  const char* tok = b["token"].is<const char*>() ? b["token"].as<const char*>() : "";
  if (!claimCodeMatches(cfg.auth.claimCode, code)) {   // proof of physical access (serial console)
    claimLimiter.fail(now);
    sendErr(403, "bad_code");
    return;
  }
  if (!validTokenText(tok)) { sendErr(400, "bad_token"); return; }
  storeToken(tok);
  cfg.auth.claimed = true;                              // claim code is kept until Wi-Fi is confirmed (see net.h)
  if (!configSave()) {
    cfg.auth.claimed = false;
    cfg.auth.tokenHash[0] = '\0';
    sendErr(500, "save_failed");
    return;
  }
  claimLimiter.success();
  JsonDocument d;
  d["ok"] = true;
  d["board"] = (const char*)gBoardId;
  sendJson(200, d);
}

// ------------------------------------------------------------------ authenticated reads
static void hState() {
  AUTH();
  JsonDocument d;
  d["ok"] = true;
  d["api"] = 2;
  snapshotInventory(d["inventory"].to<JsonObject>(), true);
  snapshotState(d["state"].to<JsonObject>());
  netWifiStatusToJson(d["wifi"].to<JsonObject>());
  JsonObject m = d["mqtt"].to<JsonObject>();         // no credentials, ever
  m["enabled"] = cfg.mqtt.enabled;
  m["provider"] = (const char*)cfg.mqtt.provider;
  m["host"] = (const char*)cfg.mqtt.host;
  m["homeId"] = (const char*)cfg.mqtt.homeId;
  m["status"] = cloudStatus();
  m["hasPassword"] = cfg.mqtt.password[0] != '\0';
  JsonObject t = d["time"].to<JsonObject>();
  t["valid"] = timeValid();
  t["tz"] = (const char*)cfg.board.tz;
  sendJson(200, d);
}

static void hConfigGet() {
  AUTH();
  JsonDocument d;
  configToJson(d, true);      // redacted: no Wi-Fi/MQTT passwords, no token hash, no claim code
  d["ok"] = true;
  sendJson(200, d);
}

// ------------------------------------------------------------------ control
static void hSet() {
  AUTH(); BODY(b);
  if (!b["on"].is<bool>()) { sendErr(400, "bad_on"); return; }
  int i = devIndex(idFromBody(b));
  const char* e = devSet(i, b["on"].as<bool>(), true);
  if (e) { sendErrFor(e); return; }
  sendOk();
}
static void hScene() {
  AUTH(); BODY(b);
  const char* e = devRunScene(b["scene"].is<const char*>() ? b["scene"].as<const char*>() : "", false);
  if (e) { sendErrFor(e); return; }
  sendOk();
}
static void hDnd() {
  AUTH(); BODY(b);
  if (!b["on"].is<bool>()) { sendErr(400, "bad_on"); return; }
  devSetDnd(b["on"].as<bool>());
  sendOk();
}

// ------------------------------------------------------------------ devices
static bool deviceInUse(const char* id) {
  for (int i = 0; i < cfg.nScenes; i++)
    for (int a = 0; a < cfg.scenes[i].nActions; a++) if (!strcmp(cfg.scenes[i].actions[a].target, id)) return true;
  for (int i = 0; i < cfg.nSchedules; i++) if (!strcmp(cfg.schedules[i].action.target, id)) return true;
  for (int i = 0; i < cfg.nAutomations; i++) {
    if (cfg.automations[i].trigType == TRIG_DEVICE && !strcmp(cfg.automations[i].trigDevice, id)) return true;
    for (int a = 0; a < cfg.automations[i].nActions; a++) if (!strcmp(cfg.automations[i].actions[a].target, id)) return true;
  }
  return false;
}

static void afterDeviceChange() {
  devicesReconcile();
  automationConfigChanged();
  cloudInventoryChanged();
}

static void hDeviceAdd() {
  AUTH(); BODY(b);
  bool generated = false;
  if (!b["id"].is<const char*>()) {
    b["id"] = String("d") + String((unsigned long)cfg.nextDeviceId);
    generated = true;
  }
  CfgDevice d;
  const char* e = parseDeviceJson(b.as<JsonObjectConst>(), nullptr, false, d);
  if (!e) e = validateDeviceStrict(cfg, d, -1, gTarget, gPsram, dhtReservedPin());
  if (e) { sendErrFor(e); return; }
  uint32_t oldNext = cfg.nextDeviceId;
  if (generated) cfg.nextDeviceId++;
  e = upsertAndSave(cfg.devices, cfg.nDevices, MAX_DEVICES, -1, d);
  if (e) { cfg.nextDeviceId = oldNext; sendErrFor(e); return; }
  afterDeviceChange();
  JsonDocument r;
  r["ok"] = true;
  r["id"] = (const char*)d.id;
  sendJson(200, r);
}

static void hDeviceUpdate() {
  AUTH(); BODY(b);
  int i = findDeviceCfg(cfg, idFromBody(b));
  if (i < 0) { sendErr(404, "unknown_device"); return; }
  CfgDevice base = cfg.devices[i], d;
  const char* e = parseDeviceJson(b.as<JsonObjectConst>(), &base, false, d);
  if (!e) e = validateDeviceStrict(cfg, d, i, gTarget, gPsram, dhtReservedPin());
  if (e) { sendErrFor(e); return; }
  e = upsertAndSave(cfg.devices, cfg.nDevices, MAX_DEVICES, i, d);
  if (e) { sendErrFor(e); return; }
  afterDeviceChange();
  sendOk();
}

static void hDeviceDelete() {
  AUTH(); BODY(b);
  const char* id = idFromBody(b);
  int i = findDeviceCfg(cfg, id);
  if (i < 0) { sendErr(404, "unknown_device"); return; }
  if (deviceInUse(id)) { sendErr(409, "device_in_use"); return; }   // remove the scene/schedule/automation first
  if (!removeAndSave(cfg.devices, cfg.nDevices, i)) { sendErr(500, "save_failed"); return; }
  afterDeviceChange();
  sendOk();
}

// ------------------------------------------------------------------ rooms
static void hRoomUpsert() {
  AUTH(); BODY(b);
  CfgRoom in;
  memset(&in, 0, sizeof(in));
  copyStr(in.id, sizeof(in.id), idFromBody(b));
  copyStr(in.name, sizeof(in.name), b["name"].is<const char*>() ? b["name"].as<const char*>() : "");
  cleanText(in.name);
  int cur = findRoom(cfg, in.id);
  long rev = b["rev"].is<long>() ? b["rev"].as<long>() : (cur >= 0 ? (long)cfg.rooms[cur].rev + 1 : 1);
  in.rev = rev > 0 ? (uint32_t)rev : 0;
  bool changed = false;
  const char* e = configRoomUpsert(in, &changed);
  if (e) { sendErrFor(e); return; }
  if (changed) cloudInventoryChanged();
  JsonDocument r;
  r["ok"] = true;
  r["changed"] = changed;
  r["rev"] = in.rev;
  sendJson(200, r);
}
static void hRoomDelete() {
  AUTH(); BODY(b);
  const char* id = idFromBody(b);
  int cur = findRoom(cfg, id);
  long rev = b["rev"].is<long>() ? b["rev"].as<long>() : (cur >= 0 ? (long)cfg.rooms[cur].rev + 1 : 1);
  bool changed = false;
  const char* e = configRoomDelete(id, rev > 0 ? (uint32_t)rev : 0, &changed);
  if (e) { sendErrFor(e); return; }
  if (changed) { devicesReconcile(); cloudInventoryChanged(); }
  JsonDocument r;
  r["ok"] = true;
  r["changed"] = changed;
  sendJson(200, r);
}

// ------------------------------------------------------------------ scenes / schedules / automations
static void hSceneUpsert() {
  AUTH(); BODY(b);
  CfgScene s;
  const char* e = parseSceneJson(b.as<JsonObjectConst>(), s);
  if (!e) e = validateScene(cfg, s);
  if (e) { sendErrFor(e); return; }
  e = upsertAndSave(cfg.scenes, cfg.nScenes, MAX_SCENES, findSceneCfg(cfg, s.id), s);
  if (e) { sendErrFor(e); return; }
  cloudInventoryChanged();
  sendOk();
}
static void hSceneDelete() {
  AUTH(); BODY(b);
  int i = findSceneCfg(cfg, idFromBody(b));
  if (i < 0) { sendErr(404, "unknown_scene"); return; }
  if (!removeAndSave(cfg.scenes, cfg.nScenes, i)) { sendErr(500, "save_failed"); return; }
  cloudInventoryChanged();
  sendOk();
}
static void hScheduleUpsert() {
  AUTH(); BODY(b);
  CfgSchedule s;
  const char* e = parseScheduleJson(b.as<JsonObjectConst>(), s);
  if (!e) e = validateSchedule(cfg, s);
  if (e) { sendErrFor(e); return; }
  e = upsertAndSave(cfg.schedules, cfg.nSchedules, MAX_SCHEDULES, findScheduleCfg(cfg, s.id), s);
  if (e) { sendErrFor(e); return; }
  automationConfigChanged();
  cloudInventoryChanged();
  sendOk();
}
static void hScheduleDelete() {
  AUTH(); BODY(b);
  int i = findScheduleCfg(cfg, idFromBody(b));
  if (i < 0) { sendErr(404, "unknown_schedule"); return; }
  if (!removeAndSave(cfg.schedules, cfg.nSchedules, i)) { sendErr(500, "save_failed"); return; }
  automationConfigChanged();
  cloudInventoryChanged();
  sendOk();
}
static void hAutomationUpsert() {
  AUTH(); BODY(b);
  CfgAutomation a;
  const char* e = parseAutomationJson(b.as<JsonObjectConst>(), a);
  if (!e) e = validateAutomation(cfg, a);
  if (e) { sendErrFor(e); return; }
  e = upsertAndSave(cfg.automations, cfg.nAutomations, MAX_AUTOMATIONS, findAutomationCfg(cfg, a.id), a);
  if (e) { sendErrFor(e); return; }
  automationConfigChanged();
  cloudInventoryChanged();
  sendOk();
}
static void hAutomationDelete() {
  AUTH(); BODY(b);
  int i = findAutomationCfg(cfg, idFromBody(b));
  if (i < 0) { sendErr(404, "unknown_automation"); return; }
  if (!removeAndSave(cfg.automations, cfg.nAutomations, i)) { sendErr(500, "save_failed"); return; }
  automationConfigChanged();
  cloudInventoryChanged();
  sendOk();
}

// ------------------------------------------------------------------ board / mqtt / ca / replace / token
static void hBoardConfig() {
  AUTH(); BODY(b);
  CfgBoard nb = cfg.board;
  JsonObjectConst o = b.as<JsonObjectConst>();
  if (o["name"].is<const char*>()) { copyStr(nb.name, sizeof(nb.name), o["name"].as<const char*>()); cleanText(nb.name); }
  if (o["tz"].is<const char*>()) copyStr(nb.tz, sizeof(nb.tz), o["tz"].as<const char*>());
  if (o["profile"].is<const char*>()) copyStr(nb.profile, sizeof(nb.profile), o["profile"].as<const char*>());
  if (o["psram"].is<bool>()) nb.psram = o["psram"].as<bool>();
  if (o["relayActiveLow"].is<bool>()) nb.relayActiveLow = o["relayActiveLow"].as<bool>();
  if (o["resetPin"].is<int>()) nb.resetPin = (int16_t)o["resetPin"].as<int>();
  const char* e = validateBoard(nb);
  BoardTarget t;
  if (!e && strcmp(nb.profile, "auto") != 0 && (!targetFromName(nb.profile, &t) || t != gTarget)) e = "profile_mismatch";
  if (e) { sendErrFor(e); return; }
  CfgBoard old = cfg.board;
  cfg.board = nb;
  if (!configSave()) { cfg.board = old; sendErr(500, "save_failed"); return; }
  gPsram = SH_ASSUME_PSRAM || cfg.board.psram || psramFound();
  netApplyTimezone();
  devicesReconcile();        // PSRAM flag / relay polarity can change which pins are usable
  cloudInventoryChanged();
  sendOk();
}

static void hMqttConfig() {
  AUTH(); BODY(b);
  CfgMqtt m = cfg.mqtt;
  JsonObjectConst o = b.as<JsonObjectConst>();
  if (o["enabled"].is<bool>()) m.enabled = o["enabled"].as<bool>();
  if (o["remoteConfig"].is<bool>()) m.remoteConfig = o["remoteConfig"].as<bool>();
  if (o["provider"].is<const char*>()) copyStr(m.provider, sizeof(m.provider), o["provider"].as<const char*>());
  if (o["host"].is<const char*>()) copyStr(m.host, sizeof(m.host), o["host"].as<const char*>());
  if (o["port"].is<int>()) { int p = o["port"].as<int>(); m.port = (p > 0 && p < 65536) ? (uint16_t)p : 0; }
  if (o["username"].is<const char*>()) copyStr(m.username, sizeof(m.username), o["username"].as<const char*>());
  if (o["password"].is<const char*>()) copyStr(m.password, sizeof(m.password), o["password"].as<const char*>());
  if (o["homeId"].is<const char*>()) copyStr(m.homeId, sizeof(m.homeId), o["homeId"].as<const char*>());
  const char* e = validateMqtt(m);
  if (e) { sendErrFor(e); return; }
  CfgMqtt old = cfg.mqtt;
  cfg.mqtt = m;
  if (!configSave()) { cfg.mqtt = old; sendErr(500, "save_failed"); return; }
  cloudReconfigure();
  sendOk();
}

static void hCaSet() {
  AUTH();
  if (!server.hasArg("plain")) { sendErr(400, "body_required"); return; }
  const char* e = customCaSave(server.arg("plain").c_str());
  if (e) { sendErrFor(e); return; }
  cloudReconfigure();
  sendOk();
}
static void hCaRemove() {
  AUTH();
  customCaRemove();
  cloudReconfigure();
  sendOk();
}

static void hConfigReplace() {
  AUTH(); BODY(b);
  const char* e = configReplaceFromJson(b.as<JsonVariantConst>());
  if (e) { sendErrFor(e); return; }
  gPsram = SH_ASSUME_PSRAM || cfg.board.psram || psramFound();
  netApplyTimezone();
  afterDeviceChange();
  cloudReconfigure();
  sendOk();
}

static void hTokenRotate() {
  AUTH(); BODY(b);
  const char* tok = b["token"].is<const char*>() ? b["token"].as<const char*>() : "";
  if (!validTokenText(tok)) { sendErr(400, "bad_token"); return; }
  char oldHash[65];
  copyStr(oldHash, sizeof(oldHash), cfg.auth.tokenHash);
  storeToken(tok);
  if (!configSave()) { copyStr(cfg.auth.tokenHash, sizeof(cfg.auth.tokenHash), oldHash); sendErr(500, "save_failed"); return; }
  sendOk();                  // the old token stopped working with this save
}

// ------------------------------------------------------------------ Wi-Fi
static void hWifiStatus() {
  AUTH();
  JsonDocument d;
  d["ok"] = true;
  netWifiStatusToJson(d.as<JsonObject>());
  sendJson(200, d);
}
static void hWifiScanStart() { AUTH(); netScanRequest(); sendOk(); }
static void hWifiScanResult() {
  AUTH();
  JsonDocument d;
  d["ok"] = true;
  netScanToJson(d.as<JsonObject>());
  sendJson(200, d);
}
static void hWifiConnect() {
  AUTH(); BODY(b);
  const char* ssid = b["ssid"].is<const char*>() ? b["ssid"].as<const char*>() : "";
  const char* pass = b["password"].is<const char*>() ? b["password"].as<const char*>() : "";
  const char* err = nullptr;
  if (!netRequestConnect(ssid, pass, &err)) { sendErrFor(err); return; }
  JsonDocument d;
  d["ok"] = true;
  d["pending"] = true;
  sendJson(200, d);
}
static void hWifiReset() {
  AUTH();
  netForgetWifi();
  JsonDocument d;                       // the recovery code is shown to the authenticated owner only
  d["ok"] = true;
  d["hotspot"] = netApSsid();
  d["code"] = (const char*)cfg.auth.claimCode;
  sendJson(200, d);
}

static void hFactoryReset() {
  AUTH(); BODY(b);
  if (strcmp(b["confirm"].is<const char*>() ? b["confirm"].as<const char*>() : "", "RESET") != 0) { sendErr(400, "confirm_required"); return; }
  configWipeAll();
  netRestartSoon(1500);
  sendOk();
}

// ------------------------------------------------------------------ routing
static void hNotFound() {
  String uri = server.uri();
  if (uri.startsWith("/api/v2/")) { sendErr(404, "not_found_or_wrong_method"); return; }
  if (uri.startsWith("/api/")) { sendErr(410, "api_v1_removed_use_api_v2"); return; }   // legacy unauthenticated routes
  sendErr(404, "not_found");
}

void apiBegin() {
  static const char* hdrs[] = {"Authorization"};   // WebServer::collectHeaders wants a non-const array
  server.collectHeaders(hdrs, 1);
  server.on("/", HTTP_GET, hRoot);
  server.on("/api/v2/info", HTTP_GET, hInfo);
  server.on("/api/v2/claim", HTTP_POST, hClaim);

  server.on("/api/v2/state", HTTP_GET, hState);
  server.on("/api/v2/config", HTTP_GET, hConfigGet);
  server.on("/api/v2/wifi/status", HTTP_GET, hWifiStatus);
  server.on("/api/v2/wifi/scan", HTTP_GET, hWifiScanResult);

  server.on("/api/v2/set", HTTP_POST, hSet);
  server.on("/api/v2/scene", HTTP_POST, hScene);
  server.on("/api/v2/dnd", HTTP_POST, hDnd);

  server.on("/api/v2/device/add", HTTP_POST, hDeviceAdd);
  server.on("/api/v2/device/update", HTTP_POST, hDeviceUpdate);
  server.on("/api/v2/device/delete", HTTP_POST, hDeviceDelete);
  server.on("/api/v2/room/upsert", HTTP_POST, hRoomUpsert);
  server.on("/api/v2/room/delete", HTTP_POST, hRoomDelete);
  server.on("/api/v2/scene/upsert", HTTP_POST, hSceneUpsert);
  server.on("/api/v2/scene/delete", HTTP_POST, hSceneDelete);
  server.on("/api/v2/schedule/upsert", HTTP_POST, hScheduleUpsert);
  server.on("/api/v2/schedule/delete", HTTP_POST, hScheduleDelete);
  server.on("/api/v2/automation/upsert", HTTP_POST, hAutomationUpsert);
  server.on("/api/v2/automation/delete", HTTP_POST, hAutomationDelete);

  server.on("/api/v2/config/board", HTTP_POST, hBoardConfig);
  server.on("/api/v2/config/mqtt", HTTP_POST, hMqttConfig);
  server.on("/api/v2/config/ca", HTTP_POST, hCaSet);
  server.on("/api/v2/config/ca/remove", HTTP_POST, hCaRemove);
  server.on("/api/v2/config/replace", HTTP_POST, hConfigReplace);
  server.on("/api/v2/auth/rotate", HTTP_POST, hTokenRotate);

  server.on("/api/v2/wifi/scan", HTTP_POST, hWifiScanStart);
  server.on("/api/v2/wifi/connect", HTTP_POST, hWifiConnect);
  server.on("/api/v2/wifi/reset", HTTP_POST, hWifiReset);
  server.on("/api/v2/factory-reset", HTTP_POST, hFactoryReset);

  server.onNotFound(hNotFound);
  server.begin();
}

void apiTick() { server.handleClient(); }

#ifdef SH_HOST_TEST
WebServer& apiServerForTest() { return server; }   // host tests dispatch requests through the real handlers
#endif
