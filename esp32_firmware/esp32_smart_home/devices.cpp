#include "devices.h"

#include <LittleFS.h>
#include <ArduinoJson.h>

#if HAS_DHT
#include <DHT.h>
#ifndef DHT_TYPE
#define DHT_TYPE DHT22
#endif
static DHT dht(DHT_PIN, DHT_TYPE);
#endif

DevRuntime rt[MAX_DEVICES];
bool gDnd = false;
float gTemp = NAN, gHum = NAN;
uint32_t gStateSeq = 1;

static uint32_t lastTickMs = 0, lastDhtMs = 0;
static bool lastStateDirty = false;
static uint32_t lastStateWriteMs = 0;
static const char* const kStateFile = "/state.json";

// ------------------------------------------------------------------ low-level pin handling
static inline void relayWrite(int pin, bool energised) {
  const bool activeLow = cfg.board.relayActiveLow;
  digitalWrite(pin, (energised != activeLow) ? HIGH : LOW);   // active-low: energised -> LOW
}

static void applyOutput(int idx) {
  const CfgDevice& d = cfg.devices[idx];
  bool energised = d.invert ? !rt[idx].on : rt[idx].on;
  relayWrite(d.pin, energised);
}

// Safe order: latch the OFF level into the output register BEFORE the pin becomes an output, so the
// relay never sees a glitch. (External pull-up/pull-down resistors on the relay inputs are still
// recommended: nothing in software can act before this firmware starts. See docs/HARDWARE.md.)
static bool lastStateFor(const char* id) {
  if (!LittleFS.exists(kStateFile)) return false;
  File f = LittleFS.open(kStateFile, "r");
  if (!f) return false;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;
  return doc[id] | false;
}

static void initPin(int idx) {
  const CfgDevice& d = cfg.devices[idx];
  DevRuntime& r = rt[idx];
  if (!strcmp(d.type, "pir")) {
    pinMode(d.pin, INPUT);
    r.on = digitalRead(d.pin) == HIGH;
    r.driven = true;
    r.isOutput = false;
    return;
  }
  if (!strcmp(d.type, "lock")) r.on = true;                          // a lock always starts LOCKED
  else if (d.bootState == BOOT_ON) r.on = true;
  else if (d.bootState == BOOT_LAST) r.on = lastStateFor(d.id);
  else r.on = false;
  r.onSince = r.on ? millis() : 0;
  applyOutput(idx);       // 1) latch the level
  pinMode(d.pin, OUTPUT); // 2) only now drive the pin
  applyOutput(idx);
  r.driven = true;
  r.isOutput = true;
}

static void releasePin(int pin, bool isOutput) {
  if (isOutput) relayWrite(pin, false);   // leave it OUTPUT at the OFF level, never floating
}

const char* devPinFaultReason(int idx) {
  return devicePinFault(cfg, idx, gTarget, gPsram, dhtReservedPin());
}

// ------------------------------------------------------------------ lifecycle
void devicesBegin() {
#if HAS_DHT
  dht.begin();
#endif
  memset(rt, 0, sizeof(rt));
  devicesReconcile();
}

void devicesReconcile() {
  DevRuntime old[MAX_DEVICES];
  memcpy(old, rt, sizeof(old));
  memset(rt, 0, sizeof(rt));

  for (int i = 0; i < cfg.nDevices; i++) {
    const CfgDevice& d = cfg.devices[i];
    int j = -1;
    for (int k = 0; k < MAX_DEVICES; k++)
      if (old[k].initialised && !strcmp(old[k].id, d.id)) { j = k; break; }
    if (j >= 0) {
      rt[i] = old[j];
      old[j].initialised = false;   // claimed
    }
    DevRuntime& r = rt[i];
    const char* fault = devPinFaultReason(i);
    if (fault && !r.pinFault) Serial.printf("[dev] %s: pin %d not usable on this board (%s) - left untouched\n", d.id, d.pin, fault);
    bool pinMoved = r.initialised && r.pin != d.pin;
    if (pinMoved && r.driven) { releasePin(r.pin, r.isOutput); r.driven = false; }
    copyStr(r.id, sizeof(r.id), d.id);
    r.pin = d.pin;
    r.initialised = true;
    r.pinFault = fault != nullptr;
    bool want = d.enabled && !r.pinFault;
    if (want && !r.driven) initPin(i);
    else if (want && r.driven && r.isOutput) applyOutput(i);   // polarity/invert may have changed
    if (!want && r.driven) {                 // disabled or now invalid: switch off and stop driving
      if (r.isOutput) { r.on = false; relayWrite(r.pin, false); }
      r.driven = false;
    }
  }
  // devices that were removed: leave their pins safe
  for (int k = 0; k < MAX_DEVICES; k++)
    if (old[k].initialised && old[k].driven) releasePin(old[k].pin, old[k].isOutput);
  gStateSeq++;
}

int devIndex(const char* id) { return findDeviceCfg(cfg, id); }

// ------------------------------------------------------------------ commands
const char* devSet(int idx, bool on, bool manual) {
  if (idx < 0 || idx >= cfg.nDevices) return "unknown_device";
  const CfgDevice& d = cfg.devices[idx];
  DevRuntime& r = rt[idx];
  if (!d.enabled) return "disabled";
  if (r.pinFault || !r.driven) return "pin_fault";
  if (!strcmp(d.type, "pir")) return "read_only";
  if (on && gDnd && !strcmp(d.icon, "siren")) return "dnd";
  if (manual) r.lastManualMs = millis() ? millis() : 1;
  if (r.on == on) return nullptr;   // idempotent: repeating a command changes nothing
  r.on = on;
  r.onSince = on ? millis() : 0;
  applyOutput(idx);
  gStateSeq++;
  if (d.bootState == BOOT_LAST) lastStateDirty = true;
  return nullptr;
}

int devApplyAction(const CfgAction& a, bool manual, bool respectManualHold, bool* touched, bool remoteOnly) {
  int changed = 0;
  uint32_t now = millis();
  for (int i = 0; i < cfg.nDevices; i++) {
    const CfgDevice& d = cfg.devices[i];
    if (!selectorMatches(a.target, d.id, d.type, d.icon)) continue;
    if (!strcmp(d.type, "pir")) continue;
    if (remoteOnly && !d.remote && strcmp(d.type, "lock") != 0) continue;   // locking is always allowed remotely
    bool want;
    if (!strcmp(d.type, "lock")) {
      if (a.state == ST_OFF) continue;       // configuration NEVER unlocks (also enforced when saving)
      want = true;
    } else {
      if (a.state == ST_LOCK) continue;
      want = (a.state == ST_ON);
    }
    if (touched) {
      if (touched[i]) continue;      // an earlier action in this run already decided this device
      touched[i] = true;
    }
    if (respectManualHold && rt[i].lastManualMs != 0 && (uint32_t)(now - rt[i].lastManualMs) < 10000UL) continue;
    bool before = rt[i].on;
    if (devSet(i, want, manual) == nullptr && rt[i].on != before) changed++;
  }
  return changed;
}

const char* devRunScene(const char* sceneId, bool remoteOnly) {
  int s = findSceneCfg(cfg, sceneId);
  if (s < 0) return "unknown_scene";
  for (int a = 0; a < cfg.scenes[s].nActions; a++) devApplyAction(cfg.scenes[s].actions[a], true, false, nullptr, remoteOnly);
  return nullptr;
}

void devSetDnd(bool on) {
  if (gDnd == on) return;
  gDnd = on;
  if (on) {   // silence a running siren immediately
    for (int i = 0; i < cfg.nDevices; i++)
      if (!strcmp(cfg.devices[i].icon, "siren") && rt[i].on) devSet(i, false, false);
  }
  gStateSeq++;
}

float devPowerW() {
  float w = 0;
  for (int i = 0; i < cfg.nDevices; i++)
    if (rt[i].driven && rt[i].on && !strcmp(cfg.devices[i].type, "switch")) w += cfg.devices[i].watts;
  return w;
}

// ------------------------------------------------------------------ periodic work
static void saveLastStateIfDue(uint32_t now) {
  if (!lastStateDirty || (uint32_t)(now - lastStateWriteMs) < 30000UL) return;   // at most every 30 s: flash wear
  lastStateDirty = false;
  lastStateWriteMs = now;
  JsonDocument doc;
  for (int i = 0; i < cfg.nDevices; i++)
    if (cfg.devices[i].bootState == BOOT_LAST) doc[(const char*)cfg.devices[i].id] = rt[i].on;
  File f = LittleFS.open(kStateFile, "w");
  if (!f) return;
  serializeJson(doc, f);
  f.close();
}

void devTick(uint32_t now) {
  // PIR inputs
  for (int i = 0; i < cfg.nDevices; i++) {
    if (!rt[i].driven || strcmp(cfg.devices[i].type, "pir") != 0) continue;
    bool v = digitalRead(cfg.devices[i].pin) == HIGH;
    if (v != rt[i].on) { rt[i].on = v; gStateSeq++; }
  }

  // once a second: ESTIMATED energy and safety auto-off
  if ((uint32_t)(now - lastTickMs) >= 1000UL) {
    float dtH = (now - lastTickMs) / 3600000.0f;
    lastTickMs = now;
    for (int i = 0; i < cfg.nDevices; i++) {
      const CfgDevice& d = cfg.devices[i];
      if (!rt[i].driven || !rt[i].on || strcmp(d.type, "switch") != 0) continue;
      rt[i].wh += d.watts * dtH;
      if (d.autoOffMin > 0 && (uint32_t)(now - rt[i].onSince) >= (uint32_t)d.autoOffMin * 60000UL) {
        Serial.printf("[dev] %s: auto-off after %u min\n", d.id, d.autoOffMin);
        devSet(i, false, false);
      }
    }
  }

#if HAS_DHT
  if ((uint32_t)(now - lastDhtMs) >= 3000UL) {   // DHT22 needs >= 2 s between reads
    lastDhtMs = now;
    float t = dht.readTemperature(), h = dht.readHumidity();
  Serial.print("[dht] t="); Serial.print(t); Serial.print(" h="); Serial.println(h);
  if (t < -40 || t > 80) t = NAN;
  if (h < 0 || h > 100) h = NAN;
    bool changed = false;
    if (!isnan(t)) { if (isnan(gTemp) || fabsf(t - gTemp) >= 0.5f) changed = true; gTemp = t; }
    if (!isnan(h)) { if (isnan(gHum) || fabsf(h - gHum) >= 2.0f) changed = true; gHum = h; }
    if (changed) gStateSeq++;
  }
#endif

  saveLastStateIfDue(now);
}
