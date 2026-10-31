#include "net.h"

#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_system.h>
#if __has_include(<esp_random.h>)
#include <esp_random.h>   // esp_random()/esp_fill_random() moved here in ESP-IDF 5
#endif

#include "config_store.h"

enum ConnState : uint8_t { CS_IDLE, CS_PENDING, CS_TRYING };

static bool apActive = false;
static uint8_t wifiFailCount = 0;
static char apSsid[24] = "";
static ConnState connState = CS_IDLE;
static String pendSsid, pendPass;
static uint32_t pendAt = 0, tryStart = 0;
static char lastWifiError[40] = "";
static bool lastUp = false;
static uint32_t lastChange = 0, lastWifiTry = 0, apStartedAt = 0, restartAt = 0;
static char ntpTz[40] = "";
static bool mdnsStarted = false;

bool netStaUp() { return WiFi.status() == WL_CONNECTED; }
bool netApActive() { return apActive; }

String netIp() {
  if (netStaUp()) return WiFi.localIP().toString();
  if (apActive) return WiFi.softAPIP().toString();
  return String("");
}
const char* netMode() { return netStaUp() ? "station" : (apActive ? "setup" : "offline"); }

void netRestartSoon(uint32_t ms) { restartAt = millis() + ms; if (restartAt == 0) restartAt = 1; }

// ------------------------------------------------------------------ claim code
static void printClaimBanner() {
  // The ONLY place a secret is printed: the one-time setup code, while the board is not yet set up.
  Serial.println();
  Serial.println("==================================================");
  Serial.printf(" Board        : %s\n", gBoardId);
  Serial.printf(" Setup hotspot: %s\n", apSsid);
  Serial.printf(" CLAIM CODE   : %s\n", cfg.auth.claimCode);
  Serial.println(" Use it as the hotspot password and in the app's");
  Serial.println(" 'Add board' screen. It is deleted once Wi-Fi works.");
  Serial.println("==================================================");
}

void netEnsureClaimCode() {
  if (cfg.auth.claimed || cfg.auth.claimCode[0] != '\0') return;
  uint8_t rnd[10];
  esp_fill_random(rnd, sizeof(rnd));   // hardware RNG (strongest once the radio is on)
  makeClaimCode(rnd, cfg.auth.claimCode);
  configSave();
}

// ------------------------------------------------------------------ station / hotspot
static void beginSta(const char* ssid, const char* pass) {
  WiFi.mode(apActive ? WIFI_AP_STA : WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, pass);
}

static void startAp(bool force = false) {
  if (apActive || (!force && cfg.auth.claimCode[0] == '\0')) return;   // never an open or default-password hotspot
  WiFi.mode(WIFI_AP_STA);                                  // STA stays on so networks can be scanned
  apActive = WiFi.softAP(apSsid, cfg.auth.claimCode);
  apStartedAt = millis();
  Serial.printf("[net] setup hotspot \"%s\" %s\n", apSsid, apActive ? "ON (http://192.168.4.1/)" : "FAILED to start");
}

static void stopAp() {
  if (!apActive) return;
  WiFi.softAPdisconnect(true);
  apActive = false;
  Serial.println("[net] setup hotspot off");
}

static void startMdns() {
  if (mdnsStarted) MDNS.end();
  mdnsStarted = MDNS.begin(gBoardId);
  if (mdnsStarted) MDNS.addService("http", "tcp", 80);     // http://sh-xxxxxx.local
}

void netApplyTimezone() {
  setenv("TZ", cfg.board.tz, 1);
  tzset();
  if (netStaUp()) {
    configTzTime(cfg.board.tz, "pool.ntp.org", "time.google.com");
    copyStr(ntpTz, sizeof(ntpTz), cfg.board.tz);
  } else {
    ntpTz[0] = '\0';   // re-run NTP when the network returns
  }
}

void netBegin() {
  snprintf(apSsid, sizeof(apSsid), "SmartHome-%s", gBoardId + 3);   // gBoardId = "sh-xxxxxx"
  for (char* p = apSsid + 10; *p; p++) if (*p >= 'a' && *p <= 'f') *p = (char)(*p - 32);
  WiFi.persistent(false);
  WiFi.setHostname(gBoardId);
  WiFi.mode(WIFI_STA);
  setenv("TZ", cfg.board.tz, 1);
  tzset();

  netEnsureClaimCode();                       // after the radio is on: better entropy
  lastChange = millis();
  if (cfg.wifi.ssid[0] != '\0') {
    Serial.printf("[net] connecting to \"%s\"\n", cfg.wifi.ssid);
    beginSta(cfg.wifi.ssid, cfg.wifi.password);   // non-blocking: relays and the local API are already up
  }
  if (cfg.auth.claimCode[0] != '\0') {        // not set up yet (or Wi-Fi still unconfirmed)
    startAp();
    printClaimBanner();
  }
}

// ------------------------------------------------------------------ app-driven Wi-Fi change
bool netRequestConnect(const char* ssid, const char* pass, const char** err) {
  const char* e = validateWifi(ssid, pass);
  if (e) { *err = e; return false; }
  if (connState != CS_IDLE) { *err = "busy"; return false; }
  pendSsid = ssid;
  pendPass = pass ? pass : "";
  pendAt = millis() + 1000;            // let the HTTP answer leave before the radio switches
  connState = CS_PENDING;
  lastWifiError[0] = '\0';
  return true;
}

void netWifiStatusToJson(JsonObject o) {
  const char* state = connState != CS_IDLE ? "trying" : (lastWifiError[0] ? "failed" : "idle");
  o["state"] = state;
  o["connected"] = netStaUp();
  o["ssid"] = netStaUp() ? WiFi.SSID() : String("");
  o["ip"] = netIp();
  o["rssi"] = netStaUp() ? WiFi.RSSI() : 0;
  o["mode"] = netMode();
  o["apSsid"] = apActive ? apSsid : "";
  o["target"] = connState != CS_IDLE ? pendSsid : String("");
  o["saved"] = cfg.wifi.ssid[0] != '\0';
  o["error"] = (const char*)lastWifiError;
}

void netScanRequest() {
  WiFi.scanDelete();
  WiFi.scanNetworks(true);
}

void netScanToJson(JsonObject o) {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) { o["scanning"] = true; o["networks"].to<JsonArray>(); return; }
  if (n < 0) { WiFi.scanNetworks(true); o["scanning"] = true; o["networks"].to<JsonArray>(); return; }

  int order[40];
  int count = 0;
  for (int i = 0; i < n && count < 40; i++) {            // strongest first, one entry per network name
    String s = WiFi.SSID(i);
    if (s.length() == 0) continue;
    bool dup = false;
    for (int k = 0; k < count; k++) if (WiFi.SSID(order[k]) == s) { dup = true; break; }
    if (dup) continue;
    int at = count++;
    while (at > 0 && WiFi.RSSI(order[at - 1]) < WiFi.RSSI(i)) { order[at] = order[at - 1]; at--; }
    order[at] = i;
  }
  o["scanning"] = false;
  JsonArray arr = o["networks"].to<JsonArray>();
  for (int k = 0; k < count; k++) {
    JsonObject w = arr.add<JsonObject>();
    w["ssid"] = WiFi.SSID(order[k]);
    w["rssi"] = WiFi.RSSI(order[k]);
    w["secure"] = WiFi.encryptionType(order[k]) != WIFI_AUTH_OPEN;
  }
}

const char* netApSsid() { return apSsid; }

// Owner-initiated "forget Wi-Fi". A claimed board has no claim code, so a fresh one is issued and
// returned ONLY to the authenticated caller: it becomes the WPA2 password of the recovery hotspot.
// The hotspot alone grants nothing: the API still needs the bearer token.
void netForgetWifi() {
  cfg.wifi.ssid[0] = '\0';
  cfg.wifi.password[0] = '\0';
  cfg.auth.claimCode[0] = '\0';
  uint8_t rnd[10];
  esp_fill_random(rnd, sizeof(rnd));
  makeClaimCode(rnd, cfg.auth.claimCode);
  configSave();
  netRestartSoon(1500);
}

// ------------------------------------------------------------------ credential reset
void netCredentialReset() {
  Serial.println("[net] CREDENTIAL RESET: wiping Wi-Fi, MQTT login and access token");
  cfg.wifi.ssid[0] = '\0';
  cfg.wifi.password[0] = '\0';
  cfg.mqtt.enabled = false;
  cfg.mqtt.username[0] = '\0';
  cfg.mqtt.password[0] = '\0';
  cfg.auth.claimed = false;
  cfg.auth.tokenHash[0] = '\0';
  cfg.auth.claimCode[0] = '\0';
  netEnsureClaimCode();                 // saves
  netRestartSoon(1500);                 // device configuration (rooms, devices, rules) is kept
}

static void resetButtonTick(uint32_t now) {
  static uint32_t pressedAt = 0;
  static bool seenReleased = false, fired = false;
  bool pressed = digitalRead(resetButtonPin()) == LOW;
  if (!pressed) { seenReleased = true; pressedAt = 0; fired = false; return; }
  if (!seenReleased || fired) return;               // ignore a button already held when we booted
  if (pressedAt == 0) { pressedAt = now ? now : 1; return; }
  if ((uint32_t)(now - pressedAt) >= 8000UL) { fired = true; netCredentialReset(); }
}

// ------------------------------------------------------------------ main loop
void netTick(uint32_t now) {
  resetButtonTick(now);
  if (restartAt != 0 && (int32_t)(now - restartAt) >= 0) { delay(100); ESP.restart(); }

  // 1) the app asked for another network
  if (connState == CS_PENDING) {
    if ((int32_t)(now - pendAt) >= 0) {
      connState = CS_TRYING;
      tryStart = now;
      WiFi.disconnect();
      beginSta(pendSsid.c_str(), pendPass.c_str());
      Serial.printf("[net] trying new Wi-Fi \"%s\"\n", pendSsid.c_str());
    }
    return;
  }
  // 2) wait for it: keep it on success, otherwise go back to the previous network
  if (connState == CS_TRYING) {
    if (now - tryStart > 1500 && netStaUp() && WiFi.SSID() == pendSsid) {
      copyStr(cfg.wifi.ssid, sizeof(cfg.wifi.ssid), pendSsid.c_str());
      copyStr(cfg.wifi.password, sizeof(cfg.wifi.password), pendPass.c_str());
      configSave();
      connState = CS_IDLE;
      Serial.println("[net] new Wi-Fi saved");
    } else if (now - tryStart > 25000) {
      copyStr(lastWifiError, sizeof(lastWifiError), "connect_failed");
      connState = CS_IDLE;
      Serial.println("[net] new Wi-Fi failed - back to the previous network");
      WiFi.disconnect();
      if (cfg.wifi.ssid[0]) beginSta(cfg.wifi.ssid, cfg.wifi.password);
    }
    return;
  }

  // 3) normal life
  bool up = netStaUp();
  if (up) { wifiFailCount = 0; }
  if (up != lastUp) {
    lastUp = up;
    lastChange = now;
    if (up) {
      startMdns();
      Serial.printf("[net] connected, IP %s\n", WiFi.localIP().toString().c_str());
    } else {
      ntpTz[0] = '\0';
    }
  }
  if (up && strcmp(ntpTz, cfg.board.tz) != 0) {      // (re)start SNTP with the configured time zone
    configTzTime(cfg.board.tz, "pool.ntp.org", "time.google.com");
    copyStr(ntpTz, sizeof(ntpTz), cfg.board.tz);
  }
  if (up) {
    wifiFailCount = 0;
    if (apActive) stopAp();
  } else if (cfg.wifi.ssid[0]) {
    if (wifiFailCount < 2) {
      if ((uint32_t)(now - lastWifiTry) > 5000UL) {
        lastWifiTry = now;
        wifiFailCount++;
        WiFi.disconnect();
        beginSta(cfg.wifi.ssid, cfg.wifi.password);
      }
    } else if (!apActive) {
      // both tries failed: open the fallback hotspot (its own tick)
      startAp(true);
      delay(200);
    }
  }

  // Provisioning finished once the board has been claimed AND stayed on Wi-Fi for 30 s:
  // delete the claim code so nothing printed earlier works any more, and drop the hotspot.
  if (up && cfg.auth.claimed && cfg.auth.claimCode[0] != '\0' && (uint32_t)(now - lastChange) > 30000UL) {
    cfg.auth.claimCode[0] = '\0';
    configSave();
    stopAp();
    Serial.println("[net] provisioning complete: claim code deleted");
  }
}
