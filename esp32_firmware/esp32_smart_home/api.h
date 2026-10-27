/*
  api.h - local REST API v2 (port 80). Works with no internet and no MQTT broker.

  Rules
    * Every route except GET /api/v2/info and POST /api/v2/claim needs
        Authorization: Bearer <token>
      (the board stores only SHA-256(token), compared in constant time).
    * Reads are GET. EVERY change is POST with a JSON body. Nothing is read from the URL query,
      so no secret ever appears in a URL, and a link/image tag can not switch a relay.
    * No CORS headers and no OPTIONS handler: browsers can not call it cross-origin.
    * Failed logins are rate limited (5 per minute, then a 60 s lock-out).
    * The old unauthenticated v3 routes (/api/state, /api/set?...) answer 410 Gone.
  Limits: plain HTTP (the LAN is protected by WPA2/WPA3, not TLS - see docs/SECURITY.md) and the
  stock WebServer buffers a request body before our size check runs.
  Full route list: docs/PROTOCOL.md
*/
#pragma once

#include "runtime.h"

void apiBegin();
void apiTick();
