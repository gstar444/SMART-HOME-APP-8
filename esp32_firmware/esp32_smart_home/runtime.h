/*
  runtime.h - globals and tiny helpers shared by the firmware modules.

  Module map
    board_profile.h   GPIO policy per chip          (pure, unit-tested)
    core_logic.h      ids/topics/limiters/time      (pure, unit-tested)
    sha256.h          token hashing                 (pure, unit-tested)
    config_model.h    config structs + validation   (pure, unit-tested)
    config_store.*    /config.json on LittleFS, v3 migration, JSON mapping
    devices.*         relays, sensors, scenes, estimated energy
    automation.*      schedules + automations (run locally, no cloud needed)
    net.*             Wi-Fi, setup hotspot, mDNS, NTP, credential reset button
    api.*             local REST API v2 (Bearer token, POST-only writes)
    cloud.*           MQTT over TLS (HiveMQ Cloud / EMQX Cloud / custom CA)
*/
#pragma once

#include <Arduino.h>
#include <time.h>

#include "board_profile.h"
#include "config_model.h"
#include "core_logic.h"

#define FW_VERSION "4.0.0"

// Optional DHT22 temperature/humidity sensor (as in firmware v3). Set HAS_DHT to 0 if you
// do not have the "DHT sensor library" installed or no sensor wired.
#ifndef HAS_DHT
#define HAS_DHT 1
#endif
#ifndef DHT_PIN
#define DHT_PIN 4
#endif

// PSRAM safety net. psramFound() is only true when PSRAM support is ENABLED in the Arduino "Tools"
// menu; on a WROVER module with it switched off the chip still can not use GPIO16/17. Set this to 1 for
// WROVER / PSRAM modules to reserve those pins no matter what (the app/config flag board.psram does the
// same at run time, but only after the board has been claimed).
#ifndef SH_ASSUME_PSRAM
#define SH_ASSUME_PSRAM 0
#endif

extern Config cfg;               // the one live configuration (never copy it onto the stack: it is ~12 KB)
extern BoardTarget gTarget;      // chip this firmware runs on
extern bool gPsram;              // module has PSRAM -> its GPIOs are reserved
extern char gBoardId[24];        // "sh-<6 hex>" unique per chip, used for MQTT topics, client id, hostname

inline int dhtReservedPin() { return HAS_DHT ? DHT_PIN : -1; }
inline int resetButtonPin() { return cfg.board.resetPin >= 0 ? cfg.board.resetPin : defaultResetPin(gTarget); }
inline bool timeValid() { return time(nullptr) > 1700000000L; }   // after the first NTP sync
