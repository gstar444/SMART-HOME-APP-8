# Architecture

```
                    ┌────────────────────── cloud broker (HiveMQ / EMQX / own) ─────────────────────┐
                    │  sh/v1/<home>/<board>/{cmd, ack, state, inventory, lwt}      TLS 8883          │
                    └───────▲───────────────────────────────▲───────────────────────────▲────────────┘
                            │ verified TLS                  │ verified TLS              │
   ┌────────────┐   ┌───────┴────────┐              ┌───────┴────────┐          ┌───────┴────────┐
   │ Flutter app│   │ board A (ESP32)│              │ board B (S3)   │          │ board C (C3)   │
   │ HomeCtrl   │◄──┤ LittleFS config│              │  …             │          │  …             │
   │ MqttService│   │ relays/PIR/DHT │              └────────────────┘          └────────────────┘
   │ Esp32Ctrl ─┼───►  REST API v2   │  plain HTTP + Bearer token on the local Wi-Fi (active board)
   └────────────┘   │ schedules/rules│
                    └────────────────┘
```

## Components

**Firmware (`esp32_firmware/esp32_smart_home`)** – one sketch, chip detected at compile time.

| Module | Responsibility | Verified on a PC? |
|---|---|---|
| `board_profile.h` | Which GPIOs are safe on ESP32 / S3 / C3, with or without PSRAM | unit tests |
| `core_logic.h`, `sha256.h` | ids, topics, backoff, claim code, rate limiter, de-duplication, schedule/window/selector logic, SHA-256 | unit tests |
| `config_model.h` | Config structs, strict validation, load-time repair, v3 migration | unit tests |
| `config_store.*` | `/config.json` on LittleFS (ArduinoJson 7), atomic save, backup fallback, JSON mapping, rooms | run against in-memory flash with a stand-in JSON library |
| `devices.*` | Safe relay init, commands, scenes, PIR, DHT, safety auto-off, *estimated* energy | run against stubs |
| `automation.*` | Schedules and automations (cooldown, loop limit, conflict rule) | run against stubs |
| `net.*`, `api.*`, `cloud.*`, `snapshot.*`, `.ino` | Wi-Fi/hotspot/NTP, REST API, MQTT, JSON snapshots | **syntax-checked only** |

**App (`lib/`)**

| File | Responsibility |
|---|---|
| `secure_storage.dart` | Broker login and per-board tokens in Android Keystore-backed storage; validation of cloud settings |
| `mqtt_models.dart` | Typed protocol models, topic helper, deterministic room merge |
| `mqtt_service.dart` / `mqtt_transport.dart` | Session, reconnect/backoff, command + ack + one safe retry / the real TLS client |
| `esp32_api.dart` | `BoardHttp` (local REST client) and `Esp32Controller` (polls the active board) |
| `home_controller.dart` | One merged view of all boards and rooms; routes each command local-first, else cloud |
| `dashboard_page.dart`, `settings_page.dart`, `wifi_page.dart` | The existing screens, adapted (themes, customisation and controls preserved) |
| `boards_page.dart`, `cloud_settings_page.dart` | Claim a board, tokens, per-board broker login; app broker login and connection test |

## What runs where

| Function | Runs on | Needs |
|---|---|---|
| Relay switching, boot-state policy, safety auto-off | board | power |
| Schedules, automations, scenes | board | power; schedules also a valid clock (NTP) |
| Local control from the app | board REST API ↔ app | same Wi-Fi |
| Remote control, multi-board dashboard | board ↔ broker ↔ app | internet, broker, valid TLS, correct clock on the board |
| Room/device-name sync | boards + app (replicated, merged by revision) | broker (or local link per board) |
| Estimated energy | board (watts × time) | nothing |

## Failure behaviour (what actually happens)

| Situation | Relays / rules on the board | App |
|---|---|---|
| **Internet down**, router up | Unaffected. MQTT attempts fail and back off 5 s → 5 min (+20 % jitter). | Cloud icon shows trouble; the active board still works over the local link; other boards show as unavailable. |
| **Router / Wi-Fi down** | Unaffected: relays hold state, schedules/automations keep running if the clock was set before the outage (the clock is lost on a reboot without internet; then time-based rules wait). Board retries Wi-Fi every 10 s. **No local control** until Wi-Fi returns, because the setup hotspot is only offered during provisioning/recovery (hold BOOT 8 s). | Boards unavailable. |
| **Broker down / auth failure / TLS failure** | Unaffected. Status shows `error:…` (e.g. `bad_credentials`, `tls_or_network`). | Cloud icon; local link still works. |
| **Board reboot** | Pins are set to a known-safe level before they are driven (see below), then per-device boot policy: off (default), on (never for heater/siren/lock), or last (debounced to flash). Locks start **locked**. Configuration is reloaded from flash. | Board reappears when it republishes its retained state. |
| **App restart** | n/a | Credentials come from secure storage; the cloud session starts after the first frame; retained MQTT messages repopulate the dashboard immediately. |
| **Corrupt `/config.json`** | Backup `/config.bak` is used automatically; if both are bad the board starts with an empty default configuration. A file written by a *newer* firmware is never overwritten (read-only safe mode). | Board shows no devices until fixed. |
| **Power cut** | Nothing can be promised without power. State "last" is written at most every 30 s, so up to 30 s of changes can be lost. | |

## Safe relay initialisation – and its limit

For every output the firmware first latches the *off* level into the pin's output register and only then switches the
pin to output, so the relay never sees a glitch **from this firmware**. Software cannot act before it starts:
some ESP32 pins pulse at boot (GPIO5, 14 are never offered for new devices) and any pin floats while the chip
resets. Use relay modules whose inputs have pull-ups/pull-downs, or add resistors (see HARDWARE.md).

## Configuration and sync

`/config.json` (schema version 2, `esp32_firmware/config/config.schema.json`) holds Wi-Fi, MQTT, rooms, devices,
scenes, schedules and automations. Saves are atomic (temp file → re-parsed and verified → old file kept as `.bak` →
rename). Every save increments `revision`.

*Rooms* are replicated on every board and merged deterministically by `(rev, tombstone, name)` (rules identical in
firmware and app, tested on both sides). An app edit goes to every reachable board; a board that missed it is
brought up to date by the app within a minute of being seen (once per minute per board at most).

*Conflicting commands* (two phones at once): commands are absolute states (`on`/`off`), so the last one wins and
repeating one is harmless; a command id is executed at most once per board (16-entry memory), so a retry or broker
re-delivery never runs twice. Rules never override a device a person switched by hand within the last 10 s.

*Automation safety*: cooldown ≥ 5 s per rule; at most 6 firings per minute across all rules (stops A→B→A loops);
a rule may not act on the device that triggers it; inside one rule run the first action on a device wins;
nothing configured can unlock a lock.
