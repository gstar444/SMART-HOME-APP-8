#!/usr/bin/env bash
# Builds and runs the host-side firmware logic tests (needs g++ and python3 only).
set -euo pipefail
cd "$(dirname "$0")"
python3 - <<'PY' > /tmp/sh_sha_vectors.txt
import hashlib, os, random
random.seed(7)
for n in list(range(0, 130)) + [255, 256, 1000]:
    data = bytes(random.getrandbits(8) for _ in range(n))
    print(data.hex() or "", hashlib.sha256(data).hexdigest())
PY
g++ -std=c++11 -Wall -Wextra -Wpedantic -Werror -O1 -fsanitize=address,undefined -o /tmp/sh_test_logic test_logic.cpp
/tmp/sh_test_logic /tmp/sh_sha_vectors.txt

# ---- config store / devices / automation against in-memory flash (stand-in ArduinoJson, see stubs/)
FW=../../esp32_firmware/esp32_smart_home
g++ -std=c++17 -Wall -Wextra -Werror -O1 -fsanitize=address,undefined -DCONFIG_IDF_TARGET_ESP32=1 \
    -Istubs -I$FW -o /tmp/sh_test_store test_store.cpp stubs_impl.cpp $FW/config_store.cpp $FW/devices.cpp $FW/automation.cpp
/tmp/sh_test_store

# ---- the REAL api.cpp handlers (auth, claim, lock-outs, redaction, validation) behind a functional WebServer stand-in
g++ -std=c++17 -Wall -Wextra -Werror -O1 -fsanitize=address,undefined -DCONFIG_IDF_TARGET_ESP32=1 -DSH_HOST_TEST=1 -Istubs -I$FW \
    -o /tmp/sh_test_api test_api.cpp stubs_impl.cpp $FW/api.cpp $FW/config_store.cpp $FW/devices.cpp $FW/automation.cpp $FW/snapshot.cpp
/tmp/sh_test_api
python3 test_schema.py            # (the script already cd-ed into tests/firmware)

# ---- the REAL cloud.cpp (MQTT) with recording stand-ins for the TLS client and PubSubClient
g++ -std=c++17 -Wall -Wextra -Werror -O1 -fsanitize=address,undefined -DCONFIG_IDF_TARGET_ESP32=1 -DSH_HOST_TEST=1 -Istubs -I$FW \
    -o /tmp/sh_test_cloud test_cloud.cpp stubs_impl.cpp $FW/cloud.cpp $FW/config_store.cpp $FW/devices.cpp $FW/automation.cpp $FW/snapshot.cpp
/tmp/sh_test_cloud
