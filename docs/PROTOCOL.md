# Protocol reference (firmware 4.0.0 ↔ app 4.0.0)

Everything below is implemented in `esp32_firmware/esp32_smart_home/{cloud,api,snapshot}.cpp` and
`lib/{mqtt_models,mqtt_service,esp32_api}.dart`. Both sides ignore documents whose `"v"` is not `1`.

## 1. MQTT

Broker: TLS on port **8883** (presets), MQTT 3.1.1. Board client id = board id (`sh-<6 hex>`, derived from the
chip's factory MAC); app client id = `app-session-<10 hex>`.

| Topic | Direction | QoS / retain | Content |
|---|---|---|---|
| `sh/v1/<home>/<board>/cmd` | app → board | 1 / no | one command |
| `sh/v1/<home>/<board>/ack` | board → app | 0 / no | result of a command |
| `sh/v1/<home>/<board>/state` | board → app | 0 / **retained** | live values |
| `sh/v1/<home>/<board>/inventory` | board → app | 0 / **retained** | rooms, devices, scenes |
| `sh/v1/<home>/<board>/lwt` | board → app | will: 1, `online`: 0 / **retained** | `online` / `offline` (plain text) |

* `<home>` is `[a-z0-9_-]{1,32}` (never `/ + #`), the same on every board and in the app.
* The board registers `offline` as its **Last Will** on `lwt` and publishes `online` after connecting; before a
  clean disconnect it publishes `offline` itself (a clean DISCONNECT suppresses the will).
* The board publishes at QoS 0 because the PubSubClient library cannot publish at QoS 1; the app compensates with
  its own acknowledgements (below).
* `state` is published on every change (at most every 250 ms) and as a 60 s heartbeat. The app treats a board as
  *available* only if its last-will is not `offline` **and** its last state is ≤ 150 s old.

### inventory
```json
{"v":1,"board":"sh-aabbcc","name":"Living area board","fw":"4.0.0","chip":"esp32","psram":false,
 "cfgRev":12,"maxDevices":24,
 "rooms":[{"id":"living","name":"Living room","rev":3},{"id":"old","name":"Old","rev":5,"deleted":true}],
 "devices":[{"id":"light1","name":"Main light","type":"switch","group":"Lighting","icon":"light",
             "room":"living","watts":12,"enabled":true,"remote":true},
            {"id":"fan1","name":"Fan","type":"switch","group":"Climate","icon":"fan","room":"living",
             "watts":60,"enabled":true,"remote":true,"fault":"flash"}],
 "scenes":[{"id":"alloff","name":"All off"}],"counts":{"schedules":2,"automations":2}}
```
`fault` appears when the device's GPIO is unusable on this chip/module (`flash`, `psram`, `strapping`,
`pin_duplicate`, …); such a device is never driven. GPIO numbers are **not** sent over MQTT.

### state
```json
{"v":1,"seq":41,"ts":1767225600,"up":3600,"rssi":-58,"ip":"192.168.1.37","dnd":false,
 "temp":24.5,"hum":41.0,"powerW":72.0,"energy":"estimated","cloud":"connected",
 "auto":{"fired":3,"suppressed":0,"last":"hallmotion"},
 "d":{"light1":{"on":true,"wh":1.25},"door":{"on":true},"pir1":{"on":false}}}
```
`powerW` and `wh` are **estimates** (rated watts × on-time); `energy:"estimated"` says so. For a lock `on` means
*locked*; for a PIR it means *motion*. `ts` is 0 while the board's clock is not set.

### cmd
```json
{"v":1,"id":"k3J9xQ_abcd0","op":"set","dev":"light1","on":true}
```
`id`: 1–24 characters `[A-Za-z0-9_-]`, chosen by the app. Allowed `op`s (anything else → `unknown_op`):

| op | fields | notes |
|---|---|---|
| `set` | `dev`, `on` | refused if the device has `remote:false` (exception: **locking** is always allowed; unlocking needs `remote:true`), is a sensor, disabled, on an unusable pin, or is the siren while DND is on |
| `scene` | `scene` | applies only to devices with `remote:true`; scenes can lock but never unlock |
| `dnd` | `on` | silences a running siren |
| `room.upsert` | `room:{id,name,rev}` | needs `mqtt.remoteConfig` (default true) |
| `room.delete` | `room:{id,rev}` | tombstone; the last room can not be deleted |
| `device.update` | `dev` and any of `name`,`room`,`icon` | needs `remoteConfig`; icon must keep the device type; **no pin, watts, polarity, boot policy** |
| `inventory.get` | – | republish inventory and state |
| `ping` | – | |

Pins, Wi-Fi, broker credentials, token, schedules/automations/scenes and the config file can **not** be changed
over MQTT.

Board-side checks, in order: topic is exactly this board's `cmd` topic → payload ≤ 600 bytes and valid JSON →
valid `id` → **duplicate id?** (answer again with `dup:true`, never run twice; 16 ids remembered) → rate limit
(20 commands / 10 s, else `rate_limited`) → `v == 1` → allow-listed op → per-op validation → execute.

### ack
```json
{"v":1,"id":"k3J9xQ_abcd0","ok":false,"err":"dnd","cfgRev":12}
```
`dup:true` marks the answer to a repeated id. The app waits 5 s, **retries once with the same id**, then reports
`timeout`. Error codes: `unknown_device`, `disabled`, `pin_fault`, `read_only`, `dnd`, `remote_not_allowed`,
`remote_config_disabled`, `rate_limited`, `bad_version`, `bad_on`, `bad_room`, `unknown_op`, `unknown_scene`,
`save_failed`, `failed_earlier`, plus the validation codes of the room/device operations.

### Room conflict rule (identical in `config_store.cpp` and `mqtt_models.dart`)
For the same room id: higher `rev` wins; at equal `rev` a tombstone (`deleted`) beats a live room; otherwise the
byte-wise (UTF-8) greater name wins. Verified by tests on both sides.

## 2. Local REST API v2 (port 80, JSON)

* `Authorization: Bearer <token>` on everything except `GET /api/v2/info` and `POST /api/v2/claim`.
  The board stores only `SHA-256(token)`; comparison is constant-time. Tokens: 32–128 chars `[A-Za-z0-9_-]`
  (the app generates 43 characters from `Random.secure()`).
* Reads are `GET`; **every change is `POST` with a JSON body**. Nothing is read from the URL query.
* No CORS headers, no `OPTIONS` handler. 5 failed logins per minute → 60 s lock-out (`429`); 5 wrong claim codes
  per 5 minutes → 5 minute lock-out. The old v3 routes answer `410`.
* Errors: `{"ok":false,"error":"<code>"}` with `400` (invalid), `401` (token), `403` (`not_claimed`/`bad_code`),
  `404`, `409` (conflict: `dnd`, `device_in_use`, `last_room`, `too_many_*`, …), `413`, `429`, `500` (`save_failed`).

| Method + path | Body | Result |
|---|---|---|
| `GET /api/v2/info` | – | `{board, fw, chip, claimed}` – no secrets |
| `POST /api/v2/claim` | `{code, token}` | stores `SHA-256(token)`; only while unclaimed; needs the serial-console claim code |
| `GET /api/v2/state` | – | `{inventory (detailed: pins, polarity, …), state, wifi, mqtt (no secrets), time}` |
| `GET /api/v2/config` | – | full config, **redacted** (no passwords, token hash, claim code) |
| `POST /api/v2/set` | `{id,on}` | |
| `POST /api/v2/scene` · `/dnd` | `{scene}` · `{on}` | |
| `POST /api/v2/device/add·update·delete` | device JSON / `{id}` | strict validation incl. GPIO policy for **this** chip |
| `POST /api/v2/room/upsert·delete` | `{id,name,rev?}` / `{id,rev?}` | `rev` defaults to current+1 |
| `POST /api/v2/scene/upsert·delete`, `/schedule/…`, `/automation/…` | see schema | |
| `POST /api/v2/config/board` | `{name?,tz?,psram?,relayActiveLow?,profile?,resetPin?}` | |
| `POST /api/v2/config/mqtt` | `{enabled?,provider?,host?,port?,username?,password?,homeId?,remoteConfig?}` | applied live |
| `POST /api/v2/config/ca` · `/ca/remove` | PEM text | extra trusted root for the broker |
| `POST /api/v2/config/replace` | full config (schema) | all-or-nothing; secrets and `auth` are preserved |
| `POST /api/v2/auth/rotate` | `{token}` | the old token stops working immediately |
| `GET /api/v2/wifi/status`·`/wifi/scan`, `POST /wifi/scan`·`/connect`·`/reset` | `{ssid,password}` | `connect` rolls back to the old network after 25 s if the new one fails; `reset` returns the recovery hotspot code to the authenticated caller only |
| `POST /api/v2/factory-reset` | `{"confirm":"RESET"}` | wipes everything the board stores |

## 3. Configuration file

`esp32_firmware/config/config.schema.json` (JSON Schema 2020-12) describes `/config.json`;
`config.example.json` is a valid example (pins fit a classic ESP32 incl. WROVER; adapt them for S3/C3 using the
tables in HARDWARE.md). The schema checks shape; the board additionally enforces what a schema can not express:
GPIO safety per chip, duplicate pins, automation loops, and the never-unlock rule. `python3
esp32_firmware/tools/validate_config.py my-config.json` checks a file offline, and `POST /config/replace` applies it.
