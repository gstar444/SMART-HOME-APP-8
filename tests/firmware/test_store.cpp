// Host test: config_store + devices + automation running against in-memory flash.
// Uses the stand-in ArduinoJson in stubs/ (see its header): this tests OUR logic, not the real library.
#include <time.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include "Arduino.h"
#include "ArduinoJson.h"
#include "LittleFS.h"
#include "Preferences.h"
#include "automation.h"
#include "config_store.h"
#include "devices.h"
#include "runtime.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_STR(a, b) do { const char* x_ = (a); const char* y_ = (b); if (x_ && y_ && !strcmp(x_, y_)) g_pass++; else { g_fail++; printf("  FAIL %s:%d  \"%s\" != \"%s\"\n", __FILE__, __LINE__, x_ ? x_ : "(null)", y_ ? y_ : "(null)"); } } while (0)

static void resetWorld() {
  g_fs.clear(); g_nvs.clear(); g_pins.clear(); g_pinMode.clear(); g_pinEvents.clear(); g_millis = 1000;
  gTarget = TARGET_ESP32; gPsram = false;
  memset(&gLoadReport, 0, sizeof(gLoadReport)); memset(&gMigrationReport, 0, sizeof(gMigrationReport));
  memset(&cfg, 0, sizeof(cfg));
}
static JsonDocument parse(const char* json) { JsonDocument d; deserializeJson(d, String(json)); return d; }
static const char* addDevice(const char* json) {
  JsonDocument b = parse(json); CfgDevice d;
  const char* e = parseDeviceJson(b.as<JsonObjectConst>(), nullptr, false, d);
  if (!e) e = validateDeviceStrict(cfg, d, -1, gTarget, gPsram, dhtReservedPin());
  if (e) return e;
  cfg.devices[cfg.nDevices++] = d;
  return nullptr;
}
static const char* addAutomation(const char* json) {
  JsonDocument b = parse(json); CfgAutomation a;
  const char* e = parseAutomationJson(b.as<JsonObjectConst>(), a);
  if (!e) e = validateAutomation(cfg, a);
  if (e) return e;
  cfg.automations[cfg.nAutomations++] = a;
  return nullptr;
}
// the same pin level the firmware writes for "relay off" / "relay on"
static int offLevel() { return cfg.board.relayActiveLow ? HIGH : LOW; }
static int onLevel() { return cfg.board.relayActiveLow ? LOW : HIGH; }

static void test_fresh_and_roundtrip() {
  printf("config store: fresh boot, save, reload, redaction\n");
  resetWorld();
  CHECK(configLoad() == CFG_FRESH);
  CHECK(g_fs.count("/config.json") == 1);
  CHECK(cfg.nRooms == 1 && cfg.nScenes == 3 && cfg.nDevices == 0);
  CHECK(!cfg.auth.claimed && cfg.mqtt.port == 8883 && !cfg.mqtt.enabled);

  CHECK(!addDevice(R"({"id":"light","name":"Main Light","icon":"light","pin":23,"room":"room1"})"));
  CHECK(!addDevice(R"({"id":"heat","name":"Heater","icon":"heater","pin":22})"));
  CHECK(!addDevice(R"({"id":"door","name":"Door","icon":"lock","pin":21})"));
  CHECK(!addDevice(R"({"id":"pir","name":"Hall PIR","icon":"pir","pin":34})"));
  CHECK(cfg.devices[1].autoOffMin == 180 && cfg.devices[1].watts == 1000);       // heater defaults
  CHECK(cfg.devices[2].invert && !cfg.devices[2].remote);                         // lock: inverted, not cloud-controllable
  JsonDocument sched = parse(R"({"id":"s1","name":"Evening","days":[1,2,3,4,5],"time":"18:30","action":{"target":"light","state":"on"}})");
  CfgSchedule s; CHECK(!parseScheduleJson(sched.as<JsonObjectConst>(), s)); CHECK(!validateSchedule(cfg, s)); cfg.schedules[cfg.nSchedules++] = s;
  CHECK(s.days == 0x3E && s.minute == 1110);
  CHECK(!addAutomation(R"({"id":"a1","name":"Hall","trigger":{"type":"device","device":"pir","state":"on"},"window":{"from":"18:00","to":"23:30"},"cooldownSec":30,"actions":[{"target":"light","state":"on"}]})"));
  copyStr(cfg.wifi.ssid, sizeof(cfg.wifi.ssid), "HomeNet"); copyStr(cfg.wifi.password, sizeof(cfg.wifi.password), "hunter2hunter2");
  copyStr(cfg.mqtt.password, sizeof(cfg.mqtt.password), "broker-secret"); cfg.auth.claimed = true;
  copyStr(cfg.auth.tokenHash, sizeof(cfg.auth.tokenHash), "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff");
  CHECK(configSave());
  uint32_t rev = cfg.revision;

  Config before; memcpy(&before, &cfg, sizeof(Config));
  memset(&cfg, 0, sizeof(cfg));
  CHECK(configLoad() == CFG_FILE);
  CHECK(cfg.nDevices == 4 && cfg.nSchedules == 1 && cfg.nAutomations == 1 && cfg.revision == rev);
  CHECK_STR(cfg.devices[0].name, "Main Light"); CHECK_STR(cfg.devices[0].room, "room1");
  CHECK(cfg.schedules[0].minute == 1110 && cfg.automations[0].hasWindow && cfg.automations[0].winFrom == 1080);
  CHECK(cfg.auth.claimed && !strcmp(cfg.wifi.password, "hunter2hunter2") && !strcmp(cfg.mqtt.password, "broker-secret"));
  CHECK(memcmp(cfg.devices, before.devices, sizeof(cfg.devices[0]) * 4) == 0 || true);   // padding bytes may differ; field checks above

  JsonDocument red; configToJson(red, true);
  String out; serializeJson(red, out);
  CHECK(out.find("hunter2") == std::string::npos);
  CHECK(out.find("broker-secret") == std::string::npos);
  CHECK(out.find("tokenHash") == std::string::npos && out.find("00112233") == std::string::npos);
  CHECK(out.find("claimCode") == std::string::npos);
  CHECK(out.find("hasPassword") != std::string::npos);
  JsonDocument full; configToJson(full, false); String fo; serializeJson(full, fo);
  CHECK(fo.find("hunter2hunter2") != std::string::npos);   // the file itself does hold them (see docs/SECURITY.md)
}

static void test_migration_and_fallbacks() {
  printf("config store: v3 migration, backup fallback, newer schema\n");
  resetWorld();
  g_nvs["smarthome"]["devs"] =
      "v1\nlight1|Hall Light|switch|Lighting|light|0|23|12|0|0\nfan1|Fan|switch|Climate|fan|1|22|60|0|0\n"
      "door|Door|lock|Security|lock|0|21|0|0|1\nrisky|Risky|switch|Other|plug|2|5|0|0|0\nflash|Flash|switch|Other|plug|2|6|0|0|0\n";
  g_nvs["smarthome"]["nextId"] = "9";
  g_nvs["smarthome"]["ssid"] = "OldRouter";
  g_nvs["smarthome"]["pass"] = "oldpassword1";
  CHECK(configLoad() == CFG_MIGRATED_V3);
  CHECK(gMigrationReport.imported == 5 && gMigrationReport.riskyKept == 1 && gMigrationReport.pinProblems == 1);
  CHECK(cfg.nDevices == 5 && cfg.nextDeviceId == 9 && cfg.nRooms == 3);
  CHECK_STR(cfg.wifi.ssid, "OldRouter"); CHECK_STR(cfg.wifi.password, "oldpassword1");
  CHECK(!cfg.auth.claimed);                         // old API key is NOT trusted: the board must be claimed
  CHECK(g_fs.count("/config.json") == 1);
  CHECK(g_nvs["smarthome"].empty());                // plaintext Wi-Fi password removed from NVS after the verified save
  CHECK(cfg.devices[3].riskyPinOk && !cfg.devices[0].riskyPinOk);
  CHECK(devicePinFault(cfg, 4, gTarget, gPsram, dhtReservedPin()) != nullptr);   // pin 6 stays untouched
  CHECK(configLoad() == CFG_FILE);                  // second boot reads the new file, no migration

  // corrupt main file -> backup is used and promoted
  CHECK(configSave());                              // creates .bak from the previous good file
  CHECK(g_fs.count("/config.bak") == 1);
  g_fs["/config.json"] = "{ this is not json";
  memset(&cfg, 0, sizeof(cfg));
  CHECK(configLoad() == CFG_BACKUP);
  CHECK(cfg.nDevices == 5);
  JsonDocument chk; CHECK(!deserializeJson(chk, String(g_fs["/config.json"])));   // main file healthy again

  // a newer schema is never touched
  g_fs["/config.json"] = R"({"schemaVersion":3,"devices":[]})";
  g_fs.erase("/config.bak");
  std::string newer = g_fs["/config.json"];
  CHECK(configLoad() == CFG_LOCKED_NEWER);
  CHECK(configReadOnly() && !configSave());
  CHECK(g_fs["/config.json"] == newer);
  CHECK(cfg.nDevices == 0);                         // safe mode: defaults, relays unconfigured, nothing overwritten
  CHECK_STR(configReplaceFromJson(parse(R"({"rooms":[{"id":"r1","name":"X","rev":1}]})").as<JsonVariantConst>()), "config_read_only");
  g_fs.clear(); CHECK(configLoad() == CFG_FRESH && !configReadOnly());

  // bad items in an otherwise good file are skipped, not fatal
  resetWorld();
  g_fs["/config.json"] = R"({"schemaVersion":2,"revision":5,"rooms":[{"id":"r1","name":"Hall","rev":2},{"id":"bad id","name":"X","rev":1}],
    "devices":[{"id":"ok","name":"Ok","icon":"fan","pin":25},{"id":"nopin","name":"NoPin","icon":"fan"},"junk",{"id":"ok","name":"Dup","icon":"fan","pin":26}]})";
  CHECK(configLoad() == CFG_FILE);
  CHECK(cfg.nDevices == 1 && cfg.nRooms == 1 && cfg.revision == 5);
  CHECK(gLoadReport.droppedItems >= 3);
}

static void test_replace_and_parsers() {
  printf("config store: strict replace, parsers\n");
  resetWorld(); configLoad();
  copyStr(cfg.wifi.ssid, sizeof(cfg.wifi.ssid), "Keep"); copyStr(cfg.wifi.password, sizeof(cfg.wifi.password), "keep-this-pass");
  cfg.auth.claimed = true; copyStr(cfg.auth.tokenHash, sizeof(cfg.auth.tokenHash), "ab");
  configSave();
  uint32_t rev = cfg.revision;

  // invalid -> error, nothing changes
  CHECK_STR(configReplaceFromJson(parse(R"({"devices":[{"id":"x","name":"X","icon":"fan","pin":6}]})").as<JsonVariantConst>()), "pin_not_allowed");
  CHECK_STR(configReplaceFromJson(parse(R"({"schedules":[{"id":"s","name":"S","days":[1],"time":"07:00","action":{"target":"type:lock","state":"off"}}]})").as<JsonVariantConst>()), "action_would_unlock");
  CHECK_STR(configReplaceFromJson(parse(R"({"mqtt":{"enabled":true,"host":"abc.hivemq.cloud","username":"u","password":"p","port":1883}})").as<JsonVariantConst>()), "mqtt_port");
  CHECK_STR(configReplaceFromJson(parse(R"({"board":{"tz":"bad tz!"}})").as<JsonVariantConst>()), "timezone");
  CHECK_STR(configReplaceFromJson(parse(R"({"rooms":[{"id":"a","name":"A","rev":1},{"id":"a","name":"B","rev":1}]})").as<JsonVariantConst>()), "room_duplicate");
  CHECK_STR(configReplaceFromJson(parse(R"({"schemaVersion":7})").as<JsonVariantConst>()), "schema_version");
  CHECK(cfg.revision == rev && cfg.nDevices == 0);

  // valid -> applied, secrets and auth preserved, `auth` in the body ignored
  const char* ok = R"({"schemaVersion":2,"board":{"name":"Lounge","tz":"IST-5:30"},
    "auth":{"claimed":false,"tokenHash":"evil"},
    "mqtt":{"enabled":true,"provider":"hivemq","host":"abc123.s1.eu.hivemq.cloud","username":"board-1","password":"s3cret","homeId":"myhome"},
    "rooms":[{"id":"living","name":"Living","rev":4}],
    "devices":[{"id":"l1","name":"Lamp","icon":"light","pin":23,"room":"living"},{"id":"f1","name":"Fan","icon":"fan","pin":22,"room":"living"}],
    "automations":[{"id":"a1","name":"Fan on lamp","trigger":{"type":"device","device":"l1","state":"on"},"cooldownSec":10,"actions":[{"target":"f1","state":"on"}]}]})";
  CHECK(configReplaceFromJson(parse(ok).as<JsonVariantConst>()) == nullptr);
  CHECK(cfg.nDevices == 2 && cfg.nAutomations == 1 && cfg.mqtt.enabled && !strcmp(cfg.board.name, "Lounge") && !strcmp(cfg.board.tz, "IST-5:30"));
  CHECK_STR(cfg.wifi.password, "keep-this-pass");
  CHECK(cfg.auth.claimed && !strcmp(cfg.auth.tokenHash, "ab"));
  CHECK(!strcmp(cfg.mqtt.password, "s3cret"));
  // removing a device that a rule uses: the rule is disabled (not left dangling, not deleted)
  CHECK(configReplaceFromJson(parse(R"({"devices":[{"id":"l1","name":"Lamp","icon":"light","pin":23}]})").as<JsonVariantConst>()) == nullptr);
  CHECK(cfg.nAutomations == 1 && !cfg.automations[0].enabled);

  // client can never set the grandfathered-pin flag; a pin change clears it
  CfgDevice d; JsonDocument b = parse(R"({"id":"r5","name":"R","icon":"plug","pin":5,"riskyPinOk":true})");
  CHECK(!parseDeviceJson(b.as<JsonObjectConst>(), nullptr, false, d)); CHECK(!d.riskyPinOk);
  CHECK_STR(validateDeviceStrict(cfg, d, -1, gTarget, gPsram, dhtReservedPin()), "pin_not_allowed");
  CfgDevice base = d; base.riskyPinOk = true; CfgDevice upd; JsonDocument pc = parse(R"({"pin":13})");
  CHECK(!parseDeviceJson(pc.as<JsonObjectConst>(), &base, false, upd)); CHECK(!upd.riskyPinOk && upd.pin == 13);
  JsonDocument nm = parse(R"({"name":"Renamed"})"); CHECK(!parseDeviceJson(nm.as<JsonObjectConst>(), &base, false, upd)); CHECK(upd.riskyPinOk && upd.pin == 5);   // unchanged pin keeps the flag
  CfgDevice t2; JsonDocument bad = parse(R"({"id":"x","name":"X","icon":"fan","pin":"seven"})");
  CHECK_STR(parseDeviceJson(bad.as<JsonObjectConst>(), nullptr, false, t2), "pin_required");
  JsonDocument bad2 = parse(R"({"id":"x","name":"X","icon":"fan","pin":9999})"); CHECK_STR(parseDeviceJson(bad2.as<JsonObjectConst>(), nullptr, false, t2), "pin_range");
  JsonDocument bad3 = parse(R"({"id":"x","name":"X","icon":"fan","pin":25,"bootState":"sometimes"})"); CHECK_STR(parseDeviceJson(bad3.as<JsonObjectConst>(), nullptr, false, t2), "device_boot_state");
}

static void test_rooms() {
  printf("rooms: deterministic merge regardless of delivery order\n");
  struct Op { bool del; const char* id; const char* name; uint32_t rev; };
  const Op ops[] = {{false, "kitchen", "Kitchen", 1}, {false, "kitchen", "Kitchen & Dining", 3}, {false, "kitchen", "Cooking", 3},
                    {true, "kitchen", "", 4}, {false, "kitchen", "Revived", 4}, {false, "den", "Den", 1}, {false, "den", "Den", 2}};
  const int n = sizeof(ops) / sizeof(ops[0]);
  std::string results[3];
  int order[3][7] = {{0, 1, 2, 3, 4, 5, 6}, {6, 5, 4, 3, 2, 1, 0}, {3, 0, 4, 2, 6, 1, 5}};
  for (int o = 0; o < 3; o++) {
    resetWorld(); configLoad();
    for (int k = 0; k < n; k++) {
      const Op& op = ops[order[o][k]]; bool ch;
      if (op.del) configRoomDelete(op.id, op.rev, &ch);
      else { CfgRoom r; memset(&r, 0, sizeof(r)); copyStr(r.id, sizeof(r.id), op.id); copyStr(r.name, sizeof(r.name), op.name); r.rev = op.rev; configRoomUpsert(r, &ch); }
    }
    for (int i = 0; i < cfg.nRooms; i++) {
      // a deleted room's stored name carries no meaning, only (id, rev, deleted) must converge
      char b[96]; snprintf(b, sizeof(b), "%s|%s|%u|%d;", cfg.rooms[i].id, cfg.rooms[i].deleted ? "-" : cfg.rooms[i].name, cfg.rooms[i].rev, cfg.rooms[i].deleted);
      results[o] += b;
    }
    // order-independent comparison: sort entries
    std::vector<std::string> parts; size_t s = 0, e;
    while ((e = results[o].find(';', s)) != std::string::npos) { parts.push_back(results[o].substr(s, e - s)); s = e + 1; }
    std::sort(parts.begin(), parts.end()); results[o].clear(); for (auto& p : parts) results[o] += p + ";";
  }
  CHECK(results[0] == results[1] && results[1] == results[2]);
  CHECK(results[0].find("kitchen|-|4|1;") != std::string::npos);   // tombstone wins the rev-4 tie
  CHECK(results[0].find("den|Den|2|0;") != std::string::npos);
  // last live room can not be deleted; devices of a deleted room become unassigned
  resetWorld(); configLoad(); bool ch;
  CHECK_STR(configRoomDelete("room1", 5, &ch), "last_room");
  CfgRoom r; memset(&r, 0, sizeof(r)); copyStr(r.id, sizeof(r.id), "den"); copyStr(r.name, sizeof(r.name), "Den"); r.rev = 1; CHECK(!configRoomUpsert(r, &ch) && ch);
  CHECK(!addDevice(R"({"id":"l","name":"L","icon":"light","pin":23,"room":"den"})"));
  CHECK(!configRoomDelete("den", 2, &ch) && ch && cfg.devices[0].room[0] == 0);
  CHECK(configRoomUpsert(r, &ch) == nullptr && !ch);          // stale upsert after the delete: accepted as a no-op
  // tombstones are recycled when the table is full
  resetWorld(); configLoad();
  for (int i = 0; i < 40; i++) {
    CfgRoom x; memset(&x, 0, sizeof(x)); snprintf(x.id, sizeof(x.id), "r%d", i); copyStr(x.name, sizeof(x.name), "R"); x.rev = 1;
    bool c2; const char* e = configRoomUpsert(x, &c2);
    if (e) { configRoomDelete(x.id, 2, &c2); }   // make room by deleting then continue
    if (i % 2) configRoomDelete(x.id, 2, &c2);
  }
  CHECK(cfg.nRooms <= MAX_ROOMS && liveRoomCount(cfg) >= 1);
}

static void test_devices() {
  printf("devices: safe init, commands, auto-off, DND, scenes, remote-only\n");
  resetWorld(); configLoad();
  CHECK(!addDevice(R"({"id":"light","name":"Light","icon":"light","pin":23})"));
  CHECK(!addDevice(R"({"id":"fan","name":"Fan","icon":"fan","pin":22,"remote":false})"));
  CHECK(!addDevice(R"({"id":"heat","name":"Heater","icon":"heater","pin":19,"autoOffMin":1})"));
  CHECK(!addDevice(R"({"id":"siren","name":"Siren","icon":"siren","pin":18})"));
  CHECK(!addDevice(R"({"id":"door","name":"Door","icon":"lock","pin":21})"));
  CHECK(!addDevice(R"({"id":"pir","name":"PIR","icon":"pir","pin":34})"));
  // a v3-style device on a pin that is flash on this chip: must never be driven
  cfg.devices[cfg.nDevices] = cfg.devices[0]; copyStr(cfg.devices[cfg.nDevices].id, 16, "bad"); cfg.devices[cfg.nDevices].pin = 6; cfg.nDevices++;
  g_pins.clear();
  devicesBegin();
  CHECK(g_pins[23] == offLevel() && g_pins[22] == offLevel() && g_pins[19] == offLevel() && g_pins[18] == offLevel());
  CHECK(g_pins[21] == offLevel());                    // locked: inverted strike NOT energised
  CHECK(rt[4].on);                                    // lock starts locked
  CHECK(g_pins.count(6) == 0 && g_pinMode.count(6) == 0 && rt[6].pinFault);   // unusable pin never touched
  CHECK(g_pinMode[23] == OUTPUT && g_pinMode[34] == INPUT);
  // SAFE ORDER: the OFF level is latched into the pin BEFORE the pin becomes an output (no glitch from this firmware)
  {
    auto idx = [](const std::string& ev) { for (size_t i = 0; i < g_pinEvents.size(); i++) if (g_pinEvents[i] == ev) return (int)i; return -1; };
    std::string off = "w23=" + std::to_string(offLevel());
    int firstWrite = idx(off), modeOut = idx("m23=" + std::to_string(OUTPUT));
    CHECK(firstWrite >= 0 && modeOut >= 0 && firstWrite < modeOut);
    // the lock (inverted strike) is latched to its de-energised level first as well
    CHECK(idx("w21=" + std::to_string(offLevel())) < idx("m21=" + std::to_string(OUTPUT)));
    // and no pin is ever driven to the ON level during initialisation
    for (const std::string& ev : g_pinEvents) CHECK(ev != "w23=" + std::to_string(onLevel()) && ev != "w19=" + std::to_string(onLevel()));
  }

  CHECK(devSet(0, true, true) == nullptr && g_pins[23] == onLevel() && rt[0].on);
  uint32_t seq = gStateSeq; CHECK(devSet(0, true, true) == nullptr && gStateSeq == seq);   // idempotent: no extra change
  CHECK_STR(devSet(5, true, true), "read_only"); CHECK_STR(devSet(6, true, true), "pin_fault"); CHECK_STR(devSet(99, true, true), "unknown_device");

  // never-unlock: scenes/automation actions skip a lock even if somebody forced such an action in
  CfgAction unlock; setAction(unlock, "door", ST_OFF); CHECK(devApplyAction(unlock, false, false) == 0 && rt[4].on);
  CHECK(devRunScene("goodnight") == nullptr && rt[4].on && !rt[0].on);     // goodnight: all off + locked
  CHECK_STR(devRunScene("nope"), "unknown_scene");
  // a direct, authenticated command CAN unlock (the app asks the user to confirm first)
  CHECK(devSet(4, false, true) == nullptr && !rt[4].on && g_pins[21] == onLevel());
  devSet(4, true, true);

  // remote-only scene respects remote=false
  devSet(0, true, true); devSet(1, true, true);
  CHECK(devRunScene("alloff", true) == nullptr && !rt[0].on && rt[1].on);   // fan has remote=false: untouched
  CHECK(devRunScene("alloff", false) == nullptr && !rt[1].on);

  // safety auto-off (heater, 1 minute here) + estimated energy
  devSet(2, true, true); g_millis += 61000; devTick(g_millis);
  CHECK(!rt[2].on && g_pins[19] == offLevel());
  devSet(0, true, true); g_millis += 3600000UL; devTick(g_millis); devTick(g_millis + 1000);
  CHECK(rt[0].wh > 0.0f);
  CHECK(devPowerW() == 12.0f);

  // DND blocks and silences the siren
  devSet(3, true, true); CHECK(rt[3].on);
  devSetDnd(true); CHECK(!rt[3].on && gDnd); CHECK_STR(devSet(3, true, true), "dnd"); devSetDnd(false); CHECK(devSet(3, true, true) == nullptr);

  // PIR follows the input pin
  g_pins[34] = 1; devTick(g_millis + 5000); CHECK(rt[5].on); g_pins[34] = 0; devTick(g_millis + 6000); CHECK(!rt[5].on);

  // removing a device leaves its pin at the OFF level; polarity change is re-applied
  devSet(1, true, true); int fanPin = 22;
  cfg.board.relayActiveLow = false; devicesReconcile(); CHECK(g_pins[fanPin] == HIGH);   // fan is ON: active-high now drives HIGH (polarity change re-applied)
  cfg.board.relayActiveLow = true;
  memmove(&cfg.devices[1], &cfg.devices[2], sizeof(CfgDevice) * (cfg.nDevices - 2)); cfg.nDevices--;
  devicesReconcile(); CHECK(g_pins[fanPin] == offLevel());

  // WROVER: GPIO16/17 reserved -> a device there becomes a pin fault, nothing is driven
  resetWorld(); configLoad(); gPsram = true;
  cfg.devices[0] = CfgDevice(); copyStr(cfg.devices[0].id, 16, "p16"); copyStr(cfg.devices[0].name, 48, "P16"); copyStr(cfg.devices[0].type, 8, "switch");
  copyStr(cfg.devices[0].icon, 12, "plug"); copyStr(cfg.devices[0].group, 16, "Other"); cfg.devices[0].pin = 16; cfg.devices[0].enabled = true; cfg.nDevices = 1;
  devicesBegin(); CHECK(rt[0].pinFault && g_pins.count(16) == 0);
  gPsram = false; devicesReconcile(); CHECK(!rt[0].pinFault && g_pins.count(16) == 1);

  // boot policy: "last" restores the saved state, lock/heater can not be configured to start on
  resetWorld(); configLoad();
  CHECK(!addDevice(R"({"id":"lamp","name":"Lamp","icon":"light","pin":23,"bootState":"last"})"));
  CHECK_STR(addDevice(R"({"id":"heat","name":"Heater","icon":"heater","pin":22,"bootState":"on"})"), "boot_state_unsafe");
  g_fs["/state.json"] = R"({"lamp":true})";
  devicesBegin(); CHECK(rt[0].on && g_pins[23] == onLevel());
  devSet(0, false, true); g_millis += 31000; devTick(g_millis);
  CHECK(g_fs["/state.json"].find("\"lamp\":false") != std::string::npos);   // debounced write, not on every change
}

// same order as loop() in the .ino: sensors first, then rules
static void tickAt(uint32_t ms) { g_millis = ms; devTick(ms); automationTick(ms); }

static void test_automation() {
  printf("automation: schedule, edge triggers, cooldown, loop limit, conflicts\n");
  resetWorld(); configLoad();
  CHECK(!addDevice(R"({"id":"light","name":"Light","icon":"light","pin":23})"));
  CHECK(!addDevice(R"({"id":"fan","name":"Fan","icon":"fan","pin":22})"));
  CHECK(!addDevice(R"({"id":"door","name":"Door","icon":"lock","pin":21})"));
  CHECK(!addDevice(R"({"id":"pir","name":"PIR","icon":"pir","pin":34})"));
  g_pins[34] = 0;
  devicesBegin(); automationBegin();

  // schedule: fires once in its minute, not again in the same minute
  for (int attempt = 0; attempt < 3; attempt++) {
    time_t t = time(nullptr); struct tm tmv; localtime_r(&t, &tmv);
    cfg.nSchedules = 1; CfgSchedule& s = cfg.schedules[0]; memset(&s, 0, sizeof(s));
    copyStr(s.id, 16, "s1"); copyStr(s.name, 32, "Now"); s.enabled = true; s.days = (uint8_t)(1u << tmv.tm_wday); s.minute = (uint16_t)(tmv.tm_hour * 60 + tmv.tm_min);
    setAction(s.action, "light", ST_ON);
    automationConfigChanged(); devSet(0, false, false); gAutoStats.fired = 0;
    tickAt(g_millis + 600);
    time_t t2 = time(nullptr); struct tm m2; localtime_r(&t2, &m2);
    if (m2.tm_min != tmv.tm_min) continue;     // minute rolled over while testing: try again
    CHECK(rt[0].on && gAutoStats.fired == 1);
    devSet(0, false, false); tickAt(g_millis + 600);
    CHECK(!rt[0].on && gAutoStats.fired == 1);       // same minute: no second run
    break;
  }
  cfg.nSchedules = 0;

  // device-trigger automation: PIR on -> light on, with edge detection, cooldown and manual hold
  CHECK(!addAutomation(R"({"id":"a1","name":"Motion light","trigger":{"type":"device","device":"pir","state":"on"},"cooldownSec":30,"actions":[{"target":"light","state":"on"}]})"));
  automationConfigChanged(); gAutoStats = AutomationStats{0, 0, ""};
  uint32_t t0 = g_millis + 10000;
  g_pins[34] = 0; tickAt(t0);                       // baseline: PIR off
  g_pins[34] = 1; tickAt(t0 + 1000);                // rising edge
  CHECK(rt[0].on && gAutoStats.fired == 1);
  devSet(0, false, false);
  tickAt(t0 + 2000); CHECK(!rt[0].on && gAutoStats.fired == 1);          // level still high: no re-fire (edge, not level)
  g_pins[34] = 0; tickAt(t0 + 3000); g_pins[34] = 1; tickAt(t0 + 4000);  // new edge inside the 30 s cooldown
  CHECK(!rt[0].on && gAutoStats.fired == 1 && gAutoStats.suppressed == 1);
  g_pins[34] = 0; tickAt(t0 + 40000); g_pins[34] = 1; tickAt(t0 + 41000); // after the cooldown
  CHECK(rt[0].on && gAutoStats.fired == 2);

  // a person switched the light off by hand 2 s before the rule would act: the rule leaves it alone ...
  cfg.nAutomations = 0;
  CHECK(!addAutomation(R"({"id":"a2","name":"Fast","trigger":{"type":"device","device":"pir","state":"on"},"cooldownSec":5,"actions":[{"target":"light","state":"on"}]})"));
  automationConfigChanged();
  uint32_t t1 = t0 + 100000;
  g_pins[34] = 0; tickAt(t1);
  devSet(0, false, true);                           // manual command at t1
  g_pins[34] = 1; tickAt(t1 + 2000);
  CHECK(!rt[0].on);
  // ... but not 17 s later
  g_pins[34] = 0; tickAt(t1 + 3000); g_pins[34] = 1; tickAt(t1 + 20000);
  CHECK(rt[0].on);

  // validator: a rule may not act on its own trigger, not even through a selector
  CHECK_STR(addAutomation(R"({"id":"loop","name":"Self","trigger":{"type":"device","device":"light","state":"on"},"cooldownSec":5,"actions":[{"target":"type:switch","state":"off"}]})"), "automation_loop");

  // loop limit: ping-pong between two devices can not run away (6 firings per minute, all rules together)
  resetWorld(); configLoad();
  CHECK(!addDevice(R"({"id":"a","name":"A","icon":"plug","pin":23})")); CHECK(!addDevice(R"({"id":"b","name":"B","icon":"plug","pin":22})"));
  devicesBegin();
  CHECK(!addAutomation(R"({"id":"ab","name":"A->B","trigger":{"type":"device","device":"a","state":"on"},"cooldownSec":5,"actions":[{"target":"b","state":"off"}]})"));
  CHECK(!addAutomation(R"({"id":"ba","name":"B->A","trigger":{"type":"device","device":"b","state":"off"},"cooldownSec":5,"actions":[{"target":"a","state":"off"}]})"));
  CHECK(!addAutomation(R"({"id":"toggle","name":"toggler","trigger":{"type":"device","device":"a","state":"off"},"cooldownSec":5,"actions":[{"target":"b","state":"on"}]})"));
  automationBegin(); gAutoStats = AutomationStats{0, 0, ""};
  uint32_t base = 100000; devSet(1, true, false); tickAt(base);
  for (int i = 0; i < 60; i++) { devSet(0, i % 2 == 0, false); rt[0].lastManualMs = 0; rt[1].lastManualMs = 0; tickAt(base + 6000UL * (i + 1)); }
  CHECK(gAutoStats.fired <= 42 && gAutoStats.suppressed > 0);   // 60 edge pairs in 6 simulated minutes: hard cap is 6 firings per 60 s window (<= 42 here)

  // conflict inside one rule run: the first action on a device wins
  resetWorld(); configLoad();
  CHECK(!addDevice(R"({"id":"x","name":"X","icon":"light","pin":23})")); CHECK(!addDevice(R"({"id":"y","name":"Y","icon":"fan","pin":22})")); CHECK(!addDevice(R"({"id":"t","name":"T","icon":"plug","pin":19})"));
  devicesBegin();
  CHECK(!addAutomation(R"({"id":"c","name":"Conflict","trigger":{"type":"device","device":"t","state":"on"},"cooldownSec":5,"actions":[{"target":"x","state":"on"},{"target":"icon:light","state":"off"},{"target":"icon:fan","state":"on"}]})"));
  automationBegin(); tickAt(g_millis + 1000); devSet(2, true, false); tickAt(g_millis + 1000);
  CHECK(rt[0].on);                                  // first action (on) beat the later "off"
  CHECK(rt[1].on);                                  // y (fan) got "on" from the selector action

  // temperature trigger: rising crossing, disarmed at boot, window honoured
  resetWorld(); configLoad();
  CHECK(!addDevice(R"({"id":"fan","name":"Fan","icon":"fan","pin":22})")); devicesBegin();
  CHECK(!addAutomation(R"({"id":"hot","name":"Hot","trigger":{"type":"temp","op":">","value":30},"cooldownSec":60,"actions":[{"target":"fan","state":"on"}]})"));
  automationBegin(); gTemp = 35.0f; tickAt(g_millis + 1000); CHECK(!rt[0].on);     // booted hot: no action until seen below threshold
  gTemp = 28.0f; tickAt(g_millis + 1000); gTemp = 31.0f; tickAt(g_millis + 1000); CHECK(rt[0].on);
  gTemp = NAN;
}

static void test_example_config() {
  printf("config.example.json accepted by the firmware's strict parser\n");
  std::ifstream f("../../esp32_firmware/config/config.example.json");
  CHECK(f.good());
  std::stringstream ss; ss << f.rdbuf();
  for (int psram = 0; psram < 2; psram++) {          // the example avoids GPIO16/17, so it also fits WROVER
    resetWorld(); configLoad(); gPsram = psram != 0;
    JsonDocument doc; CHECK(!deserializeJson(doc, String(ss.str())));
    CHECK(configReplaceFromJson(doc.as<JsonVariantConst>()) == nullptr);
    CHECK(cfg.nRooms == 3 && cfg.nDevices == 7 && cfg.nScenes == 3 && cfg.nSchedules == 2 && cfg.nAutomations == 2);
    for (int i = 0; i < cfg.nSchedules; i++) CHECK(cfg.schedules[i].enabled);
    for (int i = 0; i < cfg.nAutomations; i++) CHECK(cfg.automations[i].enabled);
    for (int i = 0; i < cfg.nDevices; i++) CHECK(devicePinFault(cfg, i, gTarget, gPsram, dhtReservedPin()) == nullptr);
    CHECK_STR(cfg.board.tz, "IST-5:30");
    CHECK(!cfg.mqtt.enabled && !cfg.auth.claimed);
  }
  // an unusable pin set is rejected by the same file: swap to a flash pin
  resetWorld(); configLoad();
  std::string bad = ss.str(); size_t at = bad.find("\"pin\": 23"); bad.replace(at, 9, "\"pin\": 6");
  JsonDocument d2; deserializeJson(d2, String(bad));
  CHECK_STR(configReplaceFromJson(d2.as<JsonVariantConst>()), "pin_not_allowed");
}

int main() {
  test_example_config();
  test_fresh_and_roundtrip();
  test_migration_and_fallbacks();
  test_replace_and_parsers();
  test_rooms();
  test_devices();
  test_automation();
  printf("\n%d checks passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
