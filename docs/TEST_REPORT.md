# Test report – Phase 2 (firmware 4.0.0 / app 4.0.0)

**Bottom line.** The ESP32 *logic* and the *HTTP and MQTT handlers* were run on a PC and pass (1,157 checks, plus schema,
policy and cross-language protocol checks), and 17 deliberate regressions were all caught. **Nothing was compiled with the real
Arduino core, the real Flutter/Dart SDK, run on an ESP32, or run against a broker.** Treat your first real build as the first
real compile and use §5.

## 1. Environment of this verification

Available: `g++ 13.3`, `python 3.12`, `openssl`, `node`. **Not available:** Flutter/Dart SDK, `arduino-cli`/PlatformIO, ArduinoJson,
PubSubClient, ESP32 hardware, any network (so no `flutter pub get`, no package downloads, no broker).

## 2. Executed – and the result

| What | Command | Result |
|---|---|---|
| Pure firmware logic: GPIO policy for ESP32/S3/C3 (+PSRAM), ids, topics, backoff, claim code, limiter, de-dup ring, schedule/window/selector logic, SHA-256 (FIPS vectors, 1 M×`a`, 133 vectors vs Python `hashlib`), config validation, load-time repair, v3 migration | `tests/firmware/run_tests.sh` (C++11, `-Wall -Wextra -Wpedantic -Werror`, AddressSanitizer + UBSan) | **552 checks pass** |
| Config store + devices + automation against in-memory flash: fresh boot, atomic save/reload, redaction, **v3 NVS migration**, corrupt-file → backup, newer-schema read-only lock, strict replace, room-merge convergence under reordering, **safe pin order (OFF latched before OUTPUT)**, boot policies, never-unlock, auto-off, DND, remote-only scenes, schedule/automation edge/cooldown/loop-limit/conflict | same script | **208 checks pass** |
| **The real `api.cpp`** behind a functional WebServer stand-in: unclaimed behaviour, claim flow + brute-force lock-out, 10 ways to fail authentication, login lock-out and recovery, GET-vs-POST routing, v3 routes → 410, **no secret ever in a response**, device/rule validation over HTTP, mqtt/CA/board/replace/rotate/factory-reset endpoints | same script | **207 checks pass** |
| **The real `cloud.cpp`** with recording TLS/PubSubClient stand-ins: root-CA selection (ISRG X1 / DigiCert G2 / uploaded PEM override), refuses to connect without a CA, `setInsecure()` never called, connect/will/subscribe parameters, gating (disabled / no Wi-Fi), exponential backoff and recovery, lost session, **every command path**: allow-list, `remote` flag, locks, hardware fields ignored, malformed/oversized/wrong-topic input, duplicate ids, rate limit | same script | **190 checks pass** |
| Syntax/type check of all 8 firmware sources + the `.ino` for **each** chip (ESP32, S3, C3), warnings as errors; chip guard refuses other chips | `tests/firmware/syntax_check.sh` | pass |
| Config schema: example valid; **16 deliberately bad documents rejected** (incl. IP-address broker host); the firmware's own strict parser also accepts the example (with and without PSRAM) and rejects a flash-pin variant | `tests/firmware/test_schema.py`, `esp32_firmware/tools/validate_config.py` | pass |
| CA header: pinned SHA-256 of both roots verified, header compiles as C and the PEMs parse with `openssl`, regeneration is byte-identical | `esp32_firmware/tools/gen_ca_header.py` | pass |
| 17 "never do X" rules over the whole tree (no `setInsecure`, no default password, no secret printed, no HTTP_ANY/CORS, no accepted bad certificate, no cleartext policy, no debug-signed release, no keystore committed, …) | `sh tests/policy_checks.sh` | pass |
| App ↔ firmware contract extracted from both code bases: topic layout, protocol version, every command/field the app sends is allow-listed, all 47 JSON keys the app reads are emitted, all 19 REST calls are registered routes with the same method | `python3 tools/check_protocol_consistency.py` | pass |
| Dart sources, without an SDK: brackets, imports resolve, unused project imports, all `tr.…` strings defined, every call on `HomeController`/`LocalBoard` exists | `python3 tools/static_check_dart.py` | pass (**not** a substitute for `flutter analyze`) |
| `AndroidManifest.xml` / network config well-formed, workflow YAML parses, Gradle brackets balance | ad-hoc | pass |

### Do the tests actually catch problems? (mutation checks)
Seventeen deliberate regressions were applied one at a time and each was detected by at least one test:
**9 in the firmware** (unclaimed board accepts requests · `setInsecure()` before connect · configuration can unlock a lock · pin driven before the
OFF level is latched · command de-duplication off · cloud `device.update` accepts a GPIO pin · token check reduced to "any well-formed token" · redacted
config echoes the Wi-Fi password · cloud may switch `remote:false` devices), **4 in the app↔firmware protocol** (renamed state key · command removed from the
allow-list · a write route registered as GET · cloud accepting hardware fields) and **4 in the policy rules**.
Two cautions: one first attempt at the last firmware mutation only broke the *compile* (an unused variable under `-Werror`), so it was redone as a mutation
that compiles; and until defect 5 below was fixed, the cloud suite was silently not running in some of my own experiments, so those were redone.

### Defects found by this verification (all fixed)
1. `ITEM_FAIL` macro used `continue` inside `do{}while(0)`: bad config items would not have been skipped (found by review).
2. **Firmware accepted an IP address as MQTT host** while the app rejected it (found by the HTTP-level test) – now rejected on both sides and in the schema.
3. `WebServer::collectHeaders` was called with a `const char* const[]`; the real core wants a non-const array – **a real compile error that my
   first, too-permissive stand-in hid**; found when the stand-in was made match the real signature. (This is why a green host check is not a build.)
4. `esp_random` header location differs between ESP-IDF versions – guarded with `__has_include`.
5. `run_tests.sh` aborted *before* the cloud suite when started from the repository root (exactly how CI calls it) – fixed; two of my own mutation
   results had been silently invalid because of it, and were redone.
6. DHT sensor was never started (`begin()` missing), PIR/removed-slot handling wrote to input pins, `copyStr` used `memcpy` on overlapping buffers,
   a `-Wformat-truncation` warning, and several test-only mistakes (dangling `c_str()`, loop ordering, expectations about the 16-id memory).

## 3. NOT executed

| Not done | Why | How you check it |
|---|---|---|
| `flutter pub get`, `dart format`, `flutter analyze`, `flutter test` | no SDK | §5. **The Dart code and the 110 Dart test cases (written for `mqtt_models`, `secure_storage`, `snapshot_cache`, `mqtt_service`, `home_controller` incl. the start-up cache, `BoardHttp` over a real loopback socket, plus the updated `widget_test`) have never been compiled or run.** The Boards screen (claim / connect-existing / show / change token dialogs) has no automated test at all. The `pubspec.lock` is stale (it predates `shared_preferences`) and was not regenerated. |
| Arduino build of the firmware (all three chips) | no toolchain | §5. Expect to fix small API mismatches; firmware **size is unmeasured** (TLS + WebServer + ArduinoJson: pick a partition with ≥ ~1.4 MB app space) |
| Anything on real hardware: Wi-Fi/hotspot/mDNS/NTP, relay behaviour at reset, BOOT-button reset, DHT/PIR, LittleFS wear | no hardware | CLOUD_SETUP §6 and HARDWARE.md |
| TLS against a real broker: certificate chain validation, **host-name verification**, SNI, handshake time and memory on each chip | no network | CLOUD_SETUP §6 |
| Broker ACLs (HiveMQ/EMQX) | no account | CLOUD_SETUP §1–2 |
| Android build, secure-storage on a device, signing, cleartext behaviour of Dart sockets vs the network-security config | no SDK/device | build and install; the app's local link relies on Dart sockets not being subject to the Android cleartext policy |
| Cold-start time / frame rendering | no device | `flutter run --profile` + `adb shell am start -W`; **no startup measurement was made.** After a report that the app loaded slowly, three causes were found by reading the code and fixed: the CI fallback built a *debug* APK (now a release APK with a throw-away key), the first frame waited for a network answer (now the last known state is cached and shown at once), and the timeouts were 4/5/6 s (now 2/3/4 s) with the secure-storage read started in parallel. Whether that is enough is unmeasured. |
| Concurrency (two phones at once) | no clients | idempotent absolute commands, command-id de-dup and the 10 s manual-hold are unit-tested individually, not as a race |

## 4. What the passing tests do **not** prove
* The Arduino/ESP32/ArduinoJson/PubSubClient/WiFiClientSecure behaviour is **modelled by hand-written stand-ins** (`tests/firmware/stubs`). They check *our*
  logic and the consistency of our own modules; they can hide mismatches with the real libraries (defect 3 is proof). The JSON stand-in is not ArduinoJson.
* "No `setInsecure()`" and "CA is installed" are proven; "the handshake really rejects a bad certificate or wrong host name" is **not** – that is done by mbedTLS on the device.
* Timing, memory/heap, flash wear, brown-outs and electrical behaviour are untested.

## 5. Commands to run on your machine
```
# 0. everything that ran here (needs g++ and python3 only)
tests/firmware/run_tests.sh && tests/firmware/syntax_check.sh && sh tests/policy_checks.sh \
  && python3 tools/check_protocol_consistency.py && python3 tools/static_check_dart.py

# 1. the app
flutter pub get
dart format .                 # the Dart code was hand-formatted; this will touch files
flutter analyze               # fix what it reports (info-level hints are not fatal in CI)
flutter test
flutter build apk --release   # needs android/key.properties, see docs/ANDROID_SIGNING.md

# 2. the firmware (once per chip: esp32, esp32s3, esp32c3), partition scheme with a file system
arduino-cli core install esp32:esp32
arduino-cli lib install "ArduinoJson@7" "PubSubClient@2.8" "DHT sensor library" "Adafruit Unified Sensor"
arduino-cli compile --fqbn esp32:esp32:esp32 esp32_firmware/esp32_smart_home   # note the reported sketch size
```

## 6. Scenario matrix from the brief

| Scenario | Evidence that exists | Gap |
|---|---|---|
| Internet unavailable | cloud gating/backoff/recovery and "relays and rules do not depend on the cloud" run on the host; Dart reconnect tests written | real network |
| Router unavailable | relays/rules are independent of `net.*` (host); Wi-Fi state machine syntax-checked | **hardware needed** |
| MQTT broker unavailable | connect failure, `bad_credentials`, `not_authorized`, backoff cap, lost session, recovery – host; Dart written | real broker |
| ESP32 reboot | save/reload, backup fallback, boot policies, lock starts locked, OFF-before-OUTPUT order – host | real reset behaviour (pins float until firmware starts: HARDWARE.md) |
| App restart | settings load/scrub tests, secure-storage round trip – written | not run |
| Invalid configuration | strict replace, load-time repair, schema (17), HTTP validation – host | – |
| Unauthorized commands | REST: 401/403/429, claim lock-out, routing, redaction; MQTT: allow-list, `remote` flag, hardware fields ignored, malformed/oversized/wrong-topic/duplicate/flood – all on the real handlers | broker-side ACL, real sockets |
| Expired / invalid credentials | firmware status + backoff (host); app `test()` messages (written) | real broker |
| OTA update failure | **not applicable – OTA is not implemented in this phase** | – |
| Sensor disconnection | NaN readings are ignored by the temperature trigger; no `temp` key without a reading (host) | real DHT |
| Several users, conflicting commands | idempotent absolute commands, id de-dup, 10 s manual hold – individually tested | no concurrent test |
| Cloud sync conflicts | room merge converges under reordering on **both** sides with identical rules (firmware 3 delivery orders; Dart rotations + reverse); lagging-board repair + once-a-minute throttle (Dart, written) | no multi-board field test |

## 7. Phase-1 audit findings – status

| Finding | Status |
|---|---|
| No auth by default; any LAN client could switch relays/unlock/wipe Wi-Fi | **Fixed** – claim + Bearer token (hash only on board), constant-time compare, lock-outs |
| Fixed hotspot password `12345678` | **Fixed** – per-device random claim code, hotspot only while provisioning/recovery |
| Wi-Fi password/API key in URL query; GET could change state | **Fixed** – POST + JSON body only; local link is still plain HTTP on the LAN (documented) |
| One shared key, no identity | **Partly** – still one token per board (rotatable); no per-user accounts |
| Secrets at rest | App **fixed** (secure storage, no backup, legacy key scrubbed). Board **not fixed** – plaintext on LittleFS unless you enable flash encryption (documented, not enabled) |
| Release signed with debug key, cleartext everywhere, CI ignored failures | **Fixed in config** (not build-verified); format check is advisory, analyze/test block |
| Camera URL from the firmware loaded into a JS WebView | **Fixed** – the URL is an app setting you choose |
| Door unlock without server-side protection | **Partly** – configuration can never unlock; the cloud can unlock only if `remote:true`; a direct authenticated local unlock is still allowed (the app asks first, the board does not) |
| Only one board at a time; rooms only in the app | **Fixed** (multi-board through MQTT; without cloud only the active board is shown) |
| Same mDNS name on every board | **Fixed** – `sh-<6 hex>.local` |
| Relay state lost / always off after reboot | **Fixed** – per-device boot policy (never on for heater/siren/lock) |
| Scenes hard-coded; NVS string limit | **Fixed** – scenes are data; LittleFS JSON |
| Energy only estimated; no sensor support | **Unchanged** – still estimated and labelled; PZEM/ACS712 not implemented |
| Optimistic rollback assumed `!on` | **Fixed** – restores the real previous value (Dart; not run) |
| `TextEditingController` leaks in two pre-existing dialogs | **Not fixed** (negligible) |
| Android 12+ splash, startup measurement | **Not done** (not in the Phase 2 brief) |
| OTA, BLE provisioning, Telegram, voice/AI | **Not in this phase** |
