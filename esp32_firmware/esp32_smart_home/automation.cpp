#include "automation.h"

#include "devices.h"

AutomationStats gAutoStats = {0, 0, ""};

static uint32_t schedLastKey[MAX_SCHEDULES];            // epoch-minute a schedule last ran (no double run in one minute)
static bool prevOn[MAX_AUTOMATIONS], prevValid[MAX_AUTOMATIONS], tempArmed[MAX_AUTOMATIONS];
static uint32_t lastFireMs[MAX_AUTOMATIONS];
static bool everFired[MAX_AUTOMATIONS];
static RateWindow globalRate(60000UL, 6);
static uint32_t lastEvalMs = 0;

void automationConfigChanged() {
  memset(prevValid, 0, sizeof(prevValid));
  memset(tempArmed, 0, sizeof(tempArmed));
  memset(everFired, 0, sizeof(everFired));
  memset(schedLastKey, 0, sizeof(schedLastKey));
}

void automationBegin() { automationConfigChanged(); }

void automationTick(uint32_t now) {
  if ((uint32_t)(now - lastEvalMs) < 500UL) return;
  lastEvalMs = now;

  bool haveTime = timeValid();
  int nowMin = 0, wday = 0;
  uint32_t minuteKey = 0;
  if (haveTime) {
    time_t t = time(nullptr);
    struct tm tmv;
    localtime_r(&t, &tmv);
    nowMin = tmv.tm_hour * 60 + tmv.tm_min;
    wday = tmv.tm_wday;
    minuteKey = (uint32_t)(t / 60);
  }

  // ---- schedules
  if (haveTime) {
    for (int i = 0; i < cfg.nSchedules; i++) {
      const CfgSchedule& s = cfg.schedules[i];
      if (!s.enabled || schedLastKey[i] == minuteKey) continue;
      if (!scheduleDue(s.days, s.minute, wday, nowMin)) continue;
      schedLastKey[i] = minuteKey;
      int n = devApplyAction(s.action, false, true);
      gAutoStats.fired++;
      copyStr(gAutoStats.last, sizeof(gAutoStats.last), s.id);
      Serial.printf("[auto] schedule %s ran (%d device(s) changed)\n", s.id, n);
    }
  }

  // ---- automations
  for (int i = 0; i < cfg.nAutomations; i++) {
    const CfgAutomation& a = cfg.automations[i];
    if (!a.enabled) { prevValid[i] = false; continue; }

    bool fire = false;
    if (a.trigType == TRIG_DEVICE) {
      int di = devIndex(a.trigDevice);
      if (di < 0 || !rt[di].driven) { prevValid[i] = false; continue; }
      bool cur = rt[di].on;
      bool want = (a.trigState == ST_ON);
      if (prevValid[i] && cur != prevOn[i] && cur == want) fire = true;   // edge, not level
      prevOn[i] = cur;
      prevValid[i] = true;
    } else if (a.trigType == TRIG_TEMP) {
      if (!isnan(gTemp)) fire = tempCrossed(a.tempOp, a.tempValue, gTemp, tempArmed[i]);
    }
    if (!fire) continue;

    if (a.hasWindow && !(haveTime && inTimeWindow(a.winFrom, a.winTo, nowMin))) { gAutoStats.suppressed++; continue; }
    if (everFired[i] && (uint32_t)(now - lastFireMs[i]) < (uint32_t)a.cooldownSec * 1000UL) { gAutoStats.suppressed++; continue; }
    if (!globalRate.allow(now)) {
      gAutoStats.suppressed++;
      Serial.printf("[auto] %s suppressed: global rate limit (possible loop)\n", a.id);
      continue;
    }
    everFired[i] = true;
    lastFireMs[i] = now;

    bool touched[MAX_DEVICES];
    memset(touched, 0, sizeof(touched));
    int changed = 0;
    for (int k = 0; k < a.nActions; k++) changed += devApplyAction(a.actions[k], false, true, touched);
    gAutoStats.fired++;
    copyStr(gAutoStats.last, sizeof(gAutoStats.last), a.id);
    Serial.printf("[auto] %s fired (%d device(s) changed)\n", a.id, changed);
  }
}
