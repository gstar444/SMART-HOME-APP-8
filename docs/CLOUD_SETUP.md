# Cloud provisioning (HiveMQ Cloud / EMQX Cloud / own broker)

You need: one broker, **one login for the app**, and **one separate login per board**. Nothing secret is stored in
this repository; credentials are typed into the app and sent to each board over the authenticated local link.

## 0. Pick the names

* **home id** – a short topic namespace, `[a-z0-9_-]`, e.g. `myhome`. The same value goes into the app
  (*Settings → Cloud → Home id*) and into every board (done for you by *Boards → ⋮ → Send cloud login*).
* **board ids** are printed in the Serial Monitor and shown in the app (`sh-1a2b3c`). Topics:
  `sh/v1/<home>/<board>/{cmd,ack,state,inventory,lwt}` (see PROTOCOL.md).

## 1. HiveMQ Cloud

1. Create a cluster. Note its host, e.g. `abc123.s1.eu.hivemq.cloud`, TLS port **8883**.
2. *Access Management → Credentials*: create `app-<yourname>` and one `board-<boardid>` per board.
3. **Permissions.** The app needs publish on `…/+/cmd` and subscribe on `…/+/{state,inventory,lwt,ack}`.
   A board needs subscribe on `…/<board>/cmd` and publish on `…/<board>/{ack,state,inventory,lwt}`.
   * On the free **Serverless** plan the available permission levels are *publish only / subscribe only /
     publish and subscribe* applied to **all topics** (`#`) – no per-topic scoping (HiveMQ's documentation at the time
     of writing; check your console). Consequence: any credential can read and publish everywhere in that cluster.
     The firmware does not rely on the broker for authorisation: boards run only allow-listed commands, honour
     the per-device `remote` flag and never accept hardware/credential changes over MQTT. Still use a separate
     login per board so one stolen login can be revoked alone, and keep `remoteConfig` off where you do not need
     cloud renaming. Per-topic permissions are offered on paid plans – use the topic tables above there.
4. Root CA: HiveMQ Cloud presents a Let's Encrypt chain; the firmware embeds **ISRG Root X1** (provider `hivemq`).
   The app uses the Android trust store, which contains ISRG Root X1 from **Android 7.1.1**. The app's minimum is
   Android 7.0 (API 24): on a 7.0 phone the TLS check will (correctly) fail – paste
   `esp32_firmware/certs/isrg_root_x1.pem` into *Cloud → Own broker → extra CA* to add that root.

## 2. EMQX Cloud

1. Create a deployment, note the host and TLS port 8883.
2. *Authentication*: add users (`app-…`, `board-…`).
3. *Authorization*: add ACL rules per user using the topic tables above. EMQX topic placeholders `${clientid}` and
   `${username}` are accepted for a *whole topic level*; since a board's client id **is** its board id you can write
   one rule shape for boards: allow publish `sh/v1/<home>/${clientid}/ack|state|inventory|lwt`, allow subscribe
   `sh/v1/<home>/${clientid}/cmd`, deny everything else.
4. Root CA: the firmware embeds **DigiCert Global Root G2** for provider `emqx` (the root used by EMQX's public
   broker). **For your own Serverless/Dedicated deployment, download the CA certificate from the deployment overview
   page and compare it** – if it differs, upload it (*Cloud → provider "Own broker" → extra CA* in the app, and
   `POST /api/v2/config/ca` / *Send cloud login* with provider `custom` for the board). An uploaded PEM **overrides**
   the built-in root; it never disables verification.

## 3. Own broker (Mosquitto, EMQX, …)

Provider *Own broker*: TLS on any port, a certificate that verifies (public CA, or your private CA uploaded as above),
host **name** (not an IP address – the certificate must match it). Plain MQTT is not available in either the app or
the firmware.

## 4. Enter the settings

1. App: *Settings → Cloud*: provider, host, username/password of the **app** login, home id → *Test connection* → *Save & connect*.
   *Test* opens a throw-away session and reports: unreachable host / certificate not verified / bad login / not authorised.
2. For each board (reach it locally first): *Settings → Boards → ⋮ → Send cloud login* with **that board's** login.
   The password is sent over the local link and can never be read back (the API reports only `hasPassword`).
3. *Boards → ⋮ → Name & time zone*: set the POSIX time zone (`IST-5:30`, `CET-1CEST,M3.5.0,M10.5.0/3`, …).
   Schedules and time windows depend on it, and **TLS needs the board's clock**: a board connects to the broker only
   after SNTP has set the time (status `waiting_time` until then).
4. The Serial Monitor and `GET /api/v2/state` show the cloud status: `connected`, `waiting_wifi`, `waiting_time`,
   `no_ca`, `connecting`, or `error:<reason>` with `bad_credentials`, `not_authorized`, `tls_or_network`, …

## 5. Checking it from a PC

```
mosquitto_sub -h abc123.s1.eu.hivemq.cloud -p 8883 -u app-me -P '<password>' --capath /etc/ssl/certs \
              -t 'sh/v1/myhome/#' -v
```
You should see the retained `inventory`, `state` and `lwt` of every board immediately. Powering a board off must turn its
`lwt` into `offline` within the keep-alive time (≈45 s).

## 6. First-hardware-test checklist (security-relevant, none of this has been run by the author)

* [ ] Wrong credentials → board status `error:bad_credentials`; app *Test connection* says so.
* [ ] **Host mismatch is rejected:** point a board at a host whose certificate is for a *different* name → `error:tls_or_network`.
      (The host-name check is done by the ESP32 TLS stack when connecting by name; confirm it on your core version.)
* [ ] A board with the wrong time (unplug the router, reboot) stays `waiting_time` and does not connect.
* [ ] Power-off → `lwt` becomes `offline`; the app greys out that board's devices.
* [ ] Send `{"v":1,"id":"t1","op":"set","dev":"<id>","on":true}` twice with the same id → second answer has `"dup":true`, relay switched once.
* [ ] A device with `remote:false` can not be switched from the cloud; a lock can be locked but not unlocked.
* [ ] `{"op":"device.update","pin":5}` and `{"op":"wifi…"}` are rejected / ignored.
