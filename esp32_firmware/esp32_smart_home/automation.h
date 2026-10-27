/*
  automation.h - schedules and automations, evaluated locally on the ESP32.

  They keep working with no internet and no MQTT. Schedules need a valid clock, which comes from
  NTP: after a reboot WITHOUT internet the clock is unset and schedules/time windows stay idle
  until time is available (relay control and device-trigger automations are unaffected).

  Safety constraints (see config_model.h for the ones enforced when saving):
    * a rule can never unlock a lock;
    * a rule can not act on the device that triggers it (direct loop);
    * each automation has a cooldown (>= 5 s), and ALL automations together may fire at most
      6 times per minute, which stops indirect loops (A->B->A) from running away;
    * inside one rule run the first action on a device wins, later conflicting ones are dropped;
    * a device a person switched by hand in the last 10 s is left alone by rules.
*/
#pragma once

#include "runtime.h"

struct AutomationStats {
  uint32_t fired;
  uint32_t suppressed;     // blocked by cooldown, rate limit or time window
  char last[16];           // id of the last rule that fired
};
extern AutomationStats gAutoStats;

void automationBegin();
void automationConfigChanged();      // call after schedules/automations were edited
void automationTick(uint32_t now);
