/*
  cloud.h - MQTT over TLS (port 8883) to HiveMQ Cloud, EMQX Cloud or a self-hosted broker.

  Security properties
    * The broker certificate is ALWAYS verified against a pinned root CA: the built-in ISRG Root X1
      (hivemq), DigiCert Global Root G2 (emqx) or a PEM you upload (custom). There is no
      "insecure" mode in this code base, and a board with no CA available refuses to connect.
    * Per-board credentials (username/password) are created in the broker console, sent once to
      the board over the authenticated local API, and never leave it again (the REST API redacts them).
    * Every command is validated on the board: schema, allow-listed operation, device exists,
      per-device "remote" permission, duplicate id, rate limit. The broker is never trusted to
      authorise anything on its own.

  Topics (docs/PROTOCOL.md):   sh/v1/<home>/<board>/{cmd,ack,state,inventory,lwt}
*/
#pragma once

#include "runtime.h"

void cloudBegin();
void cloudTick(uint32_t now);
void cloudReconfigure();         // MQTT settings or CA changed: reconnect with the new values
void cloudInventoryChanged();    // rooms/devices/names changed: republish the retained inventory
bool cloudConnected();
// "disabled" | "waiting_wifi" | "waiting_time" | "no_ca" | "connecting" | "connected" | "error:<reason>"
const char* cloudStatus();
