/*
  config_store.h - persistent configuration on LittleFS (/config.json, ArduinoJson 7).

  Files kept on the LittleFS partition:
    /config.json   the configuration (schema v2, see config.schema.json)
    /config.bak    previous good copy (used automatically if config.json is damaged)
    /state.json    last relay states, only for devices with bootState "last"
    /ca.pem        optional custom broker root CA (provider "custom")

  Writes are atomic: tmp file -> verified by re-parsing -> old file kept as .bak -> rename.
*/
#pragma once

#include <ArduinoJson.h>

#include "runtime.h"

enum ConfigSource : uint8_t { CFG_FRESH, CFG_FILE, CFG_BACKUP, CFG_MIGRATED_V3, CFG_LOCKED_NEWER };

extern ConfigReport gLoadReport;
extern MigrationReport gMigrationReport;

ConfigSource configLoad();                 // fills `cfg`; always leaves it valid
const char* configSourceName(ConfigSource s);
bool configSave();                         // atomic; bumps cfg.revision
bool configReadOnly();                     // true when the file on flash has a NEWER schema (never overwritten)
void configWipeAll();                      // delete every file/NVS key this firmware keeps

void configToJson(JsonDocument& doc, bool redact);       // redact=true removes every secret
const char* configReplaceFromJson(JsonVariantConst src); // strict; applies and saves, nullptr or error code

// Per-object parsing used by the REST API. They only check JSON shape/types; the semantic checks are
// the validate*Strict functions in config_model.h.
const char* parseDeviceJson(JsonObjectConst o, const CfgDevice* base, bool trustRiskyFlag, CfgDevice& out);
const char* parseSceneJson(JsonObjectConst o, CfgScene& out);
const char* parseScheduleJson(JsonObjectConst o, CfgSchedule& out);
const char* parseAutomationJson(JsonObjectConst o, CfgAutomation& out);

void deviceToJson(JsonObject o, const CfgDevice& d);
void sceneToJson(JsonObject o, const CfgScene& s);
void scheduleToJson(JsonObject o, const CfgSchedule& s);
void automationToJson(JsonObject o, const CfgAutomation& a);

// Room metadata is replicated on every board; rev decides conflicts (see CfgRoom).
const char* configRoomUpsert(const CfgRoom& in, bool* changed);
const char* configRoomDelete(const char* id, uint32_t rev, bool* changed);

// Optional custom CA (provider "custom")
bool customCaLoad(String& pem);
const char* customCaSave(const char* pem);
void customCaRemove();
