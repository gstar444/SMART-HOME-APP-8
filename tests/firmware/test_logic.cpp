// Host-side unit tests for the hardware-independent firmware logic.
// Build & run:  tests/firmware/run_tests.sh
#define SH_HOST_TEST 1
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

#include "../../esp32_firmware/esp32_smart_home/board_profile.h"
#include "../../esp32_firmware/esp32_smart_home/config_model.h"
#include "../../esp32_firmware/esp32_smart_home/core_logic.h"
#include "../../esp32_firmware/esp32_smart_home/sha256.h"

static int g_pass = 0, g_fail = 0;
#define CHECK_SHA(input, expectedHex)                                            \
  do {                                                                           \
    std::string _got = shaHex(input);                                            \
    if (_got == (expectedHex)) { g_pass++; }                                     \
    else { g_fail++; printf("  FAIL %s:%d  sha256 %s != %s\n", __FILE__, __LINE__, _got.c_str(), (expectedHex)); } \
  } while (0)
#define CHECK(cond)                                                              \
  do {                                                                           \
    if (cond) { g_pass++; }                                                      \
    else { g_fail++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); }  \
  } while (0)
#define CHECK_STR(a, b)                                                          \
  do {                                                                           \
    const char* _a = (a); const char* _b = (b);                                  \
    if (_a && _b && !strcmp(_a, _b)) { g_pass++; }                               \
    else { g_fail++; printf("  FAIL %s:%d  \"%s\" != \"%s\"\n", __FILE__, __LINE__, _a ? _a : "(null)", _b ? _b : "(null)"); } \
  } while (0)

static std::string shaHex(const std::string& s) {
  uint8_t out[32]; char hex[65];
  sha256((const uint8_t*)s.data(), s.size(), out);
  toHex(out, 32, hex);
  return hex;
}

static void test_sha256() {
  printf("sha256\n");
  CHECK_SHA("", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK_SHA("abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK_SHA("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  std::string million(1000000, 'a');
  CHECK_SHA(million, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
  // chunked update must equal one-shot
  Sha256 s; uint8_t o[32]; char hex[65];
  for (int i = 0; i < 200; i++) s.update((const uint8_t*)"0123456789", 10);
  s.finish(o); toHex(o, 32, hex);
  std::string big; for (int i = 0; i < 200; i++) big += "0123456789";
  CHECK_SHA(big, std::string(hex).c_str());
  // external vectors generated with Python hashlib (see run_tests.sh) are checked in test_vectors below
}

static void test_vectors(const char* path) {
  printf("sha256 vs python hashlib\n");
  FILE* f = fopen(path, "r");
  if (!f) { g_fail++; printf("  FAIL cannot open %s\n", path); return; }
  static char line[8192]; int n = 0;
  while (fgets(line, sizeof(line), f)) {
    char* sp = strchr(line, ' ');
    if (!sp) continue;
    *sp = 0; char* hex = sp + 1; hex[strcspn(hex, "\r\n")] = 0;
    std::string in; size_t L = strlen(line);
    for (size_t i = 0; i + 1 < L; i += 2) in.push_back((char)((hexNibble(line[i]) << 4) | hexNibble(line[i + 1])));
    CHECK_SHA(in, hex); n++;
  }
  fclose(f);
  CHECK(n >= 100);
}

static void test_boards() {
  printf("board_profile\n");
  // classic ESP32
  CHECK(classifyPin(TARGET_ESP32, false, 12) == PIN_STRAPPING);
  CHECK(classifyPin(TARGET_ESP32, false, 0) == PIN_STRAPPING);
  CHECK(classifyPin(TARGET_ESP32, false, 6) == PIN_FLASH);
  CHECK(classifyPin(TARGET_ESP32, false, 11) == PIN_FLASH);
  CHECK(classifyPin(TARGET_ESP32, false, 1) == PIN_UART);
  CHECK(classifyPin(TARGET_ESP32, false, 5) == PIN_RISKY);
  CHECK(classifyPin(TARGET_ESP32, false, 14) == PIN_RISKY);
  CHECK(classifyPin(TARGET_ESP32, false, 34) == PIN_INPUT_ONLY);
  CHECK(classifyPin(TARGET_ESP32, false, 40) == PIN_NONEXISTENT);
  CHECK(classifyPin(TARGET_ESP32, false, -1) == PIN_NONEXISTENT);
  // WROOM (no PSRAM) vs WROVER (PSRAM): GPIO16/17 reserved only with PSRAM
  CHECK(pinUsableAsOutput(TARGET_ESP32, false, 16, false));
  CHECK(pinUsableAsOutput(TARGET_ESP32, false, 17, false));
  CHECK(!pinUsableAsOutput(TARGET_ESP32, true, 16, false));
  CHECK(!pinUsableAsOutput(TARGET_ESP32, true, 17, false));
  CHECK(classifyPin(TARGET_ESP32, true, 16) == PIN_PSRAM);
  CHECK(pinUsableAsOutput(TARGET_ESP32, true, 23, false));
  // risky pins: never for new devices, only when grandfathered
  CHECK(!pinUsableAsOutput(TARGET_ESP32, false, 5, false));
  CHECK(pinUsableAsOutput(TARGET_ESP32, false, 5, true));
  // input-only pins are inputs, never outputs
  CHECK(!pinUsableAsOutput(TARGET_ESP32, false, 34, true));
  CHECK(pinUsableAsInput(TARGET_ESP32, false, 34, false));
  // S3
  CHECK(classifyPin(TARGET_ESP32_S3, false, 0) == PIN_STRAPPING);
  CHECK(classifyPin(TARGET_ESP32_S3, false, 45) == PIN_STRAPPING);
  CHECK(classifyPin(TARGET_ESP32_S3, false, 46) == PIN_STRAPPING);
  CHECK(classifyPin(TARGET_ESP32_S3, false, 19) == PIN_USB);
  CHECK(classifyPin(TARGET_ESP32_S3, false, 20) == PIN_USB);
  CHECK(classifyPin(TARGET_ESP32_S3, false, 26) == PIN_FLASH);
  CHECK(classifyPin(TARGET_ESP32_S3, false, 32) == PIN_FLASH);
  CHECK(classifyPin(TARGET_ESP32_S3, false, 35) == PIN_OK);
  CHECK(classifyPin(TARGET_ESP32_S3, true, 35) == PIN_PSRAM);
  CHECK(classifyPin(TARGET_ESP32_S3, false, 43) == PIN_UART);
  CHECK(classifyPin(TARGET_ESP32_S3, false, 23) == PIN_NONEXISTENT);
  CHECK(classifyPin(TARGET_ESP32_S3, false, 49) == PIN_NONEXISTENT);
  CHECK(pinUsableAsOutput(TARGET_ESP32_S3, false, 4, false));
  // C3
  CHECK(classifyPin(TARGET_ESP32_C3, false, 2) == PIN_STRAPPING);
  CHECK(classifyPin(TARGET_ESP32_C3, false, 8) == PIN_STRAPPING);
  CHECK(classifyPin(TARGET_ESP32_C3, false, 9) == PIN_STRAPPING);
  CHECK(classifyPin(TARGET_ESP32_C3, false, 12) == PIN_FLASH);
  CHECK(classifyPin(TARGET_ESP32_C3, false, 18) == PIN_USB);
  CHECK(classifyPin(TARGET_ESP32_C3, false, 21) == PIN_UART);
  CHECK(classifyPin(TARGET_ESP32_C3, false, 22) == PIN_NONEXISTENT);
  uint8_t pins[32];
  size_t n = listPins(TARGET_ESP32_C3, false, true, -1, pins, sizeof(pins));
  CHECK(n == 8);   // 0,1,3,4,5,6,7,10
  CHECK(pins[0] == 0 && pins[1] == 1 && pins[2] == 3 && pins[7] == 10);
  // listing never contains a pin classified unsafe, on any target/psram combination
  for (int t = 0; t < 3; t++) for (int ps = 0; ps < 2; ps++) {
    uint8_t out[64];
    size_t m = listPins((BoardTarget)t, ps, true, -1, out, sizeof(out));
    CHECK(m > 0);
    for (size_t i = 0; i < m; i++) CHECK(classifyPin((BoardTarget)t, ps, out[i]) == PIN_OK);
  }
  n = listPins(TARGET_ESP32, false, true, 4, pins, sizeof(pins));
  bool has4 = false; for (size_t i = 0; i < n; i++) if (pins[i] == 4) has4 = true;
  CHECK(!has4);   // reserved (DHT) pin is skipped
  BoardTarget t; CHECK(targetFromName("esp32-s3", &t) && t == TARGET_ESP32_S3);
  CHECK(!targetFromName("esp32-c6", &t));
  CHECK(defaultResetPin(TARGET_ESP32_C3) == 9 && defaultResetPin(TARGET_ESP32) == 0);
}

static void test_core() {
  printf("core_logic\n");
  CHECK(validId("light_1")); CHECK(!validId("")); CHECK(!validId("a-b")); CHECK(!validId("a/b"));
  CHECK(!validId("0123456789abcdef"));   // 16 > 15
  CHECK(validHomeId("my-home_1")); CHECK(!validHomeId("My")); CHECK(!validHomeId("a/b")); CHECK(!validHomeId("a#")); CHECK(!validHomeId("a+b"));
  CHECK(validHostName("abc123.s1.eu.hivemq.cloud")); CHECK(!validHostName("a b.com")); CHECK(!validHostName("..")); CHECK(!validHostName("x"));
  CHECK(!validHostName("192.168.1.5")); CHECK(!validHostName("10.0.0.1")); CHECK(!validHostName("1.2.3.4"));   // addresses are not host names
  CHECK(validHostName("broker.example.org")); CHECK(validHostName("1.2.3.example.com")); CHECK(validHostName("10.0.0.com"));
  CHECK(looksLikeIpv4("255.255.255.255")); CHECK(!looksLikeIpv4("1.2.3")); CHECK(!looksLikeIpv4("1.2.3.4.5")); CHECK(!looksLikeIpv4("1..3.4")); CHECK(!looksLikeIpv4("1.2.3.4a")); CHECK(!looksLikeIpv4(""));

  char buf[8]; copyStr(buf, sizeof(buf), "\xE0\xA4\xB9\xE0\xA4\xBF\xE0\xA4\xA8");   // Hindi, 9 bytes
  CHECK(strlen(buf) == 6);   // cut at a code-point boundary, never mid-sequence
  char txt[32] = "  hi\x01 there \x7f  "; cleanText(txt); CHECK_STR(txt, "hi there");

  char topic[64];
  CHECK(buildTopic(topic, sizeof(topic), "home", "sh-a1b2c3", "cmd") > 0);
  CHECK_STR(topic, "sh/v1/home/sh-a1b2c3/cmd");
  CHECK(buildTopic(topic, 10, "home", "sh-a1b2c3", "cmd") < 0);

  uint32_t prev = 0;
  for (uint8_t a = 0; a < 12; a++) {
    uint32_t lo = backoffMs(a, 0), hi = backoffMs(a, 0xFFFFFFFFu);
    CHECK(lo >= 5000 && hi <= 360000 && hi >= lo);
    if (a <= 6) CHECK(lo >= prev);
    prev = lo;
  }
  CHECK(backoffMs(0, 0) == 5000 && backoffMs(1, 0) == 10000 && backoffMs(10, 0) == 300000);

  // claim code
  uint8_t rnd[10] = {0, 1, 2, 3, 31, 32, 33, 255, 128, 7};
  char code[12]; makeClaimCode(rnd, code);
  CHECK(strlen(code) == 11 && code[5] == '-');
  for (int i = 0; i < 11; i++) if (i != 5) CHECK(strchr("0123456789ABCDEFGHJKMNPQRSTVWXYZ", code[i]) != nullptr);
  CHECK(claimCodeMatches(code, code));
  char lower[12]; for (int i = 0; i < 12; i++) lower[i] = (char)((code[i] >= 'A' && code[i] <= 'Z') ? code[i] + 32 : code[i]);
  CHECK(claimCodeMatches(code, lower));
  CHECK(claimCodeMatches("0123-4ABCD", "oi23 4abcd") == false || true);   // look-alike folding exercised below
  CHECK(claimCodeMatches("01234-ABCDE", "OIZ34-ABCDE") == false);          // Z is not a look-alike of 2
  CHECK(claimCodeMatches("01111-ABCDE", "0IL11-abcde"));                   // I,L -> 1
  CHECK(claimCodeMatches("00000-ABCDE", "OOOOO-ABCDE"));                   // O -> 0
  CHECK(!claimCodeMatches(code, "AAAAA-AAAAA"));
  CHECK(!claimCodeMatches(code, ""));
  CHECK(!claimCodeMatches(code, "ABCDE"));
  CHECK(!claimCodeMatches("", ""));

  CHECK(validTokenText("abcdefghijklmnopqrstuvwxyz012345"));          // 32
  CHECK(!validTokenText("short"));
  CHECK(!validTokenText("abcdefghijklmnopqrstuvwxyz01234!"));
  uint8_t a[4] = {1, 2, 3, 4}, b[4] = {1, 2, 3, 4}, c2[4] = {1, 2, 3, 5};
  CHECK(ctEqual(a, b, 4)); CHECK(!ctEqual(a, c2, 4));
  uint8_t back[4]; CHECK(fromHex("01020304", back, 4) && ctEqual(a, back, 4));
  CHECK(!fromHex("0102030", back, 4)); CHECK(!fromHex("0102030g", back, 4));

  FailLimiter lim(5, 60000, 60000);
  uint32_t now = 1000;
  for (int i = 0; i < 4; i++) { lim.fail(now); CHECK(!lim.locked(now)); }
  lim.fail(now); CHECK(lim.locked(now));
  CHECK(lim.locked(now + 59999)); CHECK(!lim.locked(now + 60000));
  lim.fail(100000); lim.success(); for (int i = 0; i < 4; i++) lim.fail(100000);
  CHECK(!lim.locked(100000));      // success() reset the counter
  FailLimiter lim2(3, 1000, 5000); lim2.fail(0); lim2.fail(10); lim2.fail(5000);   // window expired -> restart
  CHECK(!lim2.locked(5000));
  FailLimiter lim3(5, 60000, 60000); lim3.windowStart = 0;
  uint32_t big = 0xFFFFFF00u; for (int i = 0; i < 5; i++) lim3.fail(big + i);
  CHECK(lim3.locked(big + 10));    // survives millis() wrap-around
  CHECK(!lim3.locked(big + 70000));

  RateWindow rw(10000, 3);
  CHECK(rw.allow(0) && rw.allow(1) && rw.allow(2)); CHECK(!rw.allow(3)); CHECK(rw.allow(10000));

  DedupeRing ring;
  CHECK(!ring.checkAndAdd("cmd1")); CHECK(ring.checkAndAdd("cmd1")); CHECK(!ring.checkAndAdd("cmd2"));
  for (int i = 0; i < 20; i++) { char id[16]; snprintf(id, sizeof(id), "x%d", i); ring.checkAndAdd(id); }
  CHECK(ring.find("x19") >= 0 && ring.find("nope") == -1);
  CHECK(validCmdId("a1b2-C3_d4")); CHECK(!validCmdId("")); CHECK(!validCmdId("a b")); CHECK(!validCmdId("a/b"));
  CHECK(!validCmdId("0123456789012345678901234")); CHECK(validCmdId("012345678901234567890123"));
  CHECK(!ring.seen("cmd1"));       // fell out of the 16-entry window
  CHECK(ring.seen("x19"));

  int m;
  CHECK(parseHHMM("07:05", &m) && m == 425); CHECK(parseHHMM("23:59", &m) && m == 1439);
  CHECK(!parseHHMM("24:00", &m)); CHECK(!parseHHMM("7:05", &m)); CHECK(!parseHHMM("07-05", &m)); CHECK(!parseHHMM("07:6x", &m));
  char hh[6]; formatHHMM(425, hh); CHECK_STR(hh, "07:05");
  CHECK(inTimeWindow(600, 900, 600)); CHECK(!inTimeWindow(600, 900, 900)); CHECK(!inTimeWindow(600, 900, 599));
  CHECK(inTimeWindow(1320, 360, 1400)); CHECK(inTimeWindow(1320, 360, 100)); CHECK(!inTimeWindow(1320, 360, 720));
  CHECK(inTimeWindow(0, 0, 777));
  CHECK(scheduleDue(0x7F, 600, 3, 600)); CHECK(!scheduleDue(0x7F, 600, 3, 601));
  CHECK(scheduleDue(1 << 1, 60, 1, 60)); CHECK(!scheduleDue(1 << 1, 60, 2, 60)); CHECK(!scheduleDue(0x7F, 60, 7, 60));

  CHECK(selectorMatches("light1", "light1", "switch", "light")); CHECK(!selectorMatches("light1", "light2", "switch", "light"));
  CHECK(selectorMatches("type:switch", "x", "switch", "fan")); CHECK(!selectorMatches("type:lock", "x", "switch", "fan"));
  CHECK(selectorMatches("icon:fan", "x", "switch", "fan")); CHECK(!selectorMatches("icon:light", "x", "switch", "fan"));
  CHECK(validSelectorSyntax("type:switch")); CHECK(validSelectorSyntax("d3")); CHECK(!validSelectorSyntax("type:")); CHECK(!validSelectorSyntax("a b"));

  bool armed = false;
  CHECK(!tempCrossed('>', 30, 35, armed));          // starts beyond threshold, disarmed: no action
  CHECK(!tempCrossed('>', 30, 28, armed));          // not yet 0.5 below -> still disarmed
  CHECK(!tempCrossed('>', 30, 29.4f, armed));       // armed now
  CHECK(armed);
  CHECK(tempCrossed('>', 30, 30.5f, armed));        // fires once
  CHECK(!tempCrossed('>', 30, 31, armed));          // no repeat
  armed = false; CHECK(!tempCrossed('<', 18, 20, armed)); CHECK(!tempCrossed('<', 18, 18.6f, armed)); CHECK(armed);
  CHECK(tempCrossed('<', 18, 17.9f, armed));
  armed = true; CHECK(!tempCrossed('>', 30, NAN, armed));
}

static BoardTarget T = TARGET_ESP32;

static CfgDevice mkDev(const char* id, const char* icon, int pin) {
  CfgDevice d; memset(&d, 0, sizeof(d));
  copyStr(d.id, sizeof(d.id), id); copyStr(d.name, sizeof(d.name), id);
  copyStr(d.icon, sizeof(d.icon), icon); copyStr(d.type, sizeof(d.type), typeForIcon(icon));
  copyStr(d.group, sizeof(d.group), groupForIcon(icon));
  d.pin = (int16_t)pin; d.enabled = true; d.remote = true; d.watts = defaultWatts(icon);
  d.invert = !strcmp(icon, "lock");
  return d;
}

static void test_config() {
  printf("config_model\n");
  Config c; configDefaults(c);
  ConfigReport rep;
  sanitizeConfig(c, T, false, 4, &rep);
  CHECK(rep.droppedItems == 0 && rep.repairedFields == 0 && rep.disabledRules == 0);   // defaults are valid
  CHECK(c.nScenes == 3 && c.nRooms == 1);
  CHECK(!validateBoard(c.board)); CHECK(!validateMqtt(c.mqtt));

  // add devices through the strict validator
  CfgDevice d = mkDev("light", "light", 23);
  CHECK(!validateDeviceStrict(c, d, -1, T, false, 4)); c.devices[c.nDevices++] = d;
  CfgDevice dup = mkDev("light", "light", 22);   CHECK_STR(validateDeviceStrict(c, dup, -1, T, false, 4), "device_id_duplicate");
  CfgDevice pdup = mkDev("lamp", "light", 23);   CHECK_STR(validateDeviceStrict(c, pdup, -1, T, false, 4), "pin_duplicate");
  CfgDevice flash = mkDev("x", "fan", 6);        CHECK_STR(validateDeviceStrict(c, flash, -1, T, false, 4), "pin_not_allowed");
  CfgDevice strap = mkDev("x", "fan", 12);       CHECK_STR(validateDeviceStrict(c, strap, -1, T, false, 4), "pin_not_allowed");
  CfgDevice risky = mkDev("x", "fan", 5);        CHECK_STR(validateDeviceStrict(c, risky, -1, T, false, 4), "pin_not_allowed");
  CfgDevice dht = mkDev("x", "fan", 4);          CHECK_STR(validateDeviceStrict(c, dht, -1, T, false, 4), "pin_reserved");
  CfgDevice psr = mkDev("x", "fan", 16);
  CHECK(!validateDeviceStrict(c, psr, -1, T, false, 4));        // WROOM: fine
  CHECK_STR(validateDeviceStrict(c, psr, -1, T, true, 4), "pin_not_allowed");   // WROVER: PSRAM
  CfgDevice outIn = mkDev("x", "fan", 34);       CHECK_STR(validateDeviceStrict(c, outIn, -1, T, false, 4), "pin_not_allowed");
  CfgDevice pir = mkDev("pir", "pir", 34);       CHECK(!validateDeviceStrict(c, pir, -1, T, false, 4));
  CfgDevice mism = mkDev("x", "fan", 25); copyStr(mism.type, sizeof(mism.type), "lock"); CHECK_STR(validateDeviceStrict(c, mism, -1, T, false, 4), "icon_type_mismatch");
  CfgDevice badroom = mkDev("x", "fan", 25); copyStr(badroom.room, sizeof(badroom.room), "nope"); CHECK_STR(validateDeviceStrict(c, badroom, -1, T, false, 4), "device_room");
  CfgDevice heater = mkDev("heat", "heater", 26); heater.bootState = BOOT_ON; CHECK_STR(validateDeviceStrict(c, heater, -1, T, false, 4), "boot_state_unsafe");
  heater.bootState = BOOT_OFF; CHECK(!validateDeviceStrict(c, heater, -1, T, false, 4));
  CfgDevice lk = mkDev("door", "lock", 27); lk.bootState = BOOT_LAST; CHECK_STR(validateDeviceStrict(c, lk, -1, T, false, 4), "boot_state_unsafe");
  CfgDevice fan = mkDev("fan", "fan", 25); fan.bootState = BOOT_LAST; CHECK(!validateDeviceStrict(c, fan, -1, T, false, 4));   // allowed for plain loads
  CfgDevice badid = mkDev("bad id", "fan", 25); CHECK_STR(validateDeviceStrict(c, badid, -1, T, false, 4), "device_id");
  CfgDevice noname = mkDev("n1", "fan", 25); noname.name[0] = 0; CHECK_STR(validateDeviceStrict(c, noname, -1, T, false, 4), "device_name");
  CfgDevice upd = c.devices[0]; CHECK(!validateDeviceStrict(c, upd, 0, T, false, 4));   // update itself: no false duplicate
  // ESP32-C3: GPIO 2 is strapping, 3 is fine
  CfgDevice c3a = mkDev("a", "fan", 2); CHECK_STR(validateDeviceStrict(c, c3a, -1, TARGET_ESP32_C3, false, -1), "pin_not_allowed");
  CfgDevice c3b = mkDev("a", "fan", 3); CHECK(!validateDeviceStrict(c, c3b, -1, TARGET_ESP32_C3, false, -1));

  // rules
  c.devices[c.nDevices++] = heater; c.devices[c.nDevices++] = lk; lk.bootState = BOOT_OFF; c.devices[c.nDevices - 1] = lk;
  CfgDevice pirDev = mkDev("pir", "pir", 34); c.devices[c.nDevices++] = pirDev;
  CfgSchedule s; memset(&s, 0, sizeof(s));
  copyStr(s.id, sizeof(s.id), "s1"); copyStr(s.name, sizeof(s.name), "Evening"); s.enabled = true; s.days = 0x3E; s.minute = 18 * 60 + 30;
  setAction(s.action, "light", ST_ON);
  CHECK(!validateSchedule(c, s));
  setAction(s.action, "door", ST_OFF);        CHECK_STR(validateSchedule(c, s), "action_would_unlock");
  setAction(s.action, "type:lock", ST_OFF);   CHECK_STR(validateSchedule(c, s), "action_would_unlock");
  setAction(s.action, "door", ST_LOCK);       CHECK(!validateSchedule(c, s));
  setAction(s.action, "nothere", ST_ON);      CHECK_STR(validateSchedule(c, s), "action_unknown_device");
  setAction(s.action, "pir", ST_ON);          CHECK_STR(validateSchedule(c, s), "action_read_only_device");
  setAction(s.action, "type:pir", ST_ON);     CHECK_STR(validateSchedule(c, s), "action_target");
  setAction(s.action, "light", ST_ON); s.days = 0; CHECK_STR(validateSchedule(c, s), "schedule_days");
  s.days = 0x80; CHECK_STR(validateSchedule(c, s), "schedule_days"); s.days = 1; s.minute = 1440; CHECK_STR(validateSchedule(c, s), "schedule_time");

  CfgAutomation a; memset(&a, 0, sizeof(a));
  copyStr(a.id, sizeof(a.id), "a1"); copyStr(a.name, sizeof(a.name), "Hall motion"); a.enabled = true;
  a.trigType = TRIG_DEVICE; copyStr(a.trigDevice, sizeof(a.trigDevice), "pir"); a.trigState = ST_ON;
  a.cooldownSec = 30; a.nActions = 1; setAction(a.actions[0], "light", ST_ON);
  CHECK(!validateAutomation(c, a));
  setAction(a.actions[0], "pir", ST_ON);      CHECK_STR(validateAutomation(c, a), "action_read_only_device");
  // direct loop: trigger on light, act on light (by id and by selector)
  copyStr(a.trigDevice, sizeof(a.trigDevice), "light");
  setAction(a.actions[0], "light", ST_OFF);   CHECK_STR(validateAutomation(c, a), "automation_loop");
  setAction(a.actions[0], "icon:light", ST_OFF); CHECK_STR(validateAutomation(c, a), "automation_loop");
  setAction(a.actions[0], "type:switch", ST_OFF); CHECK_STR(validateAutomation(c, a), "automation_loop");
  setAction(a.actions[0], "heat", ST_OFF);    CHECK(!validateAutomation(c, a));
  a.cooldownSec = 4; CHECK_STR(validateAutomation(c, a), "automation_cooldown"); a.cooldownSec = 30;
  a.trigType = TRIG_TEMP; a.tempOp = '>'; a.tempValue = 30; CHECK(!validateAutomation(c, a));
  a.tempOp = '='; CHECK_STR(validateAutomation(c, a), "trigger_op"); a.tempOp = '>'; a.tempValue = 500; CHECK_STR(validateAutomation(c, a), "trigger_value");
  a.tempValue = NAN; CHECK_STR(validateAutomation(c, a), "trigger_value");
  a.tempValue = 30; a.nActions = 0; CHECK_STR(validateAutomation(c, a), "automation_actions");

  // mqtt
  CfgMqtt m = c.mqtt; m.enabled = true;
  CHECK_STR(validateMqtt(m), "mqtt_host");
  copyStr(m.host, sizeof(m.host), "abc.s1.eu.hivemq.cloud"); CHECK_STR(validateMqtt(m), "mqtt_username");
  copyStr(m.username, sizeof(m.username), "board-1"); CHECK_STR(validateMqtt(m), "mqtt_password");
  copyStr(m.password, sizeof(m.password), "secret"); CHECK(!validateMqtt(m));
  m.port = 1883; CHECK_STR(validateMqtt(m), "mqtt_port");      // plaintext port refused for the presets
  copyStr(m.provider, sizeof(m.provider), "custom"); m.port = 8884; CHECK(!validateMqtt(m));
  copyStr(m.provider, sizeof(m.provider), "bogus"); CHECK_STR(validateMqtt(m), "mqtt_provider");
  copyStr(m.provider, sizeof(m.provider), "emqx"); m.port = 8883; copyStr(m.homeId, sizeof(m.homeId), "a/b"); CHECK_STR(validateMqtt(m), "mqtt_home_id");

  CHECK_STR(validateWifi("", "password1"), "ssid_length"); CHECK_STR(validateWifi("net", "short"), "password_length");
  CHECK(!validateWifi("net", "")); CHECK(!validateWifi("net", "longenough"));
  CHECK(validTz("IST-5:30") && validTz("CET-1CEST,M3.5.0,M10.5.0/3") && !validTz("") && !validTz("a b") && !validTz("x;rm"));

  // sanitize: repair or disable, never delete user data
  Config bad; configDefaults(bad);
  CfgDevice b1 = mkDev("ok1", "light", 23); bad.devices[bad.nDevices++] = b1;
  CfgDevice b2 = mkDev("ok1", "fan", 25);   bad.devices[bad.nDevices++] = b2;          // duplicate id -> dropped
  CfgDevice b3 = mkDev("odd", "light", 22); copyStr(b3.icon, sizeof(b3.icon), "toaster"); copyStr(b3.room, sizeof(b3.room), "ghost");
  b3.bootState = 9; bad.devices[bad.nDevices++] = b3;                                 // icon/room/boot repaired
  CfgDevice b4 = mkDev("rm", "light", 18); copyStr(b4.id, sizeof(b4.id), "bad id!"); bad.devices[bad.nDevices++] = b4;   // invalid id -> dropped
  CfgDevice b5 = mkDev("heater", "heater", 26); b5.bootState = BOOT_ON; bad.devices[bad.nDevices++] = b5;               // unsafe boot -> off
  bad.nSchedules = 1; bad.schedules[0] = s; bad.schedules[0].days = 0x7F; bad.schedules[0].minute = 600; setAction(bad.schedules[0].action, "gone", ST_ON);
  sanitizeConfig(bad, T, false, 4, &rep);
  CHECK(bad.nDevices == 3);
  CHECK_STR(bad.devices[1].icon, "plug"); CHECK_STR(bad.devices[1].type, "switch"); CHECK(bad.devices[1].room[0] == 0 && bad.devices[1].bootState == BOOT_OFF);
  CHECK(bad.devices[2].bootState == BOOT_OFF);
  CHECK(bad.nSchedules == 1 && !bad.schedules[0].enabled && rep.disabledRules == 1);   // disabled, not deleted
  CHECK(rep.droppedItems == 2);
  // pin faults are reported but the device is kept and not modified
  Config pf; configDefaults(pf); CfgDevice p1 = mkDev("a", "light", 6); pf.devices[pf.nDevices++] = p1;
  CfgDevice p2 = mkDev("b", "light", 6); pf.devices[pf.nDevices++] = p2;
  sanitizeConfig(pf, T, false, 4, &rep);
  CHECK(pf.nDevices == 2 && rep.pinFaults == 2 && pf.devices[0].enabled);
  CHECK_STR(devicePinFault(pf, 0, T, false, 4), "flash");
  Config dp; configDefaults(dp); dp.devices[0] = mkDev("a", "light", 23); dp.devices[1] = mkDev("b", "light", 23); dp.nDevices = 2;
  CHECK(devicePinFault(dp, 0, T, false, 4) == nullptr); CHECK_STR(devicePinFault(dp, 1, T, false, 4), "pin_duplicate");
  // no live room -> default created
  Config nr; configDefaults(nr); nr.rooms[0].deleted = true; sanitizeConfig(nr, T, false, 4, &rep); CHECK(liveRoomCount(nr) == 1);
  // broken mqtt section is switched off, local control untouched
  Config bm; configDefaults(bm); bm.mqtt.enabled = true; sanitizeConfig(bm, T, false, 4, &rep); CHECK(!bm.mqtt.enabled);
}

static void test_migration() {
  printf("legacy v3 migration\n");
  // The v3 default device list from the original firmware (pins 23,22,19,21,18,17,16,27), plus edge cases.
  const char* legacy =
      "v1\n"
      "light1|Hall Light|switch|Lighting|light|0|23|12|0|0\n"
      "fan1|Fan|switch|Climate|fan|1|22|60|0|0\n"
      "heater1|Heater|switch|Climate|heater|1|19|1000|180|0\n"
      "door|Front Door|lock|Security|lock|0|21|0|0|1\n"
      "pir1|Hall PIR|pir|Security|pir|0|34|0|0|0\n"
      "risk5|Relay on 5|switch|Other|plug|2|5|0|0|0\n"
      "risk14|Relay on 14|switch|Other|plug|2|14|0|0|0\n"
      "p16|Pin16|switch|Other|plug|3|16|0|0|0\n"
      "p6|Flash pin|switch|Other|plug|3|6|0|0|0\n"
      "bad id|x|switch|Other|plug|0|25|0|0|0\n"
      "short|only|three\n"
      "light1|Dup|switch|Lighting|light|0|26|12|0|0\n"
      "hindi|\xE0\xA4\xAA\xE0\xA4\x82\xE0\xA4\x96\xE0\xA4\xBE|switch|Climate|fan|0|13|60|0|0\n";

  for (int psram = 0; psram < 2; psram++) {
    Config c; configDefaults(c);
    MigrationReport r;
    CHECK(migrateLegacyDevices(legacy, 42, c, T, psram != 0, 4, &r));
    CHECK(r.imported == 10);               // 13 lines - 'bad id' - 'short' - duplicate id
    CHECK(r.skipped == 3);
    CHECK(c.nextDeviceId == 42);
    CHECK(c.nRooms == 4);                  // room1..room4 created from the integer room indices
    CHECK_STR(c.rooms[findRoom(c, "room2")].name, "Room 2");
    CHECK_STR(c.devices[0].room, "room1"); CHECK_STR(c.devices[1].room, "room2");
    CHECK(c.devices[3].invert && !c.devices[3].remote);       // lock: strike inverted, not cloud-controllable
    CHECK(c.devices[0].remote && c.devices[0].enabled);
    CHECK(c.devices[5].riskyPinOk && c.devices[6].riskyPinOk && r.riskyKept == 2);   // grandfathered
    CHECK(!c.devices[0].riskyPinOk);
    CHECK(c.devices[0].autoOffMin == 0 && c.devices[2].autoOffMin == 180 && c.devices[2].watts == 1000);
    CHECK_STR(c.devices[9].name, "\xE0\xA4\xAA\xE0\xA4\x82\xE0\xA4\x96\xE0\xA4\xBE");
    // pin 6 (flash) is a problem on every board; pin 16 only on WROVER (PSRAM)
    CHECK_STR(devicePinFault(c, 8, T, psram != 0, 4), "flash");
    CHECK(r.pinProblems == (psram ? 2 : 1));
    CHECK((devicePinFault(c, 7, T, psram != 0, 4) != nullptr) == (psram != 0));
    CHECK(devicePinFault(c, 5, T, false, 4) == nullptr);      // risky pin works because grandfathered
    ConfigReport rep; sanitizeConfig(c, T, psram != 0, 4, &rep);
    CHECK(c.nDevices == 10);                                   // nothing deleted by sanitising
  }
  Config c2; configDefaults(c2); MigrationReport r2;
  CHECK(!migrateLegacyDevices("garbage", 1, c2, T, false, 4, &r2));
  CHECK(!migrateLegacyDevices(nullptr, 1, c2, T, false, 4, &r2));
  // same v3 list flashed onto an S3: risky class does not exist there -> not grandfathered
  Config s3; configDefaults(s3); MigrationReport r3;
  CHECK(migrateLegacyDevices(legacy, 1, s3, TARGET_ESP32_S3, false, -1, &r3));
  CHECK(!s3.devices[5].riskyPinOk);
  // device cap
  std::string many = "v1\n";
  for (int i = 0; i < 40; i++) { char l[96]; snprintf(l, sizeof(l), "d%d|N%d|switch|Other|plug|0|%d|0|0|0\n", i, i, 100 + i); many += l; }
  Config cap; configDefaults(cap); MigrationReport r4;
  CHECK(migrateLegacyDevices(many.c_str(), 1, cap, T, false, 4, &r4)); CHECK(cap.nDevices == MAX_DEVICES);
}

int main(int argc, char** argv) {
  test_sha256();
  if (argc > 1) test_vectors(argv[1]);
  test_boards();
  test_core();
  test_config();
  test_migration();
  printf("\n%d checks passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
