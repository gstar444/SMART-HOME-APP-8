# Hardware, GPIO policy, wiring and flashing

## Supported chips and modules

| Chip | Typical boards | Notes |
|---|---|---|
| ESP32 | DevKit / WROOM-32 | classic dual-core |
| ESP32 + PSRAM | WROVER, WROVER-B/E | **GPIO16/17 are used by the PSRAM** and are never offered |
| ESP32-S3 | DevKitC-1 (N8, N16R8, …) | native USB on GPIO19/20; PSRAM variants use GPIO33-37 |
| ESP32-C3 | DevKitM-1, SuperMini | few usable pins; BOOT button is GPIO9 |

The chip is detected **at compile time** (selecting the right board in the Arduino IDE is enough; any other
chip fails the build on purpose instead of silently using the wrong pin table). PSRAM is a property of the
*module*, not of the chip, so it is detected at run time **and** can be forced:

* `psramFound()` is only true if PSRAM support is **enabled in the Tools menu** (*Tools → PSRAM → Enabled / OPI*).
  On a WROVER with PSRAM switched off the firmware would not know. **WROVER owners:** set
  `#define SH_ASSUME_PSRAM 1` at the top of `runtime.h` (or send `{"psram": true}` to `POST /api/v2/config/board`).

## GPIO policy (generated from `board_profile.h`)

A pin is offered for a relay only if it is a normal, boot-safe general-purpose pin on the chip. Excluded:
strapping pins, flash pins, native-USB pins, UART0 (serial console/flashing), PSRAM pins when the module has PSRAM,
pins that do not exist, and ESP32 GPIO5/14 which emit a signal during boot (a relay on them can click at power-up).
The firmware also validates **again on every boot**: a device whose pin is unusable on the chip it now runs on
(for example after moving the config to a WROVER) is shown as faulty and its pin is **never touched**.

```
esp32
  usable relay outputs (GPIO4 additionally reserved for the DHT sensor): 13 16 17 18 19 21 22 23 25 26 27 32 33
  input-only pins usable for PIR: 34 35 36 39
  excluded: 0(strapping) 1(uart0) 2(strapping) 3(uart0) 5(risky_boot_signal) 6(flash) 7(flash) 8(flash) 9(flash) 10(flash) 11(flash) 12(strapping) 14(risky_boot_signal) 15(strapping)

esp32 + PSRAM (WROVER / S3 R-variants)
  usable relay outputs (GPIO4 additionally reserved for the DHT sensor): 13 18 19 21 22 23 25 26 27 32 33
  input-only pins usable for PIR: 34 35 36 39
  excluded: 0(strapping) 1(uart0) 2(strapping) 3(uart0) 5(risky_boot_signal) 6(flash) 7(flash) 8(flash) 9(flash) 10(flash) 11(flash) 12(strapping) 14(risky_boot_signal) 15(strapping) 16(psram) 17(psram)

esp32-s3
  usable relay outputs (GPIO4 additionally reserved for the DHT sensor): 1 2 5 6 7 8 9 10 11 12 13 14 15 16 17 18 21 33 34 35 36 37 38 39 40 41 42 47 48
  input-only pins usable for PIR: none
  excluded: 0(strapping) 3(strapping) 19(usb) 20(usb) 26(flash) 27(flash) 28(flash) 29(flash) 30(flash) 31(flash) 32(flash) 43(uart0) 44(uart0) 45(strapping) 46(strapping)

esp32-s3 + PSRAM (WROVER / S3 R-variants)
  usable relay outputs (GPIO4 additionally reserved for the DHT sensor): 1 2 5 6 7 8 9 10 11 12 13 14 15 16 17 18 21 38 39 40 41 42 47 48
  input-only pins usable for PIR: none
  excluded: 0(strapping) 3(strapping) 19(usb) 20(usb) 26(flash) 27(flash) 28(flash) 29(flash) 30(flash) 31(flash) 32(flash) 33(psram) 34(psram) 35(psram) 36(psram) 37(psram) 43(uart0) 44(uart0) 45(strapping) 46(strapping)

esp32-c3
  usable relay outputs (GPIO4 additionally reserved for the DHT sensor): 0 1 3 5 6 7 10
  input-only pins usable for PIR: none
  excluded: 2(strapping) 8(strapping) 9(strapping) 11(flash) 12(flash) 13(flash) 14(flash) 15(flash) 16(flash) 17(flash) 18(usb) 19(usb) 20(uart0) 21(uart0)
```

**Verify against the datasheet of your exact module before wiring mains loads.** These tables are the author's
reading of Espressif's datasheets and are deliberately conservative, but module variants differ (for example
octal-flash S3 modules). The conservative cases are on purpose: if in doubt the pin is excluded. (Also note: the
lists above show GPIO4 as reserved because the DHT22 default pin is 4; set `HAS_DHT` to 0 in `runtime.h` to free it.)

## Relay wiring and what software can not do

* Most 1/2/4/8-channel relay boards are **active-LOW** (input LOW = relay on). That is the default
  (`board.relayActiveLow: true`). If your relays switch the wrong way, change it with `POST /api/v2/config/board`.
* The firmware writes the *off* level into the pin's output latch **before** it turns the pin into an output, so it
  never creates a glitch itself. But while the chip is resetting, and until the firmware starts (≈0.3–1 s), the pin
  is floating. Use a relay board with pull-ups on its inputs (most opto-isolated boards have them) or add
  4.7–10 kΩ resistors to the relay's idle level. A relay that must never click at power-up needs this hardware.
* Heater, siren and lock devices can not be configured to switch on at boot; locks start **locked**.
* Power the relay coils from a supply that can deliver their current (not from the ESP32's 3.3 V pin) and share ground.
* For door strikes decide deliberately whether the strike is *fail-secure* or *fail-safe*; `invert` only reverses
  the relay polarity. Never make a door's only way to open depend on this board or on the network.
* **Mains voltage is dangerous.** Wiring and enclosure must follow your local electrical code and be done by a
  qualified person where required. Nothing in this project is a safety device.

## DHT22 and energy

A DHT22 on GPIO4 (default) provides temperature/humidity (`HAS_DHT`/`DHT_PIN` in `runtime.h`). Power figures are
**estimates** (rated watts × time the device was on) because there is no current sensor in this phase; they are
labelled "Estimated" in the app and `energy:"estimated"` on the wire. Adding a PZEM-004T/ACS712 driver is a
future change.

## Buttons and recovery

| Action | Effect |
|---|---|
| Hold **BOOT** 8 s while running (GPIO0; GPIO9 on ESP32-C3) | *Credential reset*: erases Wi-Fi, broker login and access token, issues a new claim code, reboots. Rooms, devices, scenes and rules are kept. A button already held at power-up is ignored. |
| App: *Wi-Fi → forget* | Same Wi-Fi erase, but triggered by the owner; the new hotspot password is shown to the authenticated app once. |
| `POST /api/v2/factory-reset {"confirm":"RESET"}` | Erases everything the board stores. |
| Reflash with the Arduino IDE | Keeps the LittleFS contents unless you choose to erase flash. |

## Libraries, board package, partitions

| Item | Version targeted | Why |
|---|---|---|
| Board package `esp32` (Espressif) | 3.x | `psramFound`, `configTzTime`, LittleFS, mbedTLS |
| ArduinoJson | 7.x | `JsonDocument`, `.to<>()`, `.add<>()` (v6 will not compile) |
| PubSubClient | 2.8 | MQTT client over `WiFiClientSecure` |
| DHT sensor library + Adafruit Unified Sensor | current | only while `HAS_DHT` is 1 |

Partition scheme: **must contain a SPIFFS/LittleFS partition** (config lives there) and an app partition large
enough for TLS + WebServer + ArduinoJson (roughly 1.2–1.4 MB – the real size has not been measured, see the test
report). *Default 4MB with spiffs* has 1.25 MB of app space and may be too small; use *Minimal SPIFFS (1.9MB APP with
OTA)* or a custom table. *Huge APP* has no file system and will not work.

## First boot (serial monitor, 115200 baud)

```
Smart Home firmware 4.0.0 | chip esp32 | board sh-1a2b3c
[cfg] source: fresh_defaults | revision 1 | 1 room(s), 0 device(s), 0 schedule(s), 0 automation(s)
[net] setup hotspot "SmartHome-1A2B3C" ON (http://192.168.4.1/)
==================================================
 Board        : sh-1a2b3c
 Setup hotspot: SmartHome-1A2B3C
 CLAIM CODE   : 7K3QD-XM9PT
==================================================
```
This banner is the only place a secret is ever printed. The code is deleted once the board is claimed **and** has stayed
on Wi-Fi for 30 s. An unclaimed board closes the hotspot after 15 minutes without clients (reboot to reopen).
A board flashed over a v3 board that already had Wi-Fi configured joins that Wi-Fi at once and can be claimed over
the LAN with the same code (see MIGRATION.md).
