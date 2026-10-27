/*
  ESP32 Smart Home controller - firmware 4.0.0 (Phase 2)

  One sketch for ESP32 (DevKit/WROOM/WROVER), ESP32-S3 and ESP32-C3. The chip is detected at compile
  time; whether the module has PSRAM (WROVER, S3 "R" variants) is detected at run time and can be
  forced from the app (config "board.psram").

  Libraries (Arduino Library Manager)            Board package
    ArduinoJson   7.x                              "esp32" by Espressif, 3.x
    PubSubClient  2.8
    DHT sensor library + Adafruit Unified Sensor   (only while HAS_DHT = 1, see runtime.h)
  Select a partition scheme that has a SPIFFS/LittleFS partition and an app partition big enough for
  TLS (e.g. "Minimal SPIFFS (1.9MB APP with OTA)"). "Huge APP" has no file system and will not work.

  Where things are:  docs/ARCHITECTURE.md, docs/PROTOCOL.md, docs/SECURITY.md, docs/HARDWARE.md
  First boot: open the Serial Monitor at 115200 baud, note the CLAIM CODE, then use the app's
  "Add board" screen. Nothing in this firmware has a default password.

  Verification status: the pure logic modules are unit-tested on a PC (tests/firmware). The
  Arduino-dependent code (this file, net, api, cloud, devices, config_store) has NOT been compiled
  or run on hardware by the author of this release - see docs/TEST_REPORT.md.
*/

#include "api.h"
#include "automation.h"
#include "cloud.h"
#include "config_store.h"
#include "devices.h"
#include "net.h"
#include "runtime.h"

BoardTarget gTarget = TARGET_ESP32;
bool gPsram = false;
char gBoardId[24] = "sh-000000";

void setup() {
  Serial.begin(115200);
  delay(200);

  gTarget = compileTarget();
  uint64_t mac = ESP.getEfuseMac();                          // factory-programmed, unique per chip
  snprintf(gBoardId, sizeof(gBoardId), "sh-%06x", (unsigned)((mac >> 24) & 0xFFFFFF));
  gPsram = SH_ASSUME_PSRAM || psramFound();

  Serial.printf("\n\nSmart Home firmware %s | chip %s | board %s\n", FW_VERSION, targetName(gTarget), gBoardId);

  // 1) configuration (also migrates the v3 NVS data on first boot)
  ConfigSource src = configLoad();
  gPsram = SH_ASSUME_PSRAM || cfg.board.psram || psramFound();                   // reserve GPIO16/17 (ESP32) etc. when PSRAM is present
  Serial.printf("[cfg] source: %s | revision %lu | %u room(s), %u device(s), %u schedule(s), %u automation(s)\n",
                configSourceName(src), (unsigned long)cfg.revision, cfg.nRooms, cfg.nDevices, cfg.nSchedules, cfg.nAutomations);
  if (src == CFG_MIGRATED_V3)
    Serial.printf("[cfg] v3 migration: %d device(s) imported, %d skipped, %d on an unusable pin, %d on a risky boot pin (kept)\n",
                  gMigrationReport.imported, gMigrationReport.skipped, gMigrationReport.pinProblems, gMigrationReport.riskyKept);
  if (gLoadReport.droppedItems || gLoadReport.repairedFields || gLoadReport.disabledRules || gLoadReport.pinFaults)
    Serial.printf("[cfg] repaired on load: %d dropped, %d fixed field(s), %d rule(s) disabled, %d device(s) with an unusable pin\n",
                  gLoadReport.droppedItems, gLoadReport.repairedFields, gLoadReport.disabledRules, gLoadReport.pinFaults);
  BoardTarget declared;
  if (strcmp(cfg.board.profile, "auto") != 0 && targetFromName(cfg.board.profile, &declared) && declared != gTarget)
    Serial.printf("[cfg] WARNING: config says profile \"%s\" but this firmware runs on %s - using %s\n",
                  cfg.board.profile, targetName(gTarget), targetName(gTarget));

  // 2) pins to a known-safe state as early as software allows
  pinMode(resetButtonPin(), INPUT_PULLUP);
  devicesBegin();
  automationBegin();

  // 3) network, local API, cloud (all non-blocking for relay control)
  netBegin();
  apiBegin();
  cloudBegin();
  Serial.println("[sys] ready - local control works without Wi-Fi/cloud");
}

void loop() {
  uint32_t now = millis();
  apiTick();               // local REST API
  devTick(now);            // PIR, DHT, estimated energy, safety auto-off
  automationTick(now);     // schedules + automations (local)
  netTick(now);            // Wi-Fi, hotspot, NTP, credential-reset button
  cloudTick(now);          // MQTT (a connection attempt can block for a few seconds; relays keep their state)
  delay(2);
}
