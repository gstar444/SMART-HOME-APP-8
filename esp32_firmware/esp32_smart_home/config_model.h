/*
  config_model.h - the persistent configuration (schema v2) and its rules.

  Pure C++11 - no Arduino, no JSON library. config_store.cpp converts this
  struct to/from /config.json (ArduinoJson 7); everything that DECIDES whether a
  configuration is acceptable lives here so it can be unit-tested on a PC.

  Three levels of checking:
    validate*Strict()   used by every write that arrives over REST/MQTT: reject
                        the request with a short error code, change nothing.
    sanitizeConfig()    used when LOADING a file: repair what can be repaired and
                        disable (never delete) rules that no longer make sense, so
                        a bad edit can not erase the owner's data.
    devicePinFault()    evaluated at every boot: a device whose pin is unsafe on
                        THIS chip/module is left untouched (pin never driven).

  Safety rules enforced here (not only in the app):
    * configuration can never UNLOCK a lock: scenes, schedules and automations may
      only lock; unlocking needs a direct, authenticated command;
    * heater/siren/lock can not be configured to switch on at boot;
    * an automation can not act on the device that triggers it (direct loop).
*/
#pragma once

#include <stdlib.h>

#include "board_profile.h"
#include "core_logic.h"

#define CONFIG_SCHEMA_VERSION 2
#define MAX_ROOMS 16            // includes tombstones of deleted rooms
#define MAX_DEVICES 24
#define MAX_SCENES 8
#define MAX_SCENE_ACTIONS 8
#define MAX_SCHEDULES 16
#define MAX_AUTOMATIONS 12
#define MAX_AUTO_ACTIONS 4

enum ActState : uint8_t { ST_OFF = 0, ST_ON = 1, ST_LOCK = 2 };
enum BootState : uint8_t { BOOT_OFF = 0, BOOT_ON = 1, BOOT_LAST = 2 };
enum TrigType : uint8_t { TRIG_DEVICE = 0, TRIG_TEMP = 1 };

// ------------------------------------------------------------------ icons / types
static const char* const kIcons[] = {"light", "fan", "heater", "curtain", "purifier",
                                     "siren", "lock", "pir", "plug"};
static const size_t kIconCount = sizeof(kIcons) / sizeof(kIcons[0]);

inline bool iconKnown(const char* s) {
  for (size_t i = 0; i < kIconCount; i++) if (!strcmp(s, kIcons[i])) return true;
  return false;
}
inline const char* typeForIcon(const char* icon) {
  if (!strcmp(icon, "lock")) return "lock";
  if (!strcmp(icon, "pir")) return "pir";
  return "switch";
}
inline const char* groupForIcon(const char* icon) {
  if (!strcmp(icon, "light")) return "Lighting";
  if (!strcmp(icon, "fan") || !strcmp(icon, "heater") || !strcmp(icon, "curtain") || !strcmp(icon, "purifier")) return "Climate";
  if (!strcmp(icon, "siren") || !strcmp(icon, "lock") || !strcmp(icon, "pir")) return "Security";
  return "Other";
}
inline uint16_t defaultWatts(const char* icon) {
  if (!strcmp(icon, "light")) return 12;
  if (!strcmp(icon, "fan")) return 60;
  if (!strcmp(icon, "heater")) return 1000;
  if (!strcmp(icon, "purifier")) return 30;
  if (!strcmp(icon, "siren")) return 5;
  return 0;
}
inline uint16_t defaultAutoOff(const char* icon) {   // safety timers, minutes
  if (!strcmp(icon, "heater")) return 180;
  if (!strcmp(icon, "siren")) return 5;
  return 0;
}
inline bool isSafetyIcon(const char* icon) {          // may never start ON by itself
  return !strcmp(icon, "heater") || !strcmp(icon, "siren") || !strcmp(icon, "lock");
}

inline const char* actStateName(uint8_t s) { return s == ST_ON ? "on" : (s == ST_LOCK ? "lock" : "off"); }
inline bool actStateFromName(const char* s, uint8_t* out) {
  if (!s) return false;
  if (!strcmp(s, "on")) { *out = ST_ON; return true; }
  if (!strcmp(s, "off")) { *out = ST_OFF; return true; }
  if (!strcmp(s, "lock")) { *out = ST_LOCK; return true; }
  return false;
}
inline const char* bootStateName(uint8_t s) { return s == BOOT_ON ? "on" : (s == BOOT_LAST ? "last" : "off"); }
inline bool bootStateFromName(const char* s, uint8_t* out) {
  if (!s) return false;
  if (!strcmp(s, "off")) { *out = BOOT_OFF; return true; }
  if (!strcmp(s, "on")) { *out = BOOT_ON; return true; }
  if (!strcmp(s, "last")) { *out = BOOT_LAST; return true; }
  return false;
}

// ------------------------------------------------------------------ structs
struct CfgRoom {
  char id[16];
  char name[48];
  uint32_t rev;      // conflict resolution: higher rev wins, equal rev -> greater name wins
  bool deleted;      // tombstone, so a board that missed the delete can not resurrect it
};

struct CfgDevice {
  char id[16];
  char name[48];
  char type[8];      // "switch" | "lock" | "pir"
  char group[16];
  char icon[12];
  char room[16];     // room id, "" = unassigned
  int16_t pin;
  bool invert;       // lock strike: energised = unlocked
  bool enabled;
  bool remote;       // may be switched through the cloud (a lock may only be LOCKED remotely unless true)
  bool riskyPinOk;   // set only by migration for a v3 device already wired to GPIO5/14
  uint16_t watts;    // typical load, used for the ESTIMATED energy figure
  uint16_t autoOffMin;
  uint8_t bootState; // BootState
};

struct CfgAction {
  char target[24];   // device id, "type:<t>" or "icon:<i>"
  uint8_t state;     // ActState
};

struct CfgScene {
  char id[16];
  char name[32];
  uint8_t nActions;
  CfgAction actions[MAX_SCENE_ACTIONS];
};

struct CfgSchedule {
  char id[16];
  char name[32];
  bool enabled;
  uint8_t days;      // bit0 = Sunday ... bit6 = Saturday
  uint16_t minute;   // minutes after local midnight
  CfgAction action;
};

struct CfgAutomation {
  char id[16];
  char name[32];
  bool enabled;
  uint8_t trigType;      // TrigType
  char trigDevice[16];   // TRIG_DEVICE
  uint8_t trigState;     // TRIG_DEVICE: ST_ON / ST_OFF
  char tempOp;           // TRIG_TEMP: '>' or '<'
  float tempValue;
  bool hasWindow;
  uint16_t winFrom, winTo;   // minutes after midnight
  uint16_t cooldownSec;      // >= 5
  uint8_t nActions;
  CfgAction actions[MAX_AUTO_ACTIONS];
};

struct CfgBoard {
  char name[48];
  char profile[16];    // "auto" | "esp32" | "esp32-s3" | "esp32-c3"  (must match the compiled chip)
  bool psram;          // module has PSRAM (WROVER, S3 R-variants): reserves its GPIOs
  bool relayActiveLow;
  int16_t resetPin;    // -1 = chip default BOOT button
  char tz[40];         // POSIX TZ string, e.g. "IST-5:30"
};

struct CfgWifi { char ssid[33]; char password[64]; };

struct CfgMqtt {
  bool enabled;
  bool remoteConfig;   // allow room/device-name changes over MQTT (relays are always allowed)
  char provider[8];    // "hivemq" | "emqx" | "custom"
  char host[64];
  uint16_t port;       // 8883 (TLS) - presets accept nothing else
  char username[48];
  char password[96];
  char homeId[33];
};

struct CfgAuth {
  bool claimed;
  char tokenHash[65];  // hex SHA-256 of the bearer token ("" until claimed)
  char claimCode[12];  // XXXXX-XXXXX, present only while provisioning
};

struct Config {
  uint16_t schemaVersion;
  uint32_t revision;
  uint32_t nextDeviceId;
  CfgBoard board;
  CfgWifi wifi;
  CfgMqtt mqtt;
  CfgAuth auth;
  uint8_t nRooms;
  CfgRoom rooms[MAX_ROOMS];
  uint8_t nDevices;
  CfgDevice devices[MAX_DEVICES];
  uint8_t nScenes;
  CfgScene scenes[MAX_SCENES];
  uint8_t nSchedules;
  CfgSchedule schedules[MAX_SCHEDULES];
  uint8_t nAutomations;
  CfgAutomation automations[MAX_AUTOMATIONS];
};

// ------------------------------------------------------------------ lookups
inline int findRoom(const Config& c, const char* id) {
  for (int i = 0; i < c.nRooms; i++) if (!strcmp(c.rooms[i].id, id)) return i;
  return -1;
}
inline bool roomLive(const Config& c, const char* id) {
  int i = findRoom(c, id);
  return i >= 0 && !c.rooms[i].deleted;
}
inline int findDeviceCfg(const Config& c, const char* id) {
  for (int i = 0; i < c.nDevices; i++) if (!strcmp(c.devices[i].id, id)) return i;
  return -1;
}
inline int findSceneCfg(const Config& c, const char* id) {
  for (int i = 0; i < c.nScenes; i++) if (!strcmp(c.scenes[i].id, id)) return i;
  return -1;
}
inline int findScheduleCfg(const Config& c, const char* id) {
  for (int i = 0; i < c.nSchedules; i++) if (!strcmp(c.schedules[i].id, id)) return i;
  return -1;
}
inline int findAutomationCfg(const Config& c, const char* id) {
  for (int i = 0; i < c.nAutomations; i++) if (!strcmp(c.automations[i].id, id)) return i;
  return -1;
}
inline int liveRoomCount(const Config& c) {
  int n = 0;
  for (int i = 0; i < c.nRooms; i++) if (!c.rooms[i].deleted) n++;
  return n;
}

// ------------------------------------------------------------------ defaults
inline void setAction(CfgAction& a, const char* target, uint8_t state) {
  copyStr(a.target, sizeof(a.target), target);
  a.state = state;
}

inline void configDefaults(Config& c) {
  memset(&c, 0, sizeof(c));
  c.schemaVersion = CONFIG_SCHEMA_VERSION;
  c.revision = 1;
  c.nextDeviceId = 1;

  copyStr(c.board.name, sizeof(c.board.name), "Smart Home");
  copyStr(c.board.profile, sizeof(c.board.profile), "auto");
  c.board.psram = false;
  c.board.relayActiveLow = true;
  c.board.resetPin = -1;
  copyStr(c.board.tz, sizeof(c.board.tz), "UTC0");

  c.mqtt.enabled = false;
  c.mqtt.remoteConfig = true;
  copyStr(c.mqtt.provider, sizeof(c.mqtt.provider), "hivemq");
  c.mqtt.port = 8883;
  copyStr(c.mqtt.homeId, sizeof(c.mqtt.homeId), "home");

  c.nRooms = 1;
  copyStr(c.rooms[0].id, sizeof(c.rooms[0].id), "room1");
  copyStr(c.rooms[0].name, sizeof(c.rooms[0].name), "Room 1");
  c.rooms[0].rev = 1;

  // The same three routines the v3 firmware had built in, now editable data.
  c.nScenes = 3;
  CfgScene& off = c.scenes[0];
  copyStr(off.id, sizeof(off.id), "alloff");
  copyStr(off.name, sizeof(off.name), "All off");
  off.nActions = 1;
  setAction(off.actions[0], "type:switch", ST_OFF);

  CfgScene& night = c.scenes[1];
  copyStr(night.id, sizeof(night.id), "goodnight");
  copyStr(night.name, sizeof(night.name), "Good night");
  night.nActions = 2;
  setAction(night.actions[0], "type:switch", ST_OFF);
  setAction(night.actions[1], "type:lock", ST_LOCK);

  CfgScene& dinner = c.scenes[2];
  copyStr(dinner.id, sizeof(dinner.id), "dinner");
  copyStr(dinner.name, sizeof(dinner.name), "Dinner");
  dinner.nActions = 3;
  setAction(dinner.actions[0], "icon:light", ST_ON);
  setAction(dinner.actions[1], "icon:fan", ST_OFF);
  setAction(dinner.actions[2], "icon:heater", ST_OFF);
}

// ------------------------------------------------------------------ small validators
inline bool nameOk(const char* s, size_t cap) {
  size_t n = strlen(s);
  return n > 0 && n < cap;
}

// POSIX TZ strings are free-form; this only rejects characters that can never occur.
inline bool validTz(const char* s) {
  size_t n = s ? strlen(s) : 0;
  if (n == 0 || n > 39) return false;
  for (size_t i = 0; i < n; i++) {
    char c = s[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '+' || c == '-' || c == ':' || c == ',' || c == '.' || c == '/' || c == '<' || c == '>';
    if (!ok) return false;
  }
  return true;
}

inline const char* validateWifi(const char* ssid, const char* pass) {
  size_t ls = ssid ? strlen(ssid) : 0, lp = pass ? strlen(pass) : 0;
  if (ls == 0 || ls > 32) return "ssid_length";
  if (lp != 0 && (lp < 8 || lp > 63)) return "password_length";
  return nullptr;
}

inline const char* validateBoard(const CfgBoard& b) {
  if (!nameOk(b.name, sizeof(b.name))) return "board_name";
  BoardTarget t;
  if (strcmp(b.profile, "auto") != 0 && !targetFromName(b.profile, &t)) return "board_profile";
  if (b.resetPin < -1 || b.resetPin > 48) return "reset_pin";
  if (!validTz(b.tz)) return "timezone";
  return nullptr;
}

inline const char* validateMqtt(const CfgMqtt& m) {
  if (strcmp(m.provider, "hivemq") && strcmp(m.provider, "emqx") && strcmp(m.provider, "custom")) return "mqtt_provider";
  if (!validHomeId(m.homeId)) return "mqtt_home_id";
  if (m.enabled) {
    if (!validHostName(m.host)) return "mqtt_host";
    if (m.username[0] == '\0' || strlen(m.username) > 47) return "mqtt_username";
    if (m.password[0] == '\0') return "mqtt_password";
    // Presets are TLS-only on 8883; a self-hosted ("custom") broker may use another TLS port.
    if (strcmp(m.provider, "custom") != 0 && m.port != 8883) return "mqtt_port";
    if (m.port == 0) return "mqtt_port";
  }
  return nullptr;
}

// ------------------------------------------------------------------ pins
// Reason a device's pin can not be driven on THIS chip/module, or nullptr if fine.
inline const char* devicePinFault(const Config& c, int idx, BoardTarget t, bool psram, int reservedPin) {
  const CfgDevice& d = c.devices[idx];
  if (d.pin == reservedPin || d.pin == c.board.resetPin) return "pin_reserved";
  bool isInput = !strcmp(d.type, "pir");
  bool ok = isInput ? pinUsableAsInput(t, psram, d.pin, d.riskyPinOk)
                    : pinUsableAsOutput(t, psram, d.pin, d.riskyPinOk);
  if (!ok) return pinClassName(classifyPin(t, psram, d.pin));
  for (int j = 0; j < idx; j++) if (c.devices[j].pin == d.pin) return "pin_duplicate";   // earlier device wins
  return nullptr;
}

// ------------------------------------------------------------------ actions / rules
// Would applying this action UNLOCK a lock? Configuration may never do that.
inline bool actionUnlocks(const Config& c, const CfgAction& a) {
  if (a.state != ST_OFF) return false;
  if (!strcmp(a.target, "type:lock") || !strcmp(a.target, "icon:lock")) return true;
  int i = findDeviceCfg(c, a.target);
  return i >= 0 && !strcmp(c.devices[i].type, "lock");
}

inline const char* validateAction(const Config& c, const CfgAction& a) {
  if (!validSelectorSyntax(a.target)) return "action_target";
  if (a.state > ST_LOCK) return "action_state";
  if (!strncmp(a.target, "type:", 5)) {
    const char* t = a.target + 5;
    if (strcmp(t, "switch") && strcmp(t, "lock")) return "action_target";   // pir is read-only
  } else if (!strncmp(a.target, "icon:", 5)) {
    if (!iconKnown(a.target + 5) || !strcmp(a.target + 5, "pir")) return "action_target";
  } else {
    int i = findDeviceCfg(c, a.target);
    if (i < 0) return "action_unknown_device";
    if (!strcmp(c.devices[i].type, "pir")) return "action_read_only_device";
  }
  if (actionUnlocks(c, a)) return "action_would_unlock";
  return nullptr;
}

inline const char* validateScene(const Config& c, const CfgScene& s) {
  if (!validId(s.id)) return "scene_id";
  if (!nameOk(s.name, sizeof(s.name))) return "scene_name";
  if (s.nActions == 0 || s.nActions > MAX_SCENE_ACTIONS) return "scene_actions";
  for (int i = 0; i < s.nActions; i++) {
    const char* e = validateAction(c, s.actions[i]);
    if (e) return e;
  }
  return nullptr;
}

inline const char* validateSchedule(const Config& c, const CfgSchedule& s) {
  if (!validId(s.id)) return "schedule_id";
  if (!nameOk(s.name, sizeof(s.name))) return "schedule_name";
  if (s.days == 0 || s.days > 0x7F) return "schedule_days";
  if (s.minute > 1439) return "schedule_time";
  return validateAction(c, s.action);
}

inline const char* validateAutomation(const Config& c, const CfgAutomation& a) {
  if (!validId(a.id)) return "automation_id";
  if (!nameOk(a.name, sizeof(a.name))) return "automation_name";
  if (a.nActions == 0 || a.nActions > MAX_AUTO_ACTIONS) return "automation_actions";
  if (a.cooldownSec < 5) return "automation_cooldown";
  if (a.hasWindow && (a.winFrom > 1439 || a.winTo > 1439)) return "automation_window";

  int trig = -1;
  if (a.trigType == TRIG_DEVICE) {
    trig = findDeviceCfg(c, a.trigDevice);
    if (trig < 0) return "trigger_unknown_device";
    if (a.trigState != ST_ON && a.trigState != ST_OFF) return "trigger_state";
  } else if (a.trigType == TRIG_TEMP) {
    if (a.tempOp != '>' && a.tempOp != '<') return "trigger_op";
    if (!(a.tempValue > -40.0f && a.tempValue < 125.0f)) return "trigger_value";
  } else {
    return "trigger_type";
  }

  for (int i = 0; i < a.nActions; i++) {
    const char* e = validateAction(c, a.actions[i]);
    if (e) return e;
    // Direct loop: the rule may not switch the very device that triggers it.
    if (trig >= 0) {
      const CfgDevice& td = c.devices[trig];
      if (selectorMatches(a.actions[i].target, td.id, td.type, td.icon)) return "automation_loop";
    }
  }
  return nullptr;
}

// ------------------------------------------------------------------ devices
// Strict check for add/update. selfIdx = index being updated, -1 when adding.
inline const char* validateDeviceStrict(const Config& c, const CfgDevice& d, int selfIdx,
                                        BoardTarget t, bool psram, int reservedPin) {
  if (!validId(d.id, 15)) return "device_id";
  if (!nameOk(d.name, sizeof(d.name))) return "device_name";
  for (int i = 0; i < c.nDevices; i++) {
    if (i == selfIdx) continue;
    if (!strcmp(c.devices[i].id, d.id)) return "device_id_duplicate";
    if (c.devices[i].pin == d.pin) return "pin_duplicate";
  }
  if (strcmp(d.type, "switch") && strcmp(d.type, "lock") && strcmp(d.type, "pir")) return "device_type";
  if (!iconKnown(d.icon)) return "device_icon";
  if (strcmp(typeForIcon(d.icon), d.type) != 0) return "icon_type_mismatch";
  if (d.group[0] == '\0') return "device_group";
  if (d.room[0] != '\0' && !roomLive(c, d.room)) return "device_room";
  if (d.watts > 5000) return "device_watts";
  if (d.autoOffMin > 1440) return "device_auto_off";
  if (d.bootState > BOOT_LAST) return "device_boot_state";
  if ((isSafetyIcon(d.icon) || !strcmp(d.type, "pir")) && d.bootState != BOOT_OFF) return "boot_state_unsafe";

  bool isInput = !strcmp(d.type, "pir");
  if (d.pin == reservedPin || d.pin == c.board.resetPin) return "pin_reserved";
  bool ok = isInput ? pinUsableAsInput(t, psram, d.pin, d.riskyPinOk)
                    : pinUsableAsOutput(t, psram, d.pin, d.riskyPinOk);
  if (!ok) return "pin_not_allowed";
  return nullptr;
}

// ------------------------------------------------------------------ load-time repair
struct ConfigReport {
  int droppedItems;     // entries that could not be repaired and were removed
  int repairedFields;   // fields reset to a safe value
  int disabledRules;    // schedules/automations disabled because they became invalid
  int pinFaults;        // devices whose pin is unusable on this board (left untouched at runtime)
};

inline void sanitizeConfig(Config& c, BoardTarget t, bool psram, int reservedPin, ConfigReport* rep) {
  ConfigReport r = {0, 0, 0, 0};
  if (c.schemaVersion != CONFIG_SCHEMA_VERSION) { c.schemaVersion = CONFIG_SCHEMA_VERSION; r.repairedFields++; }

  // board
  cleanText(c.board.name);
  if (!nameOk(c.board.name, sizeof(c.board.name))) { copyStr(c.board.name, sizeof(c.board.name), "Smart Home"); r.repairedFields++; }
  BoardTarget tmp;
  if (strcmp(c.board.profile, "auto") != 0 && !targetFromName(c.board.profile, &tmp)) {
    copyStr(c.board.profile, sizeof(c.board.profile), "auto"); r.repairedFields++;
  }
  if (c.board.resetPin < -1 || c.board.resetPin > 48) { c.board.resetPin = -1; r.repairedFields++; }
  if (!validTz(c.board.tz)) { copyStr(c.board.tz, sizeof(c.board.tz), "UTC0"); r.repairedFields++; }

  // rooms: drop invalid/duplicate ids, keep at least one live room
  uint8_t w = 0;
  for (int i = 0; i < c.nRooms; i++) {
    CfgRoom& rm = c.rooms[i];
    cleanText(rm.name);
    bool dup = false;
    for (int j = 0; j < w; j++) if (!strcmp(c.rooms[j].id, rm.id)) dup = true;
    if (!validId(rm.id) || dup || !nameOk(rm.name, sizeof(rm.name))) { r.droppedItems++; continue; }
    if (w != i) c.rooms[w] = rm;
    w++;
  }
  c.nRooms = w;
  if (liveRoomCount(c) == 0) {
    if (c.nRooms >= MAX_ROOMS) c.nRooms = MAX_ROOMS - 1;
    CfgRoom& rm = c.rooms[c.nRooms++];
    memset(&rm, 0, sizeof(rm));
    copyStr(rm.id, sizeof(rm.id), "room1");
    copyStr(rm.name, sizeof(rm.name), "Room 1");
    rm.rev = 1;
    r.repairedFields++;
  }

  // devices
  w = 0;
  for (int i = 0; i < c.nDevices; i++) {
    CfgDevice d = c.devices[i];
    cleanText(d.name);
    bool dup = false;
    for (int j = 0; j < w; j++) if (!strcmp(c.devices[j].id, d.id)) dup = true;
    if (!validId(d.id, 15) || dup) { r.droppedItems++; continue; }
    if (!nameOk(d.name, sizeof(d.name))) { copyStr(d.name, sizeof(d.name), d.id); r.repairedFields++; }
    if (!iconKnown(d.icon)) { copyStr(d.icon, sizeof(d.icon), "plug"); r.repairedFields++; }
    if (strcmp(d.type, typeForIcon(d.icon)) != 0) { copyStr(d.type, sizeof(d.type), typeForIcon(d.icon)); r.repairedFields++; }
    if (d.group[0] == '\0') { copyStr(d.group, sizeof(d.group), groupForIcon(d.icon)); r.repairedFields++; }
    if (d.room[0] != '\0' && !roomLive(c, d.room)) { d.room[0] = '\0'; r.repairedFields++; }
    if (d.watts > 5000) { d.watts = 5000; r.repairedFields++; }
    if (d.autoOffMin > 1440) { d.autoOffMin = 1440; r.repairedFields++; }
    if (d.bootState > BOOT_LAST || ((isSafetyIcon(d.icon) || !strcmp(d.type, "pir")) && d.bootState != BOOT_OFF)) {
      d.bootState = BOOT_OFF; r.repairedFields++;
    }
    c.devices[w++] = d;
  }
  c.nDevices = w;
  for (int i = 0; i < c.nDevices; i++)
    if (devicePinFault(c, i, t, psram, reservedPin)) r.pinFaults++;

  // scenes: unusable ones are dropped (they are rebuilt from defaults by the app/owner)
  w = 0;
  for (int i = 0; i < c.nScenes; i++) {
    cleanText(c.scenes[i].name);
    bool dup = false;
    for (int j = 0; j < w; j++) if (!strcmp(c.scenes[j].id, c.scenes[i].id)) dup = true;
    if (dup || validateScene(c, c.scenes[i])) { r.droppedItems++; continue; }
    if (w != i) c.scenes[w] = c.scenes[i];
    w++;
  }
  c.nScenes = w;

  // schedules / automations: disabled, never deleted
  for (int i = 0; i < c.nSchedules; i++) {
    cleanText(c.schedules[i].name);
    if (c.schedules[i].enabled && validateSchedule(c, c.schedules[i])) { c.schedules[i].enabled = false; r.disabledRules++; }
  }
  for (int i = 0; i < c.nAutomations; i++) {
    cleanText(c.automations[i].name);
    if (c.automations[i].enabled && validateAutomation(c, c.automations[i])) { c.automations[i].enabled = false; r.disabledRules++; }
  }

  // MQTT: an unusable cloud section is switched off, local control is unaffected
  if (!validHomeId(c.mqtt.homeId)) { copyStr(c.mqtt.homeId, sizeof(c.mqtt.homeId), "home"); r.repairedFields++; }
  if (c.mqtt.enabled && validateMqtt(c.mqtt)) { c.mqtt.enabled = false; r.repairedFields++; }

  if (rep) *rep = r;
}

// ------------------------------------------------------------------ v3 -> v2 migration
// v3 stored the devices as text in NVS:  "v1\n" + one line per device:
//   id|name|type|group|icon|room|pin|watts|autoOffMin|invert
// (room was an integer index; room NAMES lived only inside the app).
struct MigrationReport {
  int imported;
  int skipped;
  int pinProblems;    // imported, but the pin is not usable on this board -> pin stays untouched
  int riskyKept;      // imported on GPIO5/14 and grandfathered (relay may click at boot)
};

inline int splitPipe(const char* line, size_t len, char f[10][64]) {
  int n = 0;
  size_t start = 0;
  for (size_t i = 0; i <= len && n < 10; i++) {
    if (i == len || line[i] == '|') {
      copyStr(f[n], 64, "");
      size_t l = i - start;
      if (l > 63) l = 63;
      memcpy(f[n], line + start, l);
      f[n][l] = '\0';
      n++;
      start = i + 1;
    }
  }
  return n;
}

inline bool migrateLegacyDevices(const char* text, uint32_t legacyNextId, Config& c, BoardTarget t,
                                 bool psram, int reservedPin, MigrationReport* rep) {
  MigrationReport r = {0, 0, 0, 0};
  if (!text || strncmp(text, "v1\n", 3) != 0) return false;
  const char* p = text + 3;
  while (*p && c.nDevices < MAX_DEVICES) {
    const char* e = strchr(p, '\n');
    size_t len = e ? (size_t)(e - p) : strlen(p);
    char f[10][64];
    int n = len ? splitPipe(p, len, f) : 0;
    p += len + (e ? 1 : 0);
    if (len == 0) continue;
    if (n < 10 || !validId(f[0], 15) || findDeviceCfg(c, f[0]) >= 0) { r.skipped++; continue; }
    if (strcmp(f[2], "switch") && strcmp(f[2], "lock") && strcmp(f[2], "pir")) { r.skipped++; continue; }

    CfgDevice d;
    memset(&d, 0, sizeof(d));
    copyStr(d.id, sizeof(d.id), f[0]);
    cleanText(f[1]);
    copyStr(d.name, sizeof(d.name), f[1][0] ? f[1] : f[0]);
    copyStr(d.type, sizeof(d.type), f[2]);
    cleanText(f[4]);
    copyStr(d.icon, sizeof(d.icon), iconKnown(f[4]) ? f[4] : "plug");
    if (strcmp(typeForIcon(d.icon), d.type) != 0) copyStr(d.icon, sizeof(d.icon), !strcmp(d.type, "lock") ? "lock" : (!strcmp(d.type, "pir") ? "pir" : "plug"));
    cleanText(f[3]);
    copyStr(d.group, sizeof(d.group), f[3][0] ? f[3] : groupForIcon(d.icon));

    int roomIdx = atoi(f[5]);
    if (roomIdx < 0) roomIdx = 0;
    if (roomIdx > 49) roomIdx = 49;
    char rid[16], rname[48];
    snprintf(rid, sizeof(rid), "room%d", roomIdx + 1);
    snprintf(rname, sizeof(rname), "Room %d", roomIdx + 1);
    if (findRoom(c, rid) < 0 && c.nRooms < MAX_ROOMS) {
      CfgRoom& rm = c.rooms[c.nRooms++];
      memset(&rm, 0, sizeof(rm));
      copyStr(rm.id, sizeof(rm.id), rid);
      copyStr(rm.name, sizeof(rm.name), rname);
      rm.rev = 1;
    }
    copyStr(d.room, sizeof(d.room), findRoom(c, rid) >= 0 ? rid : "");

    int pin = atoi(f[6]);
    d.pin = (int16_t)pin;
    long watts = atol(f[7]), autoOff = atol(f[8]);
    d.watts = (uint16_t)(watts < 0 ? 0 : (watts > 5000 ? 5000 : watts));
    d.autoOffMin = (uint16_t)(autoOff < 0 ? 0 : (autoOff > 1440 ? 1440 : autoOff));
    d.invert = (f[9][0] == '1');
    d.enabled = true;
    d.remote = strcmp(d.type, "lock") != 0;      // doors are not cloud-controllable until the owner opts in
    d.bootState = BOOT_OFF;
    d.riskyPinOk = (classifyPin(t, psram, pin) == PIN_RISKY);
    if (d.riskyPinOk) r.riskyKept++;

    c.devices[c.nDevices++] = d;
    r.imported++;
    if (devicePinFault(c, c.nDevices - 1, t, psram, reservedPin)) r.pinProblems++;
  }
  c.nextDeviceId = legacyNextId > 0 ? legacyNextId : 1;
  if (rep) *rep = r;
  return true;
}
