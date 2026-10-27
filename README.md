# Smart Home – ESP32 controller + Flutter app (Phase 2, version 4.0.0)

Several ESP32 boards (one per room) controlled from one Android app, **locally** (no internet needed)
and **from anywhere** through an MQTT cloud broker (HiveMQ Cloud, EMQX Cloud or your own), with every
connection authenticated and every cloud connection encrypted and certificate-checked.

> **Read this first – verification status.** This release was produced in a sandbox with **no Flutter/Dart SDK,
> no Arduino toolchain, no ESP32 hardware and no network**. What was actually run, and what was not, is listed
> in [docs/TEST_REPORT.md](docs/TEST_REPORT.md). In short: the firmware logic **and its real HTTP and MQTT handlers** were
> run on a PC against hand-written stand-ins for the Arduino APIs (1,157 checks pass, 17 deliberate regressions all caught);
> **nothing has been compiled with the real Arduino core or the real Flutter SDK, and nothing has run on a board or
> against a broker.** The Dart code and its tests have never been compiled. Treat the first build on your machine as
> the first real compile, and use the commands in the test report to check it.

## What is in this repository

| Path | What |
|---|---|
| `esp32_firmware/esp32_smart_home/` | Firmware 4.0.0 (Arduino C++): ESP32 / ESP32-S3 / ESP32-C3 |
| `esp32_firmware/config/` | `config.schema.json`, `config.example.json` |
| `esp32_firmware/certs/`, `tools/` | Pinned public root CAs and the generator that embeds them |
| `esp32_firmware/legacy/` | The v3.0.0 sketch, for rollback/reference only (**unauthenticated – do not expose**) |
| `lib/` | Flutter app 4.0.0 (provider, mqtt_client, flutter_secure_storage) |
| `test/`, `tests/firmware/` | Dart tests, host tests of the firmware logic |
| `docs/` | Architecture, protocol, security, hardware, cloud setup, migration, signing, test report |

## Quick start

1. **Firmware** – install the Arduino libraries (ArduinoJson 7.x, PubSubClient 2.8, DHT sensor library +
   Adafruit Unified Sensor if you use a DHT22) and the `esp32` board package 3.x, pick a partition scheme with a
   file system (e.g. *Minimal SPIFFS (1.9 MB APP with OTA)*), open `esp32_firmware/esp32_smart_home`, upload.
   Details and pin tables: [docs/HARDWARE.md](docs/HARDWARE.md).
2. **Claim the board** – open the Serial Monitor (115200 baud). A board with no owner prints a one-time
   **claim code**. In the app: *Settings → Boards → Add board*, join the board's hotspot `SmartHome-xxxxxx`
   (its password is the claim code), keep address `192.168.4.1`, enter the code. Then *Settings → Wi-Fi* to join
   your network. There is **no default or shared password anywhere**.
3. **Cloud (optional)** – create a broker and users as described in [docs/CLOUD_SETUP.md](docs/CLOUD_SETUP.md);
   enter the app's login in *Settings → Cloud*; send each board *its own* login with *Boards → ⋮ → Send cloud
   login*; set each board's time zone (*Boards → ⋮ → Name & time zone*).
4. **App** – `flutter pub get && flutter analyze && flutter test && flutter build apk --release`
   (release signing: [docs/ANDROID_SIGNING.md](docs/ANDROID_SIGNING.md)).

### Using a second phone (same boards, same token)
On the first phone: *Settings → Boards → ⋮ → **Show / copy token*** (copy it to a safe place, e.g. a password manager or
a message to yourself). On the second phone: *Settings → Boards → **Add board*** → enter the board's IP address and the
token, leave *Claim code* empty. The app first checks the token against the board and saves it only if it works.
To change it: *⋮ → **Change token*** – you can type your own (32–128 characters `A–Z a–z 0–9 - _`) or use the suggested random one;
the old token stops working on **every** phone immediately, and the new one is shown for copying. For the cloud, enter the
broker login in *Settings → Cloud* on the second phone as well (a separate broker login per phone is better than sharing one).

### Start-up speed
The dashboard now shows the last known state of your home on the first frame (marked "offline" until a board or the broker
really answers), starts the secure-storage read and the broker connection in parallel, and gives up on an unreachable
board after about 3 s instead of 5–6 s. **Install a *release* build**: a debug APK is much slower to start. (`flutter run`
is a debug build; use `flutter run --profile` or `flutter build apk --release` when judging speed. CI builds a release APK too.)

Upgrading from 3.x? [docs/MIGRATION.md](docs/MIGRATION.md) lists every behaviour change.

## Headline design decisions

* **Local first.** Relays, schedules and automations run on the board with no Wi-Fi, router, internet or broker.
  The local REST API keeps working when the cloud is down. See the failure matrix in
  [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).
* **The cloud can switch devices, not reconfigure a board.** Over MQTT a board accepts only an allow-list of
  commands (switch, run routine, do-not-disturb, room names, device name/icon/room). GPIO pins, Wi-Fi, broker
  credentials, rules and the access token can only be changed over the authenticated *local* connection.
* **Doors are never opened by configuration.** Scenes, schedules and automations can lock but never unlock; a
  lock can be unlocked from the cloud only if you explicitly allow it per device.
* **Energy is an estimate.** Rated watts × on-time. The boards have no current sensor and the app labels the
  numbers "Estimated" (energy sensors such as PZEM-004T were out of scope for this phase).

## Known limitations (short list; the full list is in the test report and security notes)

* Not compiled with the real toolchains and not hardware-tested (see above).
* The local link is plain HTTP on your Wi-Fi (Bearer token in a header, never in the URL). Do not forward the
  board's port 80 to the internet; use the cloud connection away from home. Local TLS is a possible next step.
* No OTA, BLE provisioning, Telegram, voice-assistant or AI integration in this phase (they were not part of the
  Phase 2 brief). Updating firmware needs USB.
* Secrets on the board are in the LittleFS file system **unencrypted** unless you enable ESP32 flash encryption
  (irreversible; see [docs/SECURITY.md](docs/SECURITY.md)).
* HiveMQ Cloud's free Serverless plan can only scope a login to *publish / subscribe / both*, not to topics –
  see [docs/CLOUD_SETUP.md](docs/CLOUD_SETUP.md) for what that means and how the firmware compensates.
