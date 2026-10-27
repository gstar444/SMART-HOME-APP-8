/*
  devices.h - relays, locks, PIR inputs, DHT sensor, scenes, estimated energy.

  Runtime state lives in rt[] (parallel to cfg.devices[]). Persistent settings live in cfg.
  Anything that changes cfg.devices[] must call devicesReconcile() afterwards so pins and
  runtime state follow the configuration.
*/
#pragma once

#include "runtime.h"

struct DevRuntime {
  char id[16];            // id this slot was initialised for (to match slots across config edits)
  int16_t pin;
  bool initialised;
  bool driven;            // pin is configured and owned by this device
  bool isOutput;          // relay/lock output (false: PIR input)
  bool pinFault;          // pin unusable on this board: NEVER touched
  bool on;                // switch: relay on | lock: locked | pir: motion detected
  float wh;               // ESTIMATED energy (rated watts x on-time), RAM only, reset at reboot
  uint32_t onSince;       // millis() when switched on (auto-off timer)
  uint32_t lastManualMs;  // last direct (human) command, automations back off for 10 s
};

extern DevRuntime rt[MAX_DEVICES];
extern bool gDnd;          // do-not-disturb: the siren will not start
extern float gTemp, gHum;  // NAN until a valid sensor reading exists
extern uint32_t gStateSeq; // incremented on every externally visible change (drives MQTT publishing)

void devicesBegin();                    // safe pin initialisation for every configured device
void devicesReconcile();                // call after cfg.devices[] changed
int devIndex(const char* id);           // -1 if unknown
const char* devSet(int idx, bool on, bool manual);   // nullptr = ok, otherwise a short error code
// `touched` (optional, MAX_DEVICES entries): a device already marked there is skipped, so within one
// rule run the FIRST action that targets a device wins and later conflicting ones are dropped.
// remoteOnly: skip devices whose `remote` flag is off (used for cloud-triggered scenes).
int devApplyAction(const CfgAction& a, bool manual, bool respectManualHold, bool* touched = nullptr, bool remoteOnly = false);   // returns devices changed
const char* devRunScene(const char* sceneId, bool remoteOnly = false);
void devSetDnd(bool on);
void devTick(uint32_t now);
float devPowerW();                      // ESTIMATED: sum of rated watts of devices that are on
const char* devPinFaultReason(int idx); // nullptr when usable
