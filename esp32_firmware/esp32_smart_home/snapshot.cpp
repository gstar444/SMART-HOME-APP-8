#include "snapshot.h"

#include <WiFi.h>

#include "automation.h"
#include "cloud.h"
#include "devices.h"
#include "net.h"

static float round1(float v) { return roundf(v * 10.0f) / 10.0f; }
static float round2(float v) { return roundf(v * 100.0f) / 100.0f; }

void snapshotInventory(JsonObject o, bool detailed) {
  o["v"] = 1;
  o["board"] = (const char*)gBoardId;
  o["name"] = (const char*)cfg.board.name;
  o["fw"] = FW_VERSION;
  o["chip"] = targetName(gTarget);
  o["psram"] = gPsram;
  o["cfgRev"] = cfg.revision;
  o["maxDevices"] = MAX_DEVICES;

  JsonArray rooms = o["rooms"].to<JsonArray>();
  for (int i = 0; i < cfg.nRooms; i++) {
    JsonObject r = rooms.add<JsonObject>();
    r["id"] = (const char*)cfg.rooms[i].id;
    r["name"] = (const char*)cfg.rooms[i].name;
    r["rev"] = cfg.rooms[i].rev;
    if (cfg.rooms[i].deleted) r["deleted"] = true;
  }

  JsonArray devs = o["devices"].to<JsonArray>();
  for (int i = 0; i < cfg.nDevices; i++) {
    const CfgDevice& d = cfg.devices[i];
    JsonObject x = devs.add<JsonObject>();
    x["id"] = (const char*)d.id;
    x["name"] = (const char*)d.name;
    x["type"] = (const char*)d.type;
    x["group"] = (const char*)d.group;
    x["icon"] = (const char*)d.icon;
    x["room"] = (const char*)d.room;
    x["watts"] = d.watts;
    x["enabled"] = d.enabled;
    x["remote"] = d.remote;
    if (rt[i].pinFault) x["fault"] = devPinFaultReason(i);   // e.g. "flash", "psram", "pin_duplicate"
    if (detailed) {
      x["pin"] = d.pin;
      x["invert"] = d.invert;
      x["autoOffMin"] = d.autoOffMin;
      x["bootState"] = bootStateName(d.bootState);
    }
  }

  JsonArray scenes = o["scenes"].to<JsonArray>();
  for (int i = 0; i < cfg.nScenes; i++) {
    JsonObject s = scenes.add<JsonObject>();
    s["id"] = (const char*)cfg.scenes[i].id;
    s["name"] = (const char*)cfg.scenes[i].name;
  }
  JsonObject counts = o["counts"].to<JsonObject>();
  counts["schedules"] = cfg.nSchedules;
  counts["automations"] = cfg.nAutomations;

  if (detailed) {
    uint8_t pins[48];
    JsonObject p = o["pins"].to<JsonObject>();
    JsonArray out = p["out"].to<JsonArray>();
    size_t n = listPins(gTarget, gPsram, true, dhtReservedPin(), pins, sizeof(pins));
    for (size_t i = 0; i < n; i++) out.add(pins[i]);
    JsonArray in = p["in"].to<JsonArray>();               // extra input-only pins (usable for PIR)
    n = listPins(gTarget, gPsram, false, dhtReservedPin(), pins, sizeof(pins));
    for (size_t i = 0; i < n; i++) in.add(pins[i]);
  }
}

void snapshotState(JsonObject o) {
  o["v"] = 1;
  o["seq"] = gStateSeq;
  o["ts"] = timeValid() ? (uint32_t)time(nullptr) : 0;
  o["up"] = (uint32_t)(millis() / 1000UL);
  o["rssi"] = netStaUp() ? WiFi.RSSI() : 0;
  o["ip"] = netIp();
  o["dnd"] = gDnd;
  if (!isnan(gTemp)) o["temp"] = round1(gTemp);
  if (!isnan(gHum)) o["hum"] = round1(gHum);
  o["powerW"] = round1(devPowerW());
  o["energy"] = "estimated";            // rated watts x on-time. NOT measured; there is no current sensor
  o["cloud"] = cloudStatus();
  JsonObject a = o["auto"].to<JsonObject>();
  a["fired"] = gAutoStats.fired;
  a["suppressed"] = gAutoStats.suppressed;
  a["last"] = (const char*)gAutoStats.last;

  JsonObject d = o["d"].to<JsonObject>();
  for (int i = 0; i < cfg.nDevices; i++) {
    JsonObject x = d[(const char*)cfg.devices[i].id].to<JsonObject>();
    x["on"] = rt[i].on;
    if (!strcmp(cfg.devices[i].type, "switch")) x["wh"] = round2(rt[i].wh);
  }
}
