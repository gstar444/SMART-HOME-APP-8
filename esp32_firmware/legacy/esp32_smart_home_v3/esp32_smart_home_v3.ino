/*
  ESP32 Smart Home - REST API firmware  v3.0.0
  ------------------------------------------------------------------
  Board    : "ESP32 Dev Module"  (Arduino IDE -> esp32 by Espressif, core 2.x or 3.x)
  Library  : "DHT sensor library" by Adafruit (+ "Adafruit Unified Sensor")
             -> only needed while HAS_DHT = 1
  Talks to : the Flutter app in this project (same Wi-Fi network)

  NEW in v3.0.0
    1) Wi-Fi from the app: scan networks, enter password, see status / IP, reset.
       The Wi-Fi is saved in flash. If the ESP32 cannot join any network it opens its own
       hotspot  "SmartHome-Setup"  (password = AP_PASS, address http://192.168.4.1/)
       so you can always set it up again from the app.
    2) Devices are no longer a fixed table. Add / edit / delete bulbs, fans and other
       devices from the app (name, room, icon, GPIO pin). The list is SAVED in flash and
       survives restarts. DEFAULT_DEVICES[] below is only used the very first time.

  API (all answers are JSON):
    GET  /api/state                          everything: wifi, sensors, devices, free pins
    POST /api/set?id=light&on=1              relay / lock on (1) or off (0)
    POST /api/dnd?on=1                       Do-Not-Disturb (blocks the siren)
    POST /api/scene?id=alloff|goodnight|dinner

    POST /api/device/add?name=..&icon=light&pin=23&room=0[&watts=12]
    POST /api/device/update?id=..[&name=..][&icon=..][&pin=..][&room=..][&watts=..]
    POST /api/device/delete?id=..

    GET  /api/wifi/status                    state of the Wi-Fi connection / setup hotspot
    GET  /api/wifi/scan[?rescan=1]           list of nearby networks (scan runs in background)
    POST /api/wifi/connect?ssid=..&pass=..   join a new network (old one is restored if it fails)
    POST /api/wifi/reset                     forget saved Wi-Fi, restart in setup (hotspot) mode

    GET  /                                   tiny test page (works in any browser)
  If API_KEY is not empty, every /api request needs the header  X-API-Key: <key>
  (or ?key=<key> in the URL).

  !! MAINS SAFETY: 220 V loads (heater, fan, lights) must go through properly rated
  relays / SSRs in an insulated box with a fuse. Never power a heater from a hobby
  relay board. If unsure, ask an electrician.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>

#define FW_VERSION "3.0.0"

#ifndef WIFI_SCAN_RUNNING
#define WIFI_SCAN_RUNNING (-1)
#endif
#ifndef WIFI_SCAN_FAILED
#define WIFI_SCAN_FAILED  (-2)
#endif

// ====================== 1) YOUR SETTINGS ======================
const char* DEVICE_NAME = "Smart Home";

// OPTIONAL first-time Wi-Fi. Once you set a Wi-Fi from the app, the saved one is used
// instead. Leave the placeholder text if you want to set everything from the app.
const char* WIFI_SSID   = "YOUR_WIFI_NAME";
const char* WIFI_PASS   = "YOUR_WIFI_PASSWORD";

// Fixed IP - used ONLY for the first-time Wi-Fi above (same IP as in the app Settings).
// A Wi-Fi chosen from the app always uses DHCP (a new router has another IP range).
#define USE_STATIC_IP 1
IPAddress STATIC_IP(192, 168, 1, 37);
IPAddress GATEWAY(192, 168, 1, 1);
IPAddress SUBNET(255, 255, 255, 0);
IPAddress DNS_SERVER(8, 8, 8, 8);

// Setup hotspot (opens when no Wi-Fi works). Password must be 8+ characters.
const char* AP_SSID = "SmartHome-Setup";
const char* AP_PASS = "12345678";

// Optional password for the API. Leave "" to disable. Use the same text in the app.
const char* API_KEY = "";

// ESP32-CAM stream (optional), e.g. "http://192.168.1.40:81/stream". "" = no camera.
const char* CAMERA_URL = "";

// Most 1/2/4/8-channel relay boards switch ON when the pin is LOW.
#define RELAY_ACTIVE_LOW 1

// Temperature / humidity sensor (DHT22 on GPIO 4). Set 0 if you have none.
// While HAS_DHT = 1 this GPIO is not offered to devices.
#define HAS_DHT  1
#define DHT_PIN  4
#define DHT_TYPE DHT22

// ====================== 2) DEVICES ======================
// type : "switch" relay | "lock" (on = locked) | "pir" motion sensor (read-only)
// group: "Lighting", "Climate", "Security", "Other" ...
// icon : light fan heater curtain purifier siren lock pir plug
// room : 0 = Room 1, 1 = Room 2 ... (the app can also move tiles between rooms)
// These are only the STARTING devices (first boot). After that the list lives in flash
// and is edited from the app.
struct DefaultDevice {
  const char* id;
  const char* name;
  const char* type;
  const char* group;
  const char* icon;
  int         room;
  int         pin;
  uint16_t    watts;
  uint16_t    autoOffMin;
  bool        invert;
};

const DefaultDevice DEFAULT_DEVICES[] = {
  // id         name              type      group      icon       room pin watts auto invert
  {"light",    "Main Light",     "switch", "Lighting", "light",    0,  23,   12,   0, false},
  {"fan",      "Ceiling Fan",    "switch", "Lighting", "fan",      0,  22,   60,   0, false},
  {"curtain",  "Smart Curtains", "switch", "Climate",  "curtain",  0,  19,    0,   0, false},
  {"heater",   "Heater",         "switch", "Climate",  "heater",   0,  21, 1000, 180, false},
  {"purifier", "Air Purifier",   "switch", "Climate",  "purifier", 0,  18,   30,   0, false},
  {"siren",    "Alarm Siren",    "switch", "Security", "siren",    0,  17,    5,   5, false},
  {"lock",     "Smart Lock",     "lock",   "Security", "lock",     0,  16,    0,   0, true},
  {"pir",      "PIR Sensor",     "pir",    "Security", "pir",      0,  27,    0,   0, false},
};
const size_t N_DEFAULT_DEVICES = sizeof(DEFAULT_DEVICES) / sizeof(DEFAULT_DEVICES[0]);

#define MAX_DEVICES 24

struct Device {
  char     id[12];
  char     name[48];
  char     type[8];
  char     group[16];
  char     icon[12];
  int      room;
  int      pin;
  uint16_t watts;
  uint16_t autoOffMin;
  bool     invert;
  // runtime values (not saved)
  bool     on;
  float    wh;
  uint32_t onSince;
};

enum ConnState { CS_IDLE, CS_PENDING, CS_TRYING };

// GPIOs that are safe for relays on a normal ESP32 Dev board. Not offered on purpose:
// 0 2 12 15 (boot pins), 1 3 (USB serial), 6-11 (flash).
const uint8_t OUT_PINS[]     = {4, 5, 13, 14, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33};
const uint8_t IN_ONLY_PINS[] = {34, 35, 36, 39};   // input only: fine for a PIR sensor
const size_t  N_OUT_PINS     = sizeof(OUT_PINS) / sizeof(OUT_PINS[0]);
const size_t  N_IN_PINS      = sizeof(IN_ONLY_PINS) / sizeof(IN_ONLY_PINS[0]);

const char* ICONS[] = {"light", "fan", "heater", "curtain", "purifier", "siren", "lock", "pir", "plug"};
const size_t N_ICONS = sizeof(ICONS) / sizeof(ICONS[0]);

// ====================== 3) CODE (no need to edit) ======================
#if HAS_DHT
#include <DHT.h>
DHT dht(DHT_PIN, DHT_TYPE);
#endif

#define PREF_NS "smarthome"

WebServer   server(80);
Preferences prefs;

Device   devices[MAX_DEVICES];
size_t   nDevices = 0;
uint32_t nextId   = 1;                       // for generated ids d1, d2, ...

bool     dnd      = false;
float    tempC    = NAN;
float    humidity = NAN;
uint32_t lastTick = 0, lastDht = 0, lastWifiTry = 0;

// ---- Wi-Fi bookkeeping
String    savedSsid, savedPass;              // Wi-Fi stored in flash
bool      setupFlag   = false;               // true after "reset Wi-Fi": go straight to setup mode
String    curSsid, curPass;                  // Wi-Fi we are using right now (to go back to)
bool      curStatic   = false;               // curSsid is the built-in Wi-Fi with a fixed IP
bool      apActive    = false;               // setup hotspot is on
ConnState connState   = CS_IDLE;
String    pendSsid, pendPass;                // new Wi-Fi requested by the app
uint32_t  pendAt = 0, tryStart = 0;
String    lastWifiError;                     // "" = none
bool      lastUp = false;
uint32_t  lastChange = 0;
bool      restartPending = false;
uint32_t  restartAt = 0;

// ---------------------------------------------------------------- small helpers
// Copy text into a fixed buffer, never cutting a UTF-8 (e.g. Hindi) letter in half.
void copyStr(char* dst, size_t cap, const char* src) {
  size_t n = strlen(src);
  if (n >= cap) {
    n = cap - 1;
    while (n > 0 && (((uint8_t)src[n]) & 0xC0) == 0x80) n--;
  }
  memcpy(dst, src, n);
  dst[n] = '\0';
}

// Names are saved as "a|b|c" lines, so these characters must not appear in them.
String cleanName(String s) {
  String o;
  o.reserve(s.length());
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '|' || c == '"' || c == '\\' || (uint8_t)c < 0x20) continue;
    o += c;
  }
  o.trim();
  return o;
}

bool validIcon(const String& s) {
  for (size_t i = 0; i < N_ICONS; i++) if (s == ICONS[i]) return true;
  return false;
}

const char* typeForIcon(const char* icon) {
  if (!strcmp(icon, "lock")) return "lock";
  if (!strcmp(icon, "pir"))  return "pir";
  return "switch";
}

const char* groupForIcon(const char* icon) {
  if (!strcmp(icon, "light")) return "Lighting";
  if (!strcmp(icon, "fan") || !strcmp(icon, "heater") || !strcmp(icon, "curtain") ||
      !strcmp(icon, "purifier")) return "Climate";
  if (!strcmp(icon, "siren") || !strcmp(icon, "lock") || !strcmp(icon, "pir")) return "Security";
  return "Other";
}

uint16_t defaultWatts(const char* icon) {
  if (!strcmp(icon, "light"))    return 12;
  if (!strcmp(icon, "fan"))      return 60;
  if (!strcmp(icon, "heater"))   return 1000;
  if (!strcmp(icon, "purifier")) return 30;
  if (!strcmp(icon, "siren"))    return 5;
  return 0;
}

uint16_t defaultAutoOff(const char* icon) {          // safety timers, minutes
  if (!strcmp(icon, "heater")) return 180;
  if (!strcmp(icon, "siren"))  return 5;
  return 0;
}

bool inList(const uint8_t* list, size_t n, int pin) {
  for (size_t i = 0; i < n; i++) if (list[i] == pin) return true;
  return false;
}

bool pinReserved(int pin) {
#if HAS_DHT
  if (pin == DHT_PIN) return true;
#endif
  return false;
}

// Output devices need an output-capable pin; a PIR may also use the input-only pins.
bool pinAllowed(const char* type, int pin) {
  if (pinReserved(pin)) return false;
  if (inList(OUT_PINS, N_OUT_PINS, pin)) return true;
  if (!strcmp(type, "pir") && inList(IN_ONLY_PINS, N_IN_PINS, pin)) return true;
  return false;
}

bool pinUsed(int pin, const Device* except) {
  for (size_t i = 0; i < nDevices; i++)
    if (&devices[i] != except && devices[i].pin == pin) return true;
  return false;
}

// ---------------------------------------------------------------- relays
inline void relayWrite(int pin, bool energised) {
#if RELAY_ACTIVE_LOW
  digitalWrite(pin, energised ? LOW : HIGH);
#else
  digitalWrite(pin, energised ? HIGH : LOW);
#endif
}

void applyDevice(Device& d) {
  if (!strcmp(d.type, "pir")) return;
  bool energised = d.invert ? !d.on : d.on;
  relayWrite(d.pin, energised);
}

void setDevice(Device& d, bool on) {
  if (d.on == on) return;
  d.on = on;
  if (on) d.onSince = millis();
  applyDevice(d);
}

void initPin(Device& d) {
  if (!strcmp(d.type, "pir")) { pinMode(d.pin, INPUT); return; }
  applyDevice(d);                 // write the safe level first ...
  pinMode(d.pin, OUTPUT);         // ... then drive the pin
  applyDevice(d);
}

// De-energise the relay before a pin is given up (device deleted / moved to another pin).
void releasePin(Device& d) {
  if (!strcmp(d.type, "pir")) return;
  relayWrite(d.pin, false);
}

int indexOfDevice(const char* id) {
  for (size_t i = 0; i < nDevices; i++)
    if (!strcmp(id, devices[i].id)) return (int)i;
  return -1;
}

Device* findDevice(const char* id) {
  int i = indexOfDevice(id);
  return i < 0 ? nullptr : &devices[i];
}

// ---------------------------------------------------------------- saved device list
String devicesToText() {
  String t = "v1\n";
  for (size_t i = 0; i < nDevices; i++) {
    const Device& d = devices[i];
    t += d.id;      t += '|';
    t += d.name;    t += '|';
    t += d.type;    t += '|';
    t += d.group;   t += '|';
    t += d.icon;    t += '|';
    t += String(d.room);       t += '|';
    t += String(d.pin);        t += '|';
    t += String((unsigned int)d.watts);      t += '|';
    t += String((unsigned int)d.autoOffMin); t += '|';
    t += d.invert ? '1' : '0';
    t += '\n';
  }
  return t;
}

void saveDevices() {
  prefs.begin(PREF_NS, false);
  size_t n = prefs.putString("devs", devicesToText());
  prefs.putUInt("nextId", nextId);
  prefs.end();
  if (n == 0) Serial.println("WARNING: could not save the device list");
}

bool validId(const String& s) {
  if (s.length() == 0 || s.length() > 11) return false;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    if (!ok) return false;
  }
  return true;
}

// Returns true when a saved list exists (even an empty one - the user may have deleted all).
bool loadDevices() {
  prefs.begin(PREF_NS, false);
  String t = prefs.getString("devs", "");
  nextId = prefs.getUInt("nextId", 1);
  prefs.end();
  if (!t.startsWith("v1\n")) return false;

  nDevices = 0;
  int pos = 3;
  while (pos < (int)t.length() && nDevices < MAX_DEVICES) {
    int e = t.indexOf('\n', pos);
    if (e < 0) e = t.length();
    String line = t.substring(pos, e);
    pos = e + 1;
    if (line.length() == 0) continue;

    String f[10];
    int n = 0, s = 0;
    while (n < 10) {
      int p = line.indexOf('|', s);
      if (p < 0) { f[n++] = line.substring(s); break; }
      f[n++] = line.substring(s, p);
      s = p + 1;
    }
    if (n < 10 || !validId(f[0])) continue;
    if (f[2] != "switch" && f[2] != "lock" && f[2] != "pir") continue;
    int pin = f[6].toInt();
    if (!pinAllowed(f[2].c_str(), pin) || pinUsed(pin, nullptr) || indexOfDevice(f[0].c_str()) >= 0) {
      Serial.print("Skipping saved device with a bad / duplicate pin: ");
      Serial.println(f[0]);
      continue;
    }

    Device& d = devices[nDevices++];
    memset(&d, 0, sizeof(d));
    copyStr(d.id,    sizeof(d.id),    f[0].c_str());
    copyStr(d.name,  sizeof(d.name),  f[1].length() ? f[1].c_str() : f[0].c_str());
    copyStr(d.type,  sizeof(d.type),  f[2].c_str());
    copyStr(d.group, sizeof(d.group), f[3].length() ? f[3].c_str() : "Other");
    copyStr(d.icon,  sizeof(d.icon),  validIcon(f[4]) ? f[4].c_str() : "plug");
    d.room       = constrain(f[5].toInt(), 0, 49);
    d.pin        = pin;
    d.watts      = (uint16_t)constrain(f[7].toInt(), 0, 5000);
    d.autoOffMin = (uint16_t)constrain(f[8].toInt(), 0, 1440);
    d.invert     = f[9] == "1";
    d.on         = !strcmp(d.type, "lock");      // a lock starts locked
  }
  return true;
}

void seedDevices() {
  nDevices = 0;
  for (size_t i = 0; i < N_DEFAULT_DEVICES && nDevices < MAX_DEVICES; i++) {
    const DefaultDevice& s = DEFAULT_DEVICES[i];
    Device& d = devices[nDevices++];
    memset(&d, 0, sizeof(d));
    copyStr(d.id,    sizeof(d.id),    s.id);
    copyStr(d.name,  sizeof(d.name),  s.name);
    copyStr(d.type,  sizeof(d.type),  s.type);
    copyStr(d.group, sizeof(d.group), s.group);
    copyStr(d.icon,  sizeof(d.icon),  s.icon);
    d.room       = s.room;
    d.pin        = s.pin;
    d.watts      = s.watts;
    d.autoOffMin = s.autoOffMin;
    d.invert     = s.invert;
    d.on         = !strcmp(s.type, "lock");
  }
  saveDevices();
}

void makeId(char* out, size_t cap) {
  for (int guard = 0; guard < 1000; guard++) {
    snprintf(out, cap, "d%lu", (unsigned long)nextId++);
    if (!findDevice(out)) return;
  }
}

// ---------------------------------------------------------------- JSON
String esc(const String& s) {
  String o;
  o.reserve(s.length() + 4);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) { /* drop control characters */ }
    else o += c;               // UTF-8 (e.g. Hindi Wi-Fi names) passes through
  }
  return o;
}

String numOrNull(float v, int decimals) {
  return isnan(v) ? String("null") : String(v, decimals);
}

const char* jsonBool(bool b) { return b ? "true" : "false"; }

bool staUp() { return WiFi.status() == WL_CONNECTED; }

String ipString() {
  if (staUp()) return WiFi.localIP().toString();
  if (apActive) return WiFi.softAPIP().toString();
  return String("0.0.0.0");
}

const char* wifiMode() {
  bool up = staUp();
  if (up && apActive) return "ap+sta";
  if (up)             return "sta";
  if (apActive)       return "ap";
  return "offline";
}

String stateJson() {
  float totalW = 0;
  for (size_t i = 0; i < nDevices; i++)
    if (devices[i].on && !strcmp(devices[i].type, "switch")) totalW += devices[i].watts;

  String j;
  j.reserve(1000 + nDevices * 210);
  j += "{\"name\":\"";   j += esc(DEVICE_NAME);
  j += "\",\"fw\":\"";   j += FW_VERSION;
  j += "\",\"ssid\":\""; j += esc(staUp() ? WiFi.SSID() : String(""));
  j += "\",\"ip\":\"";   j += ipString();
  j += "\",\"rssi\":";   j += String(staUp() ? WiFi.RSSI() : 0);
  j += ",\"mode\":\"";   j += wifiMode();
  j += "\",\"apSsid\":\""; j += apActive ? esc(AP_SSID) : String("");
  j += "\",\"uptime\":"; j += String((unsigned long)(millis() / 1000UL));
  j += ",\"temp\":";     j += numOrNull(tempC, 1);
  j += ",\"hum\":";      j += numOrNull(humidity, 0);
  j += ",\"dnd\":";      j += jsonBool(dnd);
  j += ",\"camera\":\""; j += esc(CAMERA_URL);
  j += "\",\"powerW\":"; j += String(totalW, 1);
  j += ",\"maxDevices\":"; j += String((unsigned int)MAX_DEVICES);

  j += ",\"pinsOut\":[";
  bool first = true;
  for (size_t i = 0; i < N_OUT_PINS; i++) {
    if (pinReserved(OUT_PINS[i])) continue;
    if (!first) j += ',';
    first = false;
    j += String((unsigned int)OUT_PINS[i]);
  }
  j += "],\"pinsIn\":[";
  for (size_t i = 0; i < N_IN_PINS; i++) {
    if (i) j += ',';
    j += String((unsigned int)IN_ONLY_PINS[i]);
  }

  j += "],\"devices\":[";
  for (size_t i = 0; i < nDevices; i++) {
    const Device& d = devices[i];
    if (i) j += ',';
    j += "{\"id\":\"";     j += d.id;
    j += "\",\"name\":\""; j += esc(d.name);
    j += "\",\"type\":\""; j += d.type;
    j += "\",\"group\":\""; j += esc(d.group);
    j += "\",\"icon\":\""; j += d.icon;
    j += "\",\"room\":";   j += String(d.room);
    j += ",\"pin\":";      j += String(d.pin);
    j += ",\"on\":";       j += jsonBool(d.on);
    j += ",\"watts\":";    j += String((unsigned int)d.watts);
    j += ",\"wh\":";       j += String(d.wh, 2);
    j += "}";
  }
  j += "]}";
  return j;
}

void sendJson(int code, const String& body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json; charset=utf-8", body);
}

void sendError(int code, const char* msg) {
  sendJson(code, String("{\"ok\":false,\"error\":\"") + msg + "\"}");
}

bool authorized() {
  if (API_KEY[0] == '\0') return true;
  String k = server.header("X-API-Key");
  if (k.length() == 0) k = server.arg("key");
  return k == API_KEY;
}

#define REQUIRE_AUTH() if (!authorized()) { sendError(401, "unauthorized"); return; }

bool wantOn() {
  String v = server.arg("on");
  return v == "1" || v == "true" || v == "on";
}

// ---------------------------------------------------------------- device handlers
void handleState() {
  REQUIRE_AUTH();
  sendJson(200, stateJson());
}

void handleSet() {
  REQUIRE_AUTH();
  if (!server.hasArg("id") || !server.hasArg("on")) { sendError(400, "need id and on=0|1"); return; }
  String id = server.arg("id");
  Device* d = findDevice(id.c_str());
  if (!d || !strcmp(d->type, "pir")) { sendError(404, "unknown device"); return; }
  bool on = wantOn();
  if (dnd && on && !strcmp(d->icon, "siren")) { sendError(409, "DND mode is on"); return; }
  setDevice(*d, on);
  sendJson(200, "{\"ok\":true}");
}

void handleDnd() {
  REQUIRE_AUTH();
  if (!server.hasArg("on")) { sendError(400, "need on=0|1"); return; }
  dnd = wantOn();
  if (dnd) {
    for (size_t i = 0; i < nDevices; i++)
      if (!strcmp(devices[i].icon, "siren")) setDevice(devices[i], false);
  }
  sendJson(200, "{\"ok\":true}");
}

void allSwitchesOff() {
  for (size_t i = 0; i < nDevices; i++)
    if (!strcmp(devices[i].type, "switch")) setDevice(devices[i], false);
}

void handleScene() {
  REQUIRE_AUTH();
  String id = server.arg("id");
  if (id == "alloff") {
    allSwitchesOff();
  } else if (id == "goodnight") {
    allSwitchesOff();
    for (size_t i = 0; i < nDevices; i++)
      if (!strcmp(devices[i].type, "lock")) setDevice(devices[i], true);   // locked
  } else if (id == "dinner") {
    for (size_t i = 0; i < nDevices; i++) {
      Device& d = devices[i];
      if (!strcmp(d.icon, "light")) setDevice(d, true);
      else if (!strcmp(d.icon, "fan") || !strcmp(d.icon, "heater")) setDevice(d, false);
    }
  } else {
    sendError(404, "unknown scene");
    return;
  }
  sendJson(200, "{\"ok\":true}");
}

void handleDeviceAdd() {
  REQUIRE_AUTH();
  if (nDevices >= MAX_DEVICES) { sendError(409, "device limit reached"); return; }

  String name = cleanName(server.arg("name"));
  if (name.length() == 0) { sendError(400, "name required"); return; }
  String icon = server.hasArg("icon") ? server.arg("icon") : String("plug");
  if (!validIcon(icon)) { sendError(400, "unknown icon"); return; }
  if (!server.hasArg("pin")) { sendError(400, "pin required"); return; }

  int pin = server.arg("pin").toInt();
  const char* type = typeForIcon(icon.c_str());
  if (!pinAllowed(type, pin)) { sendError(400, "this GPIO pin cannot be used"); return; }
  if (pinUsed(pin, nullptr))  { sendError(409, "this GPIO pin is already used"); return; }

  int room = constrain(server.arg("room").toInt(), 0, 49);
  int watts = server.hasArg("watts") ? constrain(server.arg("watts").toInt(), 0, 5000)
                                     : (int)defaultWatts(icon.c_str());

  Device& d = devices[nDevices];
  memset(&d, 0, sizeof(d));
  makeId(d.id, sizeof(d.id));
  copyStr(d.name,  sizeof(d.name),  name.c_str());
  copyStr(d.type,  sizeof(d.type),  type);
  copyStr(d.group, sizeof(d.group), groupForIcon(icon.c_str()));
  copyStr(d.icon,  sizeof(d.icon),  icon.c_str());
  d.room       = room;
  d.pin        = pin;
  d.watts      = (uint16_t)watts;
  d.autoOffMin = defaultAutoOff(icon.c_str());
  d.invert     = !strcmp(type, "lock");     // door strike: energised = unlocked
  d.on         = !strcmp(type, "lock");     // a lock starts locked
  nDevices++;

  initPin(d);
  saveDevices();
  sendJson(200, String("{\"ok\":true,\"id\":\"") + d.id + "\"}");
}

void handleDeviceUpdate() {
  REQUIRE_AUTH();
  String id = server.arg("id");
  Device* d = findDevice(id.c_str());
  if (!d) { sendError(404, "unknown device"); return; }

  // Check everything first, change things afterwards - a bad request changes nothing.
  String name = d->name;
  if (server.hasArg("name")) {
    name = cleanName(server.arg("name"));
    if (name.length() == 0) { sendError(400, "name required"); return; }
  }
  String icon = d->icon;
  if (server.hasArg("icon")) {
    icon = server.arg("icon");
    if (!validIcon(icon)) { sendError(400, "unknown icon"); return; }
    if (strcmp(typeForIcon(icon.c_str()), d->type) != 0) { sendError(400, "this icon does not fit the device type"); return; }
  }
  int pin = d->pin;
  if (server.hasArg("pin")) {
    pin = server.arg("pin").toInt();
    if (!pinAllowed(d->type, pin)) { sendError(400, "this GPIO pin cannot be used"); return; }
    if (pinUsed(pin, d))           { sendError(409, "this GPIO pin is already used"); return; }
  }
  int room  = server.hasArg("room")  ? constrain(server.arg("room").toInt(), 0, 49)   : d->room;
  int watts = server.hasArg("watts") ? constrain(server.arg("watts").toInt(), 0, 5000) : (int)d->watts;

  copyStr(d->name, sizeof(d->name), name.c_str());
  if (icon != d->icon) {
    copyStr(d->icon,  sizeof(d->icon),  icon.c_str());
    copyStr(d->group, sizeof(d->group), groupForIcon(icon.c_str()));
    d->autoOffMin = defaultAutoOff(icon.c_str());
  }
  d->room  = room;
  d->watts = (uint16_t)watts;
  if (pin != d->pin) {
    releasePin(*d);
    d->pin = pin;
    initPin(*d);
  }
  saveDevices();
  sendJson(200, "{\"ok\":true}");
}

void handleDeviceDelete() {
  REQUIRE_AUTH();
  String id = server.arg("id");
  int idx = indexOfDevice(id.c_str());
  if (idx < 0) { sendError(404, "unknown device"); return; }

  releasePin(devices[idx]);
  for (size_t i = (size_t)idx; i + 1 < nDevices; i++) devices[i] = devices[i + 1];
  nDevices--;
  memset(&devices[nDevices], 0, sizeof(Device));
  saveDevices();
  sendJson(200, "{\"ok\":true}");
}

// ---------------------------------------------------------------- wifi: saved settings
void loadWifiPrefs() {
  prefs.begin(PREF_NS, false);
  savedSsid = prefs.getString("ssid", "");
  savedPass = prefs.getString("pass", "");
  setupFlag = prefs.getBool("setup", false);
  prefs.end();
}

void saveWifi(const String& ssid, const String& pass) {
  prefs.begin(PREF_NS, false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.remove("setup");
  prefs.end();
  savedSsid = ssid;
  savedPass = pass;
  setupFlag = false;
}

void forgetWifi() {
  prefs.begin(PREF_NS, false);
  prefs.remove("ssid");
  prefs.remove("pass");
  prefs.putBool("setup", true);
  prefs.end();
  savedSsid = "";
  savedPass = "";
  setupFlag = true;
}

bool hasBuiltInWifi() {
  return WIFI_SSID[0] != '\0' && strcmp(WIFI_SSID, "YOUR_WIFI_NAME") != 0;
}

// ---------------------------------------------------------------- wifi: connect / hotspot
void beginSta(const String& ssid, const String& pass, bool useStatic) {
  WiFi.mode(apActive ? WIFI_AP_STA : WIFI_STA);
  WiFi.setAutoReconnect(true);
  if (useStatic) {
    WiFi.config(STATIC_IP, GATEWAY, SUBNET, DNS_SERVER);
  } else {
    IPAddress zero(0, 0, 0, 0);
    WiFi.config(zero, zero, zero);             // back to DHCP
  }
  WiFi.begin(ssid.c_str(), pass.c_str());
}

void startAp() {
  if (apActive) return;
  WiFi.mode(WIFI_AP_STA);                      // STA stays on so networks can be scanned
  apActive = WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("Setup hotspot \"");
  Serial.print(AP_SSID);
  Serial.print("\" ");
  Serial.println(apActive ? "is ON  ->  http://192.168.4.1/" : "FAILED to start");
}

void stopAp() {
  if (!apActive) return;
  WiFi.softAPdisconnect(true);
  apActive = false;
  Serial.println("Setup hotspot off");
}

void startMdns() {
  MDNS.end();
  if (MDNS.begin("esp32-home")) MDNS.addService("http", "tcp", 80);   // http://esp32-home.local
}

void bootWifi() {
  if (savedSsid.length()) {
    curSsid = savedSsid; curPass = savedPass; curStatic = false;
  } else if (!setupFlag && hasBuiltInWifi()) {
    curSsid = WIFI_SSID; curPass = WIFI_PASS; curStatic = USE_STATIC_IP;
  } else {
    curSsid = ""; curPass = ""; curStatic = false;
  }

  if (curSsid.length() == 0) {                 // nothing to join -> straight to setup mode
    startAp();
    return;
  }

  Serial.print("Connecting to Wi-Fi \"");
  Serial.print(curSsid);
  Serial.print("\"");
  beginSta(curSsid, curPass, curStatic);
  uint32_t t0 = millis();
  while (!staUp() && millis() - t0 < 20000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (staUp()) {
    Serial.print("Connected, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Wi-Fi not connected - opening the setup hotspot.");
    startAp();
  }
}

String wifiJson() {
  const char* state = "idle";
  if (connState != CS_IDLE) state = "trying";
  else if (lastWifiError.length()) state = "failed";

  String j;
  j.reserve(400);
  j += "{\"ok\":true,\"state\":\""; j += state;
  j += "\",\"connected\":";         j += jsonBool(staUp());
  j += ",\"ssid\":\"";              j += esc(staUp() ? WiFi.SSID() : String(""));
  j += "\",\"ip\":\"";              j += ipString();
  j += "\",\"rssi\":";              j += String(staUp() ? WiFi.RSSI() : 0);
  j += ",\"mode\":\"";              j += wifiMode();
  j += "\",\"apSsid\":\"";          j += apActive ? esc(AP_SSID) : String("");
  j += "\",\"target\":\"";          j += connState != CS_IDLE ? esc(pendSsid) : String("");
  j += "\",\"saved\":";             j += jsonBool(savedSsid.length() > 0);
  j += ",\"error\":\"";             j += esc(lastWifiError);
  j += "\"}";
  return j;
}

void handleWifiStatus() {
  REQUIRE_AUTH();
  sendJson(200, wifiJson());
}

void sendScanList(bool scanning, const String& list) {
  sendJson(200, String("{\"ok\":true,\"scanning\":") + jsonBool(scanning) + ",\"networks\":[" + list + "]}");
}

void handleWifiScan() {
  REQUIRE_AUTH();
  if (server.hasArg("rescan")) {               // start a fresh scan in the background
    WiFi.scanDelete();
    WiFi.scanNetworks(true);
    sendScanList(true, "");
    return;
  }
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) { sendScanList(true, ""); return; }
  if (n < 0) {                                 // no scan yet -> start one
    WiFi.scanNetworks(true);
    sendScanList(true, "");
    return;
  }

  // Results are ready: strongest first, one line per network name, hidden ones skipped.
  int order[40];
  int count = 0;
  for (int i = 0; i < n && count < 40; i++) {
    if (WiFi.SSID(i).length() == 0) continue;
    int at = count++;
    while (at > 0 && WiFi.RSSI(order[at - 1]) < WiFi.RSSI(i)) {
      order[at] = order[at - 1];
      at--;
    }
    order[at] = i;
  }

  String list;
  list.reserve(900);
  int shown = 0;
  for (int k = 0; k < count && shown < 20; k++) {
    int i = order[k];
    String name = WiFi.SSID(i);
    bool dup = false;
    for (int m = 0; m < k; m++) if (WiFi.SSID(order[m]) == name) { dup = true; break; }
    if (dup) continue;
    if (shown) list += ',';
    list += "{\"ssid\":\""; list += esc(name);
    list += "\",\"rssi\":"; list += String(WiFi.RSSI(i));
    list += ",\"secure\":"; list += jsonBool(WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    list += "}";
    shown++;
  }
  WiFi.scanDelete();
  sendScanList(false, list);
}

void handleWifiConnect() {
  REQUIRE_AUTH();
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  if (ssid.length() == 0 || ssid.length() > 32) { sendError(400, "Wi-Fi name must be 1 to 32 bytes"); return; }
  if (pass.length() != 0 && (pass.length() < 8 || pass.length() > 63)) {
    sendError(400, "password must be 8 to 63 characters (empty = open network)");
    return;
  }
  if (connState != CS_IDLE) { sendError(409, "already connecting"); return; }

  pendSsid = ssid;
  pendPass = pass;
  pendAt = millis() + 1000;                    // let this answer leave before the radio switches
  connState = CS_PENDING;
  lastWifiError = "";
  sendJson(200, "{\"ok\":true,\"pending\":true}");
}

void handleWifiReset() {
  REQUIRE_AUTH();
  forgetWifi();
  restartPending = true;
  restartAt = millis() + 1200;
  sendJson(200, "{\"ok\":true,\"restart\":true}");
}

// ---------------------------------------------------------------- web page + routes
const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>ESP32 Smart Home</title>
<style>body{font-family:sans-serif;background:#0b1220;color:#e5e9f0;margin:0;padding:16px}
h2{margin:0 0 4px}small{color:#8b95a7}.g{display:grid;grid-template-columns:repeat(auto-fill,minmax(130px,1fr));gap:10px;margin-top:14px}
button{padding:16px 8px;border-radius:14px;border:1px solid #2a3550;background:#141d30;color:#e5e9f0;font-size:15px}
button.on{background:#2f7bff;border-color:#2f7bff}</style></head><body>
<h2 id="t">ESP32 Smart Home</h2><small id="s">connecting...</small><div class="g" id="g"></div>
<script>
const key=new URLSearchParams(location.search).get('key')||'';
const H=key?{'X-API-Key':key}:{};
async function load(){try{const r=await fetch('/api/state',{headers:H});const j=await r.json();
document.getElementById('t').textContent=j.name;
document.getElementById('s').textContent=(j.ssid||j.apSsid)+' | '+j.ip+' | '+(j.temp==null?'--':j.temp+' C')+' | '+j.powerW+' W';
const g=document.getElementById('g');g.innerHTML='';
j.devices.forEach(d=>{const b=document.createElement('button');b.textContent=d.name+(d.on?' - ON':' - OFF');
if(d.on)b.className='on';if(d.type!='pir')b.onclick=async()=>{await fetch('/api/set?id='+d.id+'&on='+(d.on?0:1),{method:'POST',headers:H});load()};
g.appendChild(b)})}catch(e){document.getElementById('s').textContent='offline'}}
load();setInterval(load,2000);
</script></body></html>)HTML";

void handleRoot() { server.send_P(200, "text/html; charset=utf-8", INDEX_HTML); }
void handleNotFound() { sendError(404, "not found"); }

// ---------------------------------------------------------------- background work
void wifiTick(uint32_t now) {
  // 1) A new Wi-Fi was requested by the app: switch to it (DHCP).
  if (connState == CS_PENDING) {
    if ((int32_t)(now - pendAt) >= 0) {
      connState = CS_TRYING;
      tryStart = now;
      WiFi.disconnect();
      beginSta(pendSsid, pendPass, false);
      Serial.print("Trying new Wi-Fi: ");
      Serial.println(pendSsid);
    }
    return;
  }

  // 2) Waiting for the new Wi-Fi: keep it if it works, otherwise go back to the old one.
  if (connState == CS_TRYING) {
    if (now - tryStart > 1500 && staUp() && WiFi.SSID() == pendSsid) {
      saveWifi(pendSsid, pendPass);
      curSsid = pendSsid; curPass = pendPass; curStatic = false;
      connState = CS_IDLE;
      Serial.print("New Wi-Fi saved. IP: ");
      Serial.println(WiFi.localIP());
    } else if (now - tryStart > 25000) {
      lastWifiError = "could not connect - check the Wi-Fi name and password";
      connState = CS_IDLE;
      Serial.println("New Wi-Fi failed - going back to the old one.");
      WiFi.disconnect();
      if (curSsid.length()) beginSta(curSsid, curPass, curStatic);
    }
    return;
  }

  // 3) Normal life: reconnect when the router is gone, hotspot only while there is no Wi-Fi.
  bool up = staUp();
  if (up != lastUp) {
    lastUp = up;
    lastChange = now;
    if (up) startMdns();
  }
  if (up) {
    if (apActive && now - lastChange > 30000UL) stopAp();
  } else {
    uint32_t retryEvery = apActive ? 30000UL : 10000UL;
    if (curSsid.length() && now - lastWifiTry > retryEvery) {
      lastWifiTry = now;
      WiFi.disconnect();
      beginSta(curSsid, curPass, curStatic);
    }
    if (!apActive && now - lastChange > 20000UL) startAp();
  }
}

void tick() {
  uint32_t now = millis();

  // PIR is read on every pass so short pulses are not missed.
  for (size_t i = 0; i < nDevices; i++)
    if (!strcmp(devices[i].type, "pir")) devices[i].on = digitalRead(devices[i].pin) == HIGH;

  // Once per second: energy estimate + safety auto-off timers.
  if (now - lastTick >= 1000) {
    float dt = (now - lastTick) / 1000.0f;
    lastTick = now;
    for (size_t i = 0; i < nDevices; i++) {
      Device& d = devices[i];
      if (strcmp(d.type, "switch") || !d.on) continue;
      d.wh += d.watts * dt / 3600.0f;
      if (d.autoOffMin && (now - d.onSince) > (uint32_t)d.autoOffMin * 60000UL) setDevice(d, false);
    }
  }

#if HAS_DHT
  if (now - lastDht >= 3000) {            // DHT22 needs >= 2 s between reads
    lastDht = now;
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t)) tempC = t;
    if (!isnan(h)) humidity = h;
  }
#endif

  wifiTick(now);

  if (restartPending && (int32_t)(now - restartAt) >= 0) {
    Serial.println("Restarting ...");
    delay(50);
    ESP.restart();
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println("ESP32 Smart Home v" FW_VERSION);

  WiFi.persistent(false);                  // we keep the Wi-Fi in our own settings
  loadWifiPrefs();

  if (!loadDevices()) seedDevices();       // very first start -> default devices
  for (size_t i = 0; i < nDevices; i++) initPin(devices[i]);

#if HAS_DHT
  dht.begin();
#endif

  bootWifi();
  lastUp = staUp();
  lastChange = millis();
  startMdns();

  const char* wanted[] = {"X-API-Key"};
  server.collectHeaders(wanted, 1);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/state",         HTTP_ANY, handleState);
  server.on("/api/set",           HTTP_ANY, handleSet);
  server.on("/api/dnd",           HTTP_ANY, handleDnd);
  server.on("/api/scene",         HTTP_ANY, handleScene);
  server.on("/api/device/add",    HTTP_ANY, handleDeviceAdd);
  server.on("/api/device/update", HTTP_ANY, handleDeviceUpdate);
  server.on("/api/device/delete", HTTP_ANY, handleDeviceDelete);
  server.on("/api/wifi/status",   HTTP_ANY, handleWifiStatus);
  server.on("/api/wifi/scan",     HTTP_ANY, handleWifiScan);
  server.on("/api/wifi/connect",  HTTP_ANY, handleWifiConnect);
  server.on("/api/wifi/reset",    HTTP_ANY, handleWifiReset);
  server.onNotFound(handleNotFound);
  server.begin();
  lastTick = millis();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
  tick();
  delay(2);
}
