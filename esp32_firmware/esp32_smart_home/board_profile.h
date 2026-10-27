/*
  board_profile.h - target-specific GPIO policy.

  Pure C++ (no Arduino headers) so the same code is unit-tested on a PC
  (tests/firmware/test_logic.cpp). C++11 compatible.

  WHAT THIS DECIDES
    * which GPIOs may drive a relay (output) or read a sensor (input) on
      ESP32 (DevKit/WROOM/WROVER), ESP32-S3 and ESP32-C3,
    * which pins are never offered: boot/strapping, flash, USB, UART0,
      PSRAM (when the module has PSRAM) and pins that do not exist.

  The tables follow Espressif's datasheets/hardware-design guidelines as the
  author understood them. They are deliberately conservative, but module
  variants differ (e.g. octal-flash S3 modules): CHECK THE DATASHEET OF YOUR
  EXACT MODULE before wiring mains-voltage relays. See docs/HARDWARE.md.

  "RISKY" pins (ESP32 GPIO5 and GPIO14) emit a short signal during boot, so a
  relay on them can click at power-up. They are never offered for NEW devices.
  A device migrated from firmware v3 that already uses one keeps working only
  because migration sets riskyPinOk=true on it.
*/
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum BoardTarget : uint8_t {
  TARGET_ESP32 = 0,     // ESP32 DevKit / WROOM / WROVER (classic dual-core)
  TARGET_ESP32_S3 = 1,
  TARGET_ESP32_C3 = 2
};

enum PinClass : uint8_t {
  PIN_OK = 0,          // general purpose, safe at boot
  PIN_RISKY,           // usable, but pulses / is sampled at boot (ESP32: 5, 14)
  PIN_INPUT_ONLY,      // input only (ESP32: 34, 35, 36, 39)
  PIN_STRAPPING,       // boot-mode strapping pin
  PIN_FLASH,           // connected to SPI flash
  PIN_PSRAM,           // connected to PSRAM on modules that have it
  PIN_USB,             // native USB D+/D-
  PIN_UART,            // UART0 (serial console / flashing)
  PIN_NONEXISTENT
};

inline const char* targetName(BoardTarget t) {
  switch (t) {
    case TARGET_ESP32_S3: return "esp32-s3";
    case TARGET_ESP32_C3: return "esp32-c3";
    default:              return "esp32";
  }
}

inline bool targetFromName(const char* s, BoardTarget* out) {
  if (!s) return false;
  if (!strcmp(s, "esp32"))    { *out = TARGET_ESP32;    return true; }
  if (!strcmp(s, "esp32-s3")) { *out = TARGET_ESP32_S3; return true; }
  if (!strcmp(s, "esp32-c3")) { *out = TARGET_ESP32_C3; return true; }
  return false;
}

// Chip family this firmware was compiled for. Unsupported chips fail the build
// instead of silently using the wrong pin table.
inline BoardTarget compileTarget() {
#if defined(SH_HOST_TEST)
  return TARGET_ESP32;
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
  return TARGET_ESP32_S3;
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
  return TARGET_ESP32_C3;
#elif defined(CONFIG_IDF_TARGET_ESP32)
  return TARGET_ESP32;
#else
#error "Unsupported chip: this firmware supports ESP32, ESP32-S3 and ESP32-C3 only."
#endif
}

// GPIO of the board's BOOT button (used for the physical credential reset).
inline int defaultResetPin(BoardTarget t) { return t == TARGET_ESP32_C3 ? 9 : 0; }

inline PinClass classifyPin(BoardTarget t, bool psram, int pin) {
  if (pin < 0) return PIN_NONEXISTENT;
  switch (t) {
    case TARGET_ESP32:
      if (pin > 39) return PIN_NONEXISTENT;
      switch (pin) {
        case 0: case 2: case 12: case 15: return PIN_STRAPPING;
        case 1: case 3:                   return PIN_UART;
        case 5: case 14:                  return PIN_RISKY;
        case 6: case 7: case 8: case 9: case 10: case 11: return PIN_FLASH;
        case 16: case 17:                 return psram ? PIN_PSRAM : PIN_OK;
        case 20: case 24: case 28: case 29: case 30: case 31:
        case 37: case 38:                 return PIN_NONEXISTENT;
        case 34: case 35: case 36: case 39: return PIN_INPUT_ONLY;
        default:                          return PIN_OK;
      }
    case TARGET_ESP32_S3:
      if (pin > 48) return PIN_NONEXISTENT;
      if (pin == 0 || pin == 3 || pin == 45 || pin == 46) return PIN_STRAPPING;
      if (pin == 19 || pin == 20) return PIN_USB;
      if (pin >= 22 && pin <= 25) return PIN_NONEXISTENT;
      if (pin >= 26 && pin <= 32) return PIN_FLASH;
      if (pin >= 33 && pin <= 37) return psram ? PIN_PSRAM : PIN_OK;
      if (pin == 43 || pin == 44) return PIN_UART;
      return PIN_OK;
    case TARGET_ESP32_C3:
      if (pin > 21) return PIN_NONEXISTENT;
      if (pin == 2 || pin == 8 || pin == 9) return PIN_STRAPPING;
      if (pin >= 11 && pin <= 17) return PIN_FLASH;
      if (pin == 18 || pin == 19) return PIN_USB;
      if (pin == 20 || pin == 21) return PIN_UART;
      return PIN_OK;
  }
  return PIN_NONEXISTENT;
}

inline const char* pinClassName(PinClass c) {
  switch (c) {
    case PIN_OK:          return "ok";
    case PIN_RISKY:       return "risky_boot_signal";
    case PIN_INPUT_ONLY:  return "input_only";
    case PIN_STRAPPING:   return "strapping";
    case PIN_FLASH:       return "flash";
    case PIN_PSRAM:       return "psram";
    case PIN_USB:         return "usb";
    case PIN_UART:        return "uart0";
    default:              return "nonexistent";
  }
}

// Output (relay) pins. allowRisky is true only for devices migrated from v3.
inline bool pinUsableAsOutput(BoardTarget t, bool psram, int pin, bool allowRisky) {
  PinClass c = classifyPin(t, psram, pin);
  return c == PIN_OK || (allowRisky && c == PIN_RISKY);
}

// Input (PIR / button) pins.
inline bool pinUsableAsInput(BoardTarget t, bool psram, int pin, bool allowRisky) {
  PinClass c = classifyPin(t, psram, pin);
  return c == PIN_OK || c == PIN_INPUT_ONLY || (allowRisky && c == PIN_RISKY);
}

// Lists the pins offered to the app. `reserved` (e.g. the DHT pin) is skipped; -1 = none.
inline size_t listPins(BoardTarget t, bool psram, bool output, int reserved, uint8_t* out, size_t cap) {
  size_t n = 0;
  for (int p = 0; p <= 48 && n < cap; p++) {
    if (p == reserved) continue;
    bool ok = output ? pinUsableAsOutput(t, psram, p, false)
                     : (classifyPin(t, psram, p) == PIN_INPUT_ONLY);   // extra, input-only pins
    if (ok) out[n++] = (uint8_t)p;
  }
  return n;
}
