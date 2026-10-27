# Migrating from 3.x (firmware 3.0.0 / app 3.0.0) to 4.0.0

Firmware 4.x migrates what it can on first boot; the rest are deliberate behaviour changes you should know about.

## What is migrated automatically (board)

On first boot with no `/config.json`, the firmware reads the v3 data from NVS (namespace `smarthome`: `devs`, `nextId`,
`ssid`, `pass`), writes `/config.json`, **verifies it by reading it back**, and only then erases the old NVS entries (so the
old plaintext Wi-Fi password disappears from NVS). If the save fails, the NVS data is left for the next boot.

| v3 data | In 4.0.0 |
|---|---|
| Device list (id, name, type, group, icon, pin, watts, auto-off, invert) | imported 1:1 |
| Room **number** per device | rooms `room1…roomN` ("Room 1"…) are created; rename them in the app (room names used to live only in the app and are not migrated) |
| Wi-Fi SSID/password | imported; the board joins it immediately |
| API key | **not trusted**: the board starts unclaimed (see below) |
| Pins GPIO5 / GPIO14 (relay may click at boot) | kept working and flagged `riskyPinOk` (grandfathered, warning in the log); never offered for new devices |
| Devices on a pin that is flash/PSRAM/strapping/USB/UART on your chip | imported but **never driven**; the dashboard shows them as faulty. Move them to a valid pin |
| Scenes alloff / goodnight / dinner | now ordinary editable data with the same behaviour (goodnight locks, never unlocks) |
| Heater 180 min / siren 5 min safety timers | kept as per-device `autoOffMin`, now editable |

The migration report is printed on the serial console: `[cfg] v3 migration: N device(s) imported, …`.

## Behaviour changes (intentional)

1. **No more unauthenticated API.** All `/api/*` v3 routes answer `410`. Every call needs a Bearer token; changes are POST + JSON.
2. **Claiming.** The first time, claim each board (README step 2). Over the LAN a migrated board can be claimed with its serial-console
   code even though it is already on Wi-Fi.
3. **Setup hotspot.** The fixed `12345678` hotspot is gone. The hotspot exists only while a board is unclaimed/being set up (password =
   claim code), for 15 minutes without clients. If a **claimed** board loses its Wi-Fi it does *not* open a hotspot by itself any more; hold BOOT 8 s
   (or use *Wi-Fi → forget* while it is still reachable) to recover.
4. **Relay boot state.** v3 always started everything off; that stays the default. New: per-device `bootState` `off|on|last` (never `on`/`last` for heater, siren, lock).
5. **Static IP option removed.** DHCP only; use a DHCP reservation in your router for a stable address (the app finds boards through MQTT anyway).
6. **Rooms** are stored on the boards and synchronised (not in the app). Room moves and renames now follow the device between phones.
7. **Several boards at once.** The dashboard merges all boards that are visible through the cloud, plus the active board locally. Without cloud, only the *active* board is shown (Settings → Boards → *Use for local control*).
8. **Hardware edits are local only** (add/delete device, change GPIO/watts); names, icons and rooms can also be changed through the cloud.
9. **Camera.** The firmware no longer hands the app a camera URL; set it in *Settings → Camera address* (a plain-http camera host may need an entry in `network_security_config.xml`).
10. **Removed:** the in-app view of the board's own web page and its zoom / JavaScript / auto-refresh / timeout / toolbar settings (boards no longer serve a UI), and the API-key field.
11. The app's saved plaintext API key (if any) is erased from shared_preferences on first start.
12. **Pins offered** are now per chip; for example on ESP32-C3 only GPIO 0, 1, 3, 5, 6, 7, 10 (GPIO4 additionally needs `HAS_DHT 0`).
13. Energy figures are unchanged (estimated) but now carry `energy:"estimated"` on the wire.
14. The Android release APK is signed with *your* keystore, and the build fails without one.

## Step by step

1. Flash firmware 4.0.0 to one board; watch the serial console for the migration report and the claim banner.
2. In the app 4.0.0: *Settings → Boards → Add board* (address = the board's current IP, or `192.168.4.1` on its hotspot), claim code from the console.
3. Fix any device the report marks as faulty; set the time zone; rename rooms.
4. Repeat for the other boards. Then set up the cloud (CLOUD_SETUP.md).
5. Keep the old sketch (`esp32_firmware/legacy/`) only for rollback: flashing it brings back the unauthenticated API, and it will read the v3 data from NVS only if it was not erased
   (it was, after a successful migration – restore your device list by hand).

## Rolling back

Re-flash `legacy/esp32_smart_home_v3`. Because the migration erased the NVS copy, the v3 firmware starts with its default device list and
no Wi-Fi (setup hotspot `12345678`). Note down your configuration (`GET /api/v2/config`) before upgrading if you may want to go back.
