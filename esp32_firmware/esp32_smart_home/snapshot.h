/*
  snapshot.h - builds the inventory/state JSON documents.

  The SAME builders feed the retained MQTT topics (.../inventory, .../state) and the local REST
  endpoint GET /api/v2/state, so the app has a single parser for both transports.
  Format: docs/PROTOCOL.md
*/
#pragma once

#include <ArduinoJson.h>

#include "runtime.h"

// detailed=true (local REST only) adds GPIO numbers, polarity, boot policy and the pin lists the
// device editor needs. The MQTT copy leaves hardware details out.
void snapshotInventory(JsonObject o, bool detailed);
void snapshotState(JsonObject o);
