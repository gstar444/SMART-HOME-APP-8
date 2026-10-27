/*
  net.h - Wi-Fi, setup hotspot, mDNS, NTP and the physical credential-reset button.

  Provisioning model (no shared hotspot password exists anywhere):
    * On first boot - or after a credential reset - the board creates a random CLAIM CODE
      (XXXXX-XXXXX, 50 bits) from the hardware RNG and prints it on the serial console.
    * The setup hotspot "SmartHome-<id>" uses that code as its WPA2 password, and
      POST /api/v2/claim only accepts a request that presents the same code.
      Physical access (USB serial) is therefore the trust anchor for taking ownership.
    * Claiming stores the hash of an app-generated bearer token. The claim code stays valid
      only until the board has joined Wi-Fi (so a reboot mid-setup still works), then it is
      deleted and the hotspot is switched off for good.
    * Lost Wi-Fi/credentials later: hold the BOOT button for 8 s (credential reset).
*/
#pragma once

#include <ArduinoJson.h>

#include "runtime.h"

void netBegin();
void netTick(uint32_t now);
bool netStaUp();
bool netApActive();
String netIp();
const char* netMode();                         // "station" | "setup" | "offline"

void netEnsureClaimCode();                     // creates the code if the board is unclaimed
bool netRequestConnect(const char* ssid, const char* pass, const char** err);
void netWifiStatusToJson(JsonObject o);
void netScanRequest();
void netScanToJson(JsonObject o);
const char* netApSsid();                       // name of the setup hotspot
void netForgetWifi();                          // delete the saved network, issue a recovery code, reboot into setup mode
void netCredentialReset();                     // wipe Wi-Fi/MQTT secrets and the token, new claim code, reboot
void netRestartSoon(uint32_t ms);
void netApplyTimezone();                       // call after cfg.board.tz changed
