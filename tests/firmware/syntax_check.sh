#!/usr/bin/env bash
# Syntax/type check of EVERY firmware source file for each supported chip, against host stand-ins for the
# Arduino/ESP32 APIs (tests/firmware/stubs). It catches typos, wrong types and signature mismatches
# between the firmware's own modules. It is NOT a real Arduino build: the stand-ins are written by hand,
# so a green result here does not prove compatibility with the actual ESP32 core or libraries.
# Real build:  arduino-cli compile --fqbn esp32:esp32:esp32 esp32_firmware/esp32_smart_home
set -euo pipefail
cd "$(dirname "$0")"
FW=../../esp32_firmware/esp32_smart_home
fail=0
for T in CONFIG_IDF_TARGET_ESP32 CONFIG_IDF_TARGET_ESP32S3 CONFIG_IDF_TARGET_ESP32C3; do
  for f in config_store.cpp devices.cpp automation.cpp net.cpp api.cpp cloud.cpp snapshot.cpp; do
    out=$(g++ -std=c++17 -fsyntax-only -Wall -Wextra -D$T=1 -Istubs -I$FW $FW/$f 2>&1) || true
    if [ -n "$out" ]; then echo "== $T $f"; echo "$out" | head -20; fail=1; fi
  done
  out=$(g++ -std=c++17 -fsyntax-only -Wall -Wextra -D$T=1 -Istubs -I$FW -x c++ $FW/esp32_smart_home.ino 2>&1) || true
  if [ -n "$out" ]; then echo "== $T esp32_smart_home.ino"; echo "$out" | head -20; fail=1; fi
done
# the chip guard must refuse to build for an unsupported chip
if g++ -std=c++17 -fsyntax-only -Istubs -I$FW $FW/devices.cpp 2>/dev/null; then echo "chip guard missing"; fail=1; fi
[ $fail -eq 0 ] && echo "syntax check passed: 8 firmware files x 3 chips, warnings-as-failures, chip guard works"
exit $fail
