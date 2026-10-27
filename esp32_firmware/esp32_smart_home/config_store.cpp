#include "config_store.h"

#include <LittleFS.h>
#include <Preferences.h>

static const char* const kCfg = "/config.json";
static const char* const kBak = "/config.bak";
static const char* const kTmp = "/config.tmp";
static const char* const kState = "/state.json";
static const char* const kCa = "/ca.pem";
static const char* const kLegacyNs = "smarthome";   // firmware v3 NVS namespace

Config cfg;
static Config gScratch;            // working copy for parsing/migration (keeps ~12 KB off the 8 KB task stack)
static bool gReadOnly = false;
ConfigReport gLoadReport = {0, 0, 0, 0};
MigrationReport gMigrationReport = {0, 0, 0, 0};

bool configReadOnly() { return gReadOnly; }

const char* configSourceName(ConfigSource s) {
  switch (s) {
    case CFG_FILE: return "file";
    case CFG_BACKUP: return "backup";
    case CFG_MIGRATED_V3: return "migrated_from_v3";
    case CFG_LOCKED_NEWER: return "newer_schema_read_only";
    default: return "fresh_defaults";
  }
}

// ------------------------------------------------------------------ JSON helpers
static void jStr(char* dst, size_t cap, JsonVariantConst v, const char* def, bool clean = true) {
  const char* s = v.is<const char*>() ? v.as<const char*>() : nullptr;
  copyStr(dst, cap, s ? s : def);
  if (clean) cleanText(dst);
}
static bool jBool(JsonVariantConst v, bool def) { return v.is<bool>() ? v.as<bool>() : def; }
static bool jIsInt(JsonVariantConst v) { return v.is<long>(); }
static long jInt(JsonVariantConst v, long def) { return v.is<long>() ? v.as<long>() : def; }

static const char* parseAction(JsonVariantConst v, CfgAction& a) {
  if (!v.is<JsonObjectConst>()) return "bad_action";
  JsonObjectConst o = v.as<JsonObjectConst>();
  const char* t = o["target"].is<const char*>() ? o["target"].as<const char*>() : "";
  const char* s = o["state"].is<const char*>() ? o["state"].as<const char*>() : "";
  copyStr(a.target, sizeof(a.target), t);
  if (!actStateFromName(s, &a.state)) return "action_state";
  return nullptr;
}

static void actionToJson(JsonObject o, const CfgAction& a) {
  o["target"] = (const char*)a.target;
  o["state"] = actStateName(a.state);
}

// ------------------------------------------------------------------ per-object parsers
const char* parseDeviceJson(JsonObjectConst o, const CfgDevice* base, bool trustRiskyFlag, CfgDevice& d) {
  if (base) d = *base; else memset(&d, 0, sizeof(d));

  if (!base) {
    if (!o["id"].is<const char*>()) return "device_id";
    copyStr(d.id, sizeof(d.id), o["id"].as<const char*>());
  }
  if (o["name"].is<const char*>()) jStr(d.name, sizeof(d.name), o["name"], "");
  else if (!base) return "device_name";

  bool iconGiven = o["icon"].is<const char*>();
  if (iconGiven) jStr(d.icon, sizeof(d.icon), o["icon"], "plug");
  else if (!base) copyStr(d.icon, sizeof(d.icon), "plug");

  if (o["type"].is<const char*>()) jStr(d.type, sizeof(d.type), o["type"], "switch");
  else if (!base || iconGiven) copyStr(d.type, sizeof(d.type), typeForIcon(d.icon));

  if (o["group"].is<const char*>()) jStr(d.group, sizeof(d.group), o["group"], "");
  else if (!base) copyStr(d.group, sizeof(d.group), groupForIcon(d.icon));

  if (o["room"].is<const char*>()) jStr(d.room, sizeof(d.room), o["room"], "", false);
  else if (!base) d.room[0] = '\0';

  if (jIsInt(o["pin"])) {
    long p = jInt(o["pin"], -1);
    if (p < -1 || p > 255) return "pin_range";
    d.pin = (int16_t)p;
  } else if (!base) {
    return "pin_required";
  }
  bool isLock = !strcmp(d.type, "lock");
  d.invert = jBool(o["invert"], base ? d.invert : isLock);
  d.enabled = jBool(o["enabled"], base ? d.enabled : true);
  d.remote = jBool(o["remote"], base ? d.remote : !isLock);

  if (!o["watts"].isNull()) {
    long w = jInt(o["watts"], -1);
    if (w < 0 || w > 65535) return "device_watts";
    d.watts = (uint16_t)w;
  } else if (!base) {
    d.watts = defaultWatts(d.icon);
  }
  if (!o["autoOffMin"].isNull()) {
    long m = jInt(o["autoOffMin"], -1);
    if (m < 0 || m > 65535) return "device_auto_off";
    d.autoOffMin = (uint16_t)m;
  } else if (!base) {
    d.autoOffMin = defaultAutoOff(d.icon);
  }
  if (o["bootState"].is<const char*>()) {
    if (!bootStateFromName(o["bootState"].as<const char*>(), &d.bootState)) return "device_boot_state";
  } else if (!base) {
    d.bootState = BOOT_OFF;
  }
  // The "pin is risky but grandfathered" flag can come from our own file or from migration only,
  // never from a client request (it would bypass the pin policy).
  if (trustRiskyFlag) d.riskyPinOk = jBool(o["riskyPinOk"], false);
  else if (base && d.pin != base->pin) d.riskyPinOk = false;
  else if (!base) d.riskyPinOk = false;
  return nullptr;
}

const char* parseSceneJson(JsonObjectConst o, CfgScene& s) {
  memset(&s, 0, sizeof(s));
  if (!o["id"].is<const char*>() || !o["name"].is<const char*>()) return "scene_id";
  copyStr(s.id, sizeof(s.id), o["id"].as<const char*>());
  jStr(s.name, sizeof(s.name), o["name"], "");
  if (!o["actions"].is<JsonArrayConst>()) return "scene_actions";
  JsonArrayConst arr = o["actions"].as<JsonArrayConst>();
  if (arr.size() > MAX_SCENE_ACTIONS) return "scene_actions";
  for (JsonVariantConst v : arr) {
    const char* e = parseAction(v, s.actions[s.nActions]);
    if (e) return e;
    s.nActions++;
  }
  return nullptr;
}

const char* parseScheduleJson(JsonObjectConst o, CfgSchedule& s) {
  memset(&s, 0, sizeof(s));
  if (!o["id"].is<const char*>() || !o["name"].is<const char*>()) return "schedule_id";
  copyStr(s.id, sizeof(s.id), o["id"].as<const char*>());
  jStr(s.name, sizeof(s.name), o["name"], "");
  s.enabled = jBool(o["enabled"], true);
  if (!o["days"].is<JsonArrayConst>()) return "schedule_days";
  for (JsonVariantConst d : o["days"].as<JsonArrayConst>()) {
    long v = jInt(d, -1);
    if (v < 0 || v > 6) return "schedule_days";
    s.days |= (uint8_t)(1u << v);
  }
  int minutes = 0;
  if (!o["time"].is<const char*>() || !parseHHMM(o["time"].as<const char*>(), &minutes)) return "schedule_time";
  s.minute = (uint16_t)minutes;
  return parseAction(o["action"], s.action);
}

const char* parseAutomationJson(JsonObjectConst o, CfgAutomation& a) {
  memset(&a, 0, sizeof(a));
  if (!o["id"].is<const char*>() || !o["name"].is<const char*>()) return "automation_id";
  copyStr(a.id, sizeof(a.id), o["id"].as<const char*>());
  jStr(a.name, sizeof(a.name), o["name"], "");
  a.enabled = jBool(o["enabled"], true);
  long cd = jInt(o["cooldownSec"], 30);
  a.cooldownSec = (uint16_t)(cd < 0 ? 0 : (cd > 65535 ? 65535 : cd));

  if (!o["trigger"].is<JsonObjectConst>()) return "trigger_type";
  JsonObjectConst t = o["trigger"].as<JsonObjectConst>();
  const char* type = t["type"].is<const char*>() ? t["type"].as<const char*>() : "";
  if (!strcmp(type, "device")) {
    a.trigType = TRIG_DEVICE;
    jStr(a.trigDevice, sizeof(a.trigDevice), t["device"], "", false);
    const char* st = t["state"].is<const char*>() ? t["state"].as<const char*>() : "";
    if (!actStateFromName(st, &a.trigState) || a.trigState == ST_LOCK) return "trigger_state";
  } else if (!strcmp(type, "temp")) {
    a.trigType = TRIG_TEMP;
    const char* op = t["op"].is<const char*>() ? t["op"].as<const char*>() : "";
    a.tempOp = (op[0] && !op[1]) ? op[0] : 0;
    if (!t["value"].is<float>()) return "trigger_value";
    a.tempValue = t["value"].as<float>();
  } else {
    return "trigger_type";
  }

  if (o["window"].is<JsonObjectConst>()) {
    JsonObjectConst w = o["window"].as<JsonObjectConst>();
    int f = 0, to = 0;
    if (!w["from"].is<const char*>() || !parseHHMM(w["from"].as<const char*>(), &f) ||
        !w["to"].is<const char*>() || !parseHHMM(w["to"].as<const char*>(), &to)) return "automation_window";
    a.hasWindow = true;
    a.winFrom = (uint16_t)f;
    a.winTo = (uint16_t)to;
  }
  if (!o["actions"].is<JsonArrayConst>()) return "automation_actions";
  JsonArrayConst arr = o["actions"].as<JsonArrayConst>();
  if (arr.size() > MAX_AUTO_ACTIONS) return "automation_actions";
  for (JsonVariantConst v : arr) {
    const char* e = parseAction(v, a.actions[a.nActions]);
    if (e) return e;
    a.nActions++;
  }
  return nullptr;
}

// ------------------------------------------------------------------ object -> JSON
void deviceToJson(JsonObject o, const CfgDevice& d) {
  o["id"] = (const char*)d.id;
  o["name"] = (const char*)d.name;
  o["type"] = (const char*)d.type;
  o["group"] = (const char*)d.group;
  o["icon"] = (const char*)d.icon;
  o["room"] = (const char*)d.room;
  o["pin"] = d.pin;
  o["invert"] = d.invert;
  o["enabled"] = d.enabled;
  o["remote"] = d.remote;
  o["watts"] = d.watts;
  o["autoOffMin"] = d.autoOffMin;
  o["bootState"] = bootStateName(d.bootState);
  o["riskyPinOk"] = d.riskyPinOk;
}

void sceneToJson(JsonObject o, const CfgScene& s) {
  o["id"] = (const char*)s.id;
  o["name"] = (const char*)s.name;
  JsonArray arr = o["actions"].to<JsonArray>();
  for (int i = 0; i < s.nActions; i++) actionToJson(arr.add<JsonObject>(), s.actions[i]);
}

void scheduleToJson(JsonObject o, const CfgSchedule& s) {
  o["id"] = (const char*)s.id;
  o["name"] = (const char*)s.name;
  o["enabled"] = s.enabled;
  JsonArray days = o["days"].to<JsonArray>();
  for (int d = 0; d < 7; d++) if ((s.days >> d) & 1) days.add(d);
  char hm[6];
  formatHHMM(s.minute, hm);
  o["time"] = hm;
  actionToJson(o["action"].to<JsonObject>(), s.action);
}

void automationToJson(JsonObject o, const CfgAutomation& a) {
  o["id"] = (const char*)a.id;
  o["name"] = (const char*)a.name;
  o["enabled"] = a.enabled;
  JsonObject t = o["trigger"].to<JsonObject>();
  if (a.trigType == TRIG_DEVICE) {
    t["type"] = "device";
    t["device"] = (const char*)a.trigDevice;
    t["state"] = actStateName(a.trigState);
  } else {
    t["type"] = "temp";
    char op[2] = {a.tempOp, 0};
    t["op"] = op;
    t["value"] = a.tempValue;
  }
  if (a.hasWindow) {
    JsonObject w = o["window"].to<JsonObject>();
    char hm[6];
    formatHHMM(a.winFrom, hm);
    w["from"] = hm;
    formatHHMM(a.winTo, hm);
    w["to"] = hm;
  }
  o["cooldownSec"] = a.cooldownSec;
  JsonArray arr = o["actions"].to<JsonArray>();
  for (int i = 0; i < a.nActions; i++) actionToJson(arr.add<JsonObject>(), a.actions[i]);
}

void configToJson(JsonDocument& doc, bool redact) {
  doc["schemaVersion"] = CONFIG_SCHEMA_VERSION;
  doc["revision"] = cfg.revision;
  doc["nextDeviceId"] = cfg.nextDeviceId;

  JsonObject b = doc["board"].to<JsonObject>();
  b["name"] = (const char*)cfg.board.name;
  b["profile"] = (const char*)cfg.board.profile;
  b["psram"] = cfg.board.psram;
  b["relayActiveLow"] = cfg.board.relayActiveLow;
  b["resetPin"] = cfg.board.resetPin;
  b["tz"] = (const char*)cfg.board.tz;

  JsonObject w = doc["wifi"].to<JsonObject>();
  w["ssid"] = (const char*)cfg.wifi.ssid;
  if (redact) w["hasPassword"] = cfg.wifi.password[0] != '\0';
  else w["password"] = (const char*)cfg.wifi.password;

  JsonObject m = doc["mqtt"].to<JsonObject>();
  m["enabled"] = cfg.mqtt.enabled;
  m["remoteConfig"] = cfg.mqtt.remoteConfig;
  m["provider"] = (const char*)cfg.mqtt.provider;
  m["host"] = (const char*)cfg.mqtt.host;
  m["port"] = cfg.mqtt.port;
  m["username"] = (const char*)cfg.mqtt.username;
  if (redact) m["hasPassword"] = cfg.mqtt.password[0] != '\0';
  else m["password"] = (const char*)cfg.mqtt.password;
  m["homeId"] = (const char*)cfg.mqtt.homeId;

  JsonObject a = doc["auth"].to<JsonObject>();
  a["claimed"] = cfg.auth.claimed;
  if (!redact) {
    a["tokenHash"] = (const char*)cfg.auth.tokenHash;
    a["claimCode"] = (const char*)cfg.auth.claimCode;
  }

  JsonArray rooms = doc["rooms"].to<JsonArray>();
  for (int i = 0; i < cfg.nRooms; i++) {
    JsonObject r = rooms.add<JsonObject>();
    r["id"] = (const char*)cfg.rooms[i].id;
    r["name"] = (const char*)cfg.rooms[i].name;
    r["rev"] = cfg.rooms[i].rev;
    if (cfg.rooms[i].deleted) r["deleted"] = true;
  }
  JsonArray devs = doc["devices"].to<JsonArray>();
  for (int i = 0; i < cfg.nDevices; i++) deviceToJson(devs.add<JsonObject>(), cfg.devices[i]);
  JsonArray scenes = doc["scenes"].to<JsonArray>();
  for (int i = 0; i < cfg.nScenes; i++) sceneToJson(scenes.add<JsonObject>(), cfg.scenes[i]);
  JsonArray sch = doc["schedules"].to<JsonArray>();
  for (int i = 0; i < cfg.nSchedules; i++) scheduleToJson(sch.add<JsonObject>(), cfg.schedules[i]);
  JsonArray aut = doc["automations"].to<JsonArray>();
  for (int i = 0; i < cfg.nAutomations; i++) automationToJson(aut.add<JsonObject>(), cfg.automations[i]);
}

// ------------------------------------------------------------------ JSON -> Config
// strict=false (loading our own file): skip items that do not parse, never fail.
// strict=true  (REST replace):         first problem aborts with an error code, nothing is applied.
// trustAuth    (loading our own file): accept the auth section and the grandfathered-pin flag.
static const char* jsonToConfig(JsonVariantConst root, Config& t, bool strict, bool trustAuth) {
  if (!root.is<JsonObjectConst>()) return "bad_json";
  JsonObjectConst r = root.as<JsonObjectConst>();
  int dropped = 0;
// NOTE: a plain block, not do{}while(0): `continue` must bind to the enclosing for-loop.
#define ITEM_FAIL(code) { if (strict) return (code); dropped++; continue; }

  if (trustAuth) {
    t.revision = (uint32_t)jInt(r["revision"], t.revision);
    long nid = jInt(r["nextDeviceId"], t.nextDeviceId);
    t.nextDeviceId = nid < 1 ? 1 : (uint32_t)nid;
  }

  if (r["board"].is<JsonObjectConst>()) {
    JsonObjectConst b = r["board"].as<JsonObjectConst>();
    jStr(t.board.name, sizeof(t.board.name), b["name"], t.board.name);
    jStr(t.board.profile, sizeof(t.board.profile), b["profile"], t.board.profile, false);
    t.board.psram = jBool(b["psram"], t.board.psram);
    t.board.relayActiveLow = jBool(b["relayActiveLow"], t.board.relayActiveLow);
    t.board.resetPin = (int16_t)jInt(b["resetPin"], t.board.resetPin);
    jStr(t.board.tz, sizeof(t.board.tz), b["tz"], t.board.tz, false);
    if (strict) { const char* e = validateBoard(t.board); if (e) return e; }
  }
  if (r["wifi"].is<JsonObjectConst>()) {
    JsonObjectConst w = r["wifi"].as<JsonObjectConst>();
    if (w["ssid"].is<const char*>()) {
      char pass[64];
      copyStr(pass, sizeof(pass), t.wifi.password);
      if (w["password"].is<const char*>()) jStr(pass, sizeof(pass), w["password"], "", false);
      if (strict) { const char* e = validateWifi(w["ssid"].as<const char*>(), pass); if (e) return e; }
      jStr(t.wifi.ssid, sizeof(t.wifi.ssid), w["ssid"], "", false);
      copyStr(t.wifi.password, sizeof(t.wifi.password), pass);
    }
  }
  if (r["mqtt"].is<JsonObjectConst>()) {
    JsonObjectConst m = r["mqtt"].as<JsonObjectConst>();
    t.mqtt.enabled = jBool(m["enabled"], t.mqtt.enabled);
    t.mqtt.remoteConfig = jBool(m["remoteConfig"], t.mqtt.remoteConfig);
    jStr(t.mqtt.provider, sizeof(t.mqtt.provider), m["provider"], t.mqtt.provider, false);
    jStr(t.mqtt.host, sizeof(t.mqtt.host), m["host"], t.mqtt.host, false);
    long port = jInt(m["port"], t.mqtt.port);
    t.mqtt.port = (port > 0 && port < 65536) ? (uint16_t)port : 0;
    jStr(t.mqtt.username, sizeof(t.mqtt.username), m["username"], t.mqtt.username, false);
    if (m["password"].is<const char*>()) jStr(t.mqtt.password, sizeof(t.mqtt.password), m["password"], "", false);
    jStr(t.mqtt.homeId, sizeof(t.mqtt.homeId), m["homeId"], t.mqtt.homeId, false);
    if (strict) { const char* e = validateMqtt(t.mqtt); if (e) return e; }
  }
  if (trustAuth && r["auth"].is<JsonObjectConst>()) {
    JsonObjectConst a = r["auth"].as<JsonObjectConst>();
    t.auth.claimed = jBool(a["claimed"], false);
    jStr(t.auth.tokenHash, sizeof(t.auth.tokenHash), a["tokenHash"], "", false);
    jStr(t.auth.claimCode, sizeof(t.auth.claimCode), a["claimCode"], "", false);
    uint8_t probe[32];
    if (t.auth.claimed && !fromHex(t.auth.tokenHash, probe, 32)) {   // corrupted hash: require re-claiming
      t.auth.claimed = false;
      t.auth.tokenHash[0] = '\0';
    }
  }

  if (r["rooms"].is<JsonArrayConst>()) {
    t.nRooms = 0;
    for (JsonVariantConst v : r["rooms"].as<JsonArrayConst>()) {
      if (t.nRooms >= MAX_ROOMS) { if (strict) return "too_many_rooms"; break; }
      if (!v.is<JsonObjectConst>()) ITEM_FAIL("room_invalid");
      JsonObjectConst o = v.as<JsonObjectConst>();
      CfgRoom& rm = t.rooms[t.nRooms];
      memset(&rm, 0, sizeof(rm));
      jStr(rm.id, sizeof(rm.id), o["id"], "", false);
      jStr(rm.name, sizeof(rm.name), o["name"], "");
      rm.rev = (uint32_t)jInt(o["rev"], 1);
      rm.deleted = jBool(o["deleted"], false);
      if (!validId(rm.id) || !nameOk(rm.name, sizeof(rm.name)) || rm.rev == 0) ITEM_FAIL("room_invalid");
      if (findRoom(t, rm.id) >= 0) ITEM_FAIL("room_duplicate");
      t.nRooms++;
    }
  }
  if (r["devices"].is<JsonArrayConst>()) {
    t.nDevices = 0;
    for (JsonVariantConst v : r["devices"].as<JsonArrayConst>()) {
      if (t.nDevices >= MAX_DEVICES) { if (strict) return "too_many_devices"; break; }
      if (!v.is<JsonObjectConst>()) ITEM_FAIL("bad_device");
      CfgDevice d;
      const char* e = parseDeviceJson(v.as<JsonObjectConst>(), nullptr, trustAuth, d);
      if (!e && strict) e = validateDeviceStrict(t, d, -1, gTarget, gPsram, dhtReservedPin());
      if (e) ITEM_FAIL(e);
      t.devices[t.nDevices++] = d;
    }
  }
  if (r["scenes"].is<JsonArrayConst>()) {
    t.nScenes = 0;
    for (JsonVariantConst v : r["scenes"].as<JsonArrayConst>()) {
      if (t.nScenes >= MAX_SCENES) { if (strict) return "too_many_scenes"; break; }
      if (!v.is<JsonObjectConst>()) ITEM_FAIL("bad_scene");
      const char* e = parseSceneJson(v.as<JsonObjectConst>(), t.scenes[t.nScenes]);
      if (!e && strict) e = validateScene(t, t.scenes[t.nScenes]);
      if (!e && findSceneCfg(t, t.scenes[t.nScenes].id) >= 0) e = "scene_id_duplicate";
      if (e) ITEM_FAIL(e);
      t.nScenes++;
    }
  }
  if (r["schedules"].is<JsonArrayConst>()) {
    t.nSchedules = 0;
    for (JsonVariantConst v : r["schedules"].as<JsonArrayConst>()) {
      if (t.nSchedules >= MAX_SCHEDULES) { if (strict) return "too_many_schedules"; break; }
      if (!v.is<JsonObjectConst>()) ITEM_FAIL("bad_schedule");
      const char* e = parseScheduleJson(v.as<JsonObjectConst>(), t.schedules[t.nSchedules]);
      if (!e && strict) e = validateSchedule(t, t.schedules[t.nSchedules]);
      if (!e && findScheduleCfg(t, t.schedules[t.nSchedules].id) >= 0) e = "schedule_id_duplicate";
      if (e) ITEM_FAIL(e);
      t.nSchedules++;
    }
  }
  if (r["automations"].is<JsonArrayConst>()) {
    t.nAutomations = 0;
    for (JsonVariantConst v : r["automations"].as<JsonArrayConst>()) {
      if (t.nAutomations >= MAX_AUTOMATIONS) { if (strict) return "too_many_automations"; break; }
      if (!v.is<JsonObjectConst>()) ITEM_FAIL("bad_automation");
      const char* e = parseAutomationJson(v.as<JsonObjectConst>(), t.automations[t.nAutomations]);
      if (!e && strict) e = validateAutomation(t, t.automations[t.nAutomations]);
      if (!e && findAutomationCfg(t, t.automations[t.nAutomations].id) >= 0) e = "automation_id_duplicate";
      if (e) ITEM_FAIL(e);
      t.nAutomations++;
    }
  }
#undef ITEM_FAIL
  if (strict && liveRoomCount(t) == 0) return "no_room";
  if (!strict) gLoadReport.droppedItems += dropped;
  return nullptr;
}

const char* configReplaceFromJson(JsonVariantConst src) {
  if (gReadOnly) return "config_read_only";
  Config& t = gScratch;
  memcpy(&t, &cfg, sizeof(Config));          // start from the live config: secrets and auth survive
  if (src["schemaVersion"].is<int>() && src["schemaVersion"].as<int>() != CONFIG_SCHEMA_VERSION)
    return "schema_version";                 // a future migrator would convert older/newer documents here
  const char* e = jsonToConfig(src, t, true, false);
  if (e) return e;
  ConfigReport rep;
  sanitizeConfig(t, gTarget, gPsram, dhtReservedPin(), &rep);   // disables rules that reference removed devices
  memcpy(&cfg, &t, sizeof(Config));
  return configSave() ? nullptr : "save_failed";
}

// ------------------------------------------------------------------ file I/O
bool configSave() {
  if (gReadOnly) return false;
  cfg.revision++;
  JsonDocument doc;
  configToJson(doc, false);

  File f = LittleFS.open(kTmp, "w");
  if (!f) return false;
  size_t n = serializeJson(doc, f);
  f.flush();
  f.close();
  if (n == 0) { LittleFS.remove(kTmp); return false; }

  // Verify what actually reached flash before we trust it.
  {
    File v = LittleFS.open(kTmp, "r");
    if (!v) return false;
    JsonDocument check;
    DeserializationError err = deserializeJson(check, v);
    v.close();
    if (err || check["schemaVersion"].as<int>() != CONFIG_SCHEMA_VERSION) { LittleFS.remove(kTmp); return false; }
  }
  LittleFS.remove(kBak);
  if (LittleFS.exists(kCfg) && !LittleFS.rename(kCfg, kBak)) { LittleFS.remove(kTmp); return false; }
  if (!LittleFS.rename(kTmp, kCfg)) {
    LittleFS.rename(kBak, kCfg);   // put the old file back
    return false;
  }
  return true;
}

// Reads the v3 NVS data. Returns true if there was anything to migrate.
static bool tryMigrateV3(Config& t) {
  Preferences p;
  if (!p.begin(kLegacyNs, true)) return false;
  String devs = p.getString("devs", "");
  String ssid = p.getString("ssid", "");
  String pass = p.getString("pass", "");
  uint32_t nextId = p.getUInt("nextId", 1);
  p.end();
  bool haveDevs = devs.startsWith("v1\n");
  if (!haveDevs && ssid.length() == 0) return false;

  configDefaults(t);
  if (haveDevs) migrateLegacyDevices(devs.c_str(), nextId, t, gTarget, gPsram, dhtReservedPin(), &gMigrationReport);
  if (ssid.length() > 0 && ssid.length() <= 32) {
    copyStr(t.wifi.ssid, sizeof(t.wifi.ssid), ssid.c_str());
    copyStr(t.wifi.password, sizeof(t.wifi.password), pass.c_str());
  }
  return true;
}

ConfigSource configLoad() {
  gReadOnly = false;
  if (!LittleFS.begin(false)) {
    Serial.println("[cfg] LittleFS mount failed - formatting the partition");
    if (!LittleFS.begin(true)) {
      Serial.println("[cfg] LittleFS unusable - running with defaults, changes will not persist");
      configDefaults(cfg);
      return CFG_FRESH;
    }
  }

  const char* paths[2] = {kCfg, kBak};
  const ConfigSource sources[2] = {CFG_FILE, CFG_BACKUP};
  for (int i = 0; i < 2; i++) {
    if (!LittleFS.exists(paths[i])) continue;
    File f = LittleFS.open(paths[i], "r");
    if (!f) continue;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) { Serial.printf("[cfg] %s unreadable (%s)\n", paths[i], err.c_str()); continue; }

    int version = doc["schemaVersion"] | 0;
    if (version > CONFIG_SCHEMA_VERSION) {
      // Newer firmware wrote this file. Do not guess and do not overwrite it.
      Serial.printf("[cfg] %s has schema %d (this firmware supports %d): read-only safe mode\n", paths[i], version, CONFIG_SCHEMA_VERSION);
      configDefaults(cfg);
      gReadOnly = true;
      return CFG_LOCKED_NEWER;
    }
    if (version < 1) continue;
    // Future migrations from older schemas plug in here: convert `doc` in place, then parse as usual.

    memset(&gLoadReport, 0, sizeof(gLoadReport));
    Config& t = gScratch;
    configDefaults(t);
    jsonToConfig(doc.as<JsonVariantConst>(), t, false, true);
    ConfigReport rep;
    sanitizeConfig(t, gTarget, gPsram, dhtReservedPin(), &rep);
    rep.droppedItems += gLoadReport.droppedItems;
    gLoadReport = rep;
    memcpy(&cfg, &t, sizeof(Config));
    if (sources[i] == CFG_BACKUP) configSave();   // promote the backup so the main file is healthy again
    return sources[i];
  }

  Config& t = gScratch;
  if (tryMigrateV3(t)) {
    ConfigReport rep;
    sanitizeConfig(t, gTarget, gPsram, dhtReservedPin(), &rep);
    gLoadReport = rep;
    memcpy(&cfg, &t, sizeof(Config));
    if (configSave()) {
      // Saved and verified: remove the old plaintext Wi-Fi password and device list from NVS.
      Preferences p;
      if (p.begin(kLegacyNs, false)) { p.clear(); p.end(); }
      return CFG_MIGRATED_V3;
    }
    return CFG_MIGRATED_V3;   // could not save: legacy NVS data stays in place for the next boot
  }

  configDefaults(cfg);
  ConfigReport rep;
  sanitizeConfig(cfg, gTarget, gPsram, dhtReservedPin(), &rep);
  configSave();
  return CFG_FRESH;
}

void configWipeAll() {
  LittleFS.remove(kCfg);
  LittleFS.remove(kBak);
  LittleFS.remove(kTmp);
  LittleFS.remove(kState);
  LittleFS.remove(kCa);
  Preferences p;
  if (p.begin(kLegacyNs, false)) { p.clear(); p.end(); }
}

// ------------------------------------------------------------------ rooms
static int oldestTombstone() {
  int best = -1;
  for (int i = 0; i < cfg.nRooms; i++)
    if (cfg.rooms[i].deleted && (best < 0 || cfg.rooms[i].rev < cfg.rooms[best].rev)) best = i;
  return best;
}

static int roomSlot(const char* id) {
  int i = findRoom(cfg, id);
  if (i >= 0) return i;
  if (cfg.nRooms < MAX_ROOMS) return cfg.nRooms++;
  int t = oldestTombstone();
  return t;   // -1 when full of live rooms
}

const char* configRoomUpsert(const CfgRoom& in, bool* changed) {
  *changed = false;
  if (!validId(in.id) || !nameOk(in.name, sizeof(in.name)) || in.rev == 0) return "room_invalid";
  int i = findRoom(cfg, in.id);
  if (i >= 0) {
    const CfgRoom& cur = cfg.rooms[i];
    // Higher rev wins. Equal rev: a tombstone beats a live room, otherwise the greater name wins,
    // so every board converges on the same answer regardless of delivery order.
    bool take = in.rev > cur.rev || (in.rev == cur.rev && !cur.deleted && strcmp(in.name, cur.name) > 0);
    if (!take) return nullptr;
  }
  int s = roomSlot(in.id);
  if (s < 0) return "too_many_rooms";
  CfgRoom& r = cfg.rooms[s];
  memset(&r, 0, sizeof(r));
  copyStr(r.id, sizeof(r.id), in.id);
  copyStr(r.name, sizeof(r.name), in.name);
  r.rev = in.rev;
  r.deleted = false;
  *changed = true;
  return configSave() ? nullptr : "save_failed";
}

const char* configRoomDelete(const char* id, uint32_t rev, bool* changed) {
  *changed = false;
  if (!validId(id) || rev == 0) return "room_invalid";
  int i = findRoom(cfg, id);
  if (i >= 0) {
    CfgRoom& cur = cfg.rooms[i];
    if (cur.deleted && rev <= cur.rev) return nullptr;      // already gone
    if (!cur.deleted && rev < cur.rev) return nullptr;      // stale delete: a newer edit exists
    if (!cur.deleted && liveRoomCount(cfg) <= 1) return "last_room";
    cur.deleted = true;
    cur.rev = rev;
  } else {
    int s = roomSlot(id);   // remember the tombstone so an old upsert can not resurrect the room
    if (s < 0) return "too_many_rooms";
    CfgRoom& r = cfg.rooms[s];
    memset(&r, 0, sizeof(r));
    copyStr(r.id, sizeof(r.id), id);
    copyStr(r.name, sizeof(r.name), id);
    r.rev = rev;
    r.deleted = true;
  }
  for (int d = 0; d < cfg.nDevices; d++)
    if (!strcmp(cfg.devices[d].room, id)) cfg.devices[d].room[0] = '\0';
  *changed = true;
  return configSave() ? nullptr : "save_failed";
}

// ------------------------------------------------------------------ custom CA
bool customCaLoad(String& pem) {
  if (!LittleFS.exists(kCa)) return false;
  File f = LittleFS.open(kCa, "r");
  if (!f) return false;
  pem = f.readString();
  f.close();
  return pem.indexOf("-----BEGIN CERTIFICATE-----") >= 0;
}

const char* customCaSave(const char* pem) {
  size_t n = pem ? strlen(pem) : 0;
  if (n < 200 || n > 6000) return "ca_size";
  if (!strstr(pem, "-----BEGIN CERTIFICATE-----") || !strstr(pem, "-----END CERTIFICATE-----")) return "ca_format";
  if (strstr(pem, "PRIVATE KEY")) return "ca_contains_private_key";
  File f = LittleFS.open(kCa, "w");
  if (!f) return "save_failed";
  size_t w = f.print(pem);
  f.close();
  return w == n ? nullptr : "save_failed";
}

void customCaRemove() { LittleFS.remove(kCa); }
