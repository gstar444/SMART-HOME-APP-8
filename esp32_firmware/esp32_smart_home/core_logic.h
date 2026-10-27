/*
  core_logic.h - hardware-independent helpers used across the firmware.

  Pure C++11, no Arduino headers: everything here is unit-tested on a PC
  (tests/firmware/test_logic.cpp). Anything that touches a pin, Wi-Fi, flash or
  the network lives in the other modules.
*/
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// ------------------------------------------------------------------ text helpers
inline bool isIdChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// Device / room / scene / rule ids: [A-Za-z0-9_], 1..maxLen characters.
inline bool validId(const char* s, size_t maxLen = 15) {
  if (!s) return false;
  size_t n = strlen(s);
  if (n == 0 || n > maxLen) return false;
  for (size_t i = 0; i < n; i++) if (!isIdChar(s[i])) return false;
  return true;
}

// MQTT topic segment for the home: lowercase letters, digits, '-' and '_' (never / + #).
inline bool validHomeId(const char* s) {
  if (!s) return false;
  size_t n = strlen(s);
  if (n == 0 || n > 32) return false;
  for (size_t i = 0; i < n; i++) {
    char c = s[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (!ok) return false;
  }
  return true;
}

// Dotted-quad such as 192.168.1.5 (four groups of 1-3 digits).
inline bool looksLikeIpv4(const char* s) {
  int groups = 0, digits = 0;
  for (const char* p = s;; p++) {
    if (*p >= '0' && *p <= '9') { if (++digits > 3) return false; }
    else if (*p == '.' || *p == '\0') { if (digits == 0) return false; groups++; digits = 0; if (*p == '\0') break; }
    else return false;
  }
  return groups == 4;
}

// Broker host NAME: letters, digits, '-' and '.', 3..63 chars, no leading/trailing dot, and not an IP address
// (the TLS certificate is checked against the name, so an address would only produce a confusing failure).
inline bool validHostName(const char* s) {
  if (!s) return false;
  size_t n = strlen(s);
  if (n < 3 || n > 63 || s[0] == '.' || s[n - 1] == '.') return false;
  if (looksLikeIpv4(s)) return false;
  for (size_t i = 0; i < n; i++) {
    char c = s[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.';
    if (!ok) return false;
  }
  return true;
}

// Copy into a fixed buffer, never splitting a UTF-8 sequence (names may be Hindi).
inline void copyStr(char* dst, size_t cap, const char* src) {
  if (cap == 0) return;
  if (!src) src = "";
  size_t n = strlen(src);
  if (n >= cap) {
    n = cap - 1;
    while (n > 0 && (((uint8_t)src[n]) & 0xC0) == 0x80) n--;
  }
  memmove(dst, src, n);   // memmove: callers sometimes pass dst == src
  dst[n] = '\0';
}

// Remove control characters and surrounding spaces in place. Returns the new length.
inline size_t cleanText(char* s) {
  size_t w = 0;
  for (size_t r = 0; s[r]; r++) {
    uint8_t c = (uint8_t)s[r];
    if (c < 0x20 || c == 0x7F) continue;
    s[w++] = (char)c;
  }
  s[w] = '\0';
  size_t start = 0;
  while (s[start] == ' ') start++;
  if (start) {
    memmove(s, s + start, w - start + 1);
    w -= start;
  }
  while (w > 0 && s[w - 1] == ' ') s[--w] = '\0';
  return w;
}

// ------------------------------------------------------------------ MQTT topics
// sh/v1/<home>/<board>/<leaf>   leaf = cmd | ack | state | inventory | lwt
inline int buildTopic(char* out, size_t cap, const char* home, const char* board, const char* leaf) {
  int n = snprintf(out, cap, "sh/v1/%s/%s/%s", home, board, leaf);
  return (n < 0 || (size_t)n >= cap) ? -1 : n;
}

// Reconnect delay: 5 s doubling to 5 min, plus up to 20 % jitter so a router
// restart does not make every board hit the broker at the same instant.
inline uint32_t backoffMs(uint8_t attempt, uint32_t rnd) {
  uint32_t base = 5000UL << (attempt > 6 ? 6 : attempt);
  if (base > 300000UL) base = 300000UL;
  return base + (rnd % (base / 5 + 1));
}

// ------------------------------------------------------------------ constant time / hex
inline bool ctEqual(const uint8_t* a, const uint8_t* b, size_t n) {
  uint8_t diff = 0;
  for (size_t i = 0; i < n; i++) diff |= (uint8_t)(a[i] ^ b[i]);
  return diff == 0;
}

inline void toHex(const uint8_t* in, size_t n, char* out) {   // out needs 2n+1 bytes
  static const char* d = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) { out[2 * i] = d[in[i] >> 4]; out[2 * i + 1] = d[in[i] & 15]; }
  out[2 * n] = '\0';
}

inline int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

inline bool fromHex(const char* s, uint8_t* out, size_t n) {
  if (!s || strlen(s) != 2 * n) return false;
  for (size_t i = 0; i < n; i++) {
    int hi = hexNibble(s[2 * i]), lo = hexNibble(s[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = (uint8_t)((hi << 4) | lo);
  }
  return true;
}

// ------------------------------------------------------------------ claim code
// 10 random symbols from a 32-symbol alphabet (50 bits), shown as XXXXX-XXXXX.
// It is the unique per-device setup secret: Wi-Fi password of the setup hotspot
// AND proof of physical access for POST /api/v2/claim. No shared default exists.
inline void makeClaimCode(const uint8_t* rnd /*10 bytes*/, char* out /*12 bytes*/) {
  static const char* alpha = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
  for (int i = 0; i < 10; i++) out[i < 5 ? i : i + 1] = alpha[rnd[i] & 31];
  out[5] = '-';
  out[11] = '\0';
}

// Upper-case, drop '-' and spaces, fold look-alikes (O->0, I/L->1). Returns length.
inline size_t normalizeClaimCode(const char* in, char* out, size_t cap) {
  size_t w = 0;
  for (size_t i = 0; in && in[i] && w + 1 < cap; i++) {
    char c = in[i];
    if (c == '-' || c == ' ') continue;
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    if (c == 'O') c = '0';
    if (c == 'I' || c == 'L') c = '1';
    out[w++] = c;
  }
  out[w] = '\0';
  return w;
}

inline bool claimCodeMatches(const char* stored, const char* supplied) {
  char a[16], b[16];
  size_t la = normalizeClaimCode(stored, a, sizeof(a));
  size_t lb = normalizeClaimCode(supplied, b, sizeof(b));
  if (la != 10 || lb != 10) return false;
  return ctEqual((const uint8_t*)a, (const uint8_t*)b, 10);
}

// Bearer tokens are generated by the app: URL-safe characters, 32..128 long.
inline bool validTokenText(const char* s) {
  if (!s) return false;
  size_t n = strlen(s);
  if (n < 32 || n > 128) return false;
  for (size_t i = 0; i < n; i++) {
    char c = s[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (!ok) return false;
  }
  return true;
}

// MQTT command ids are chosen by the app (UUID-like): [A-Za-z0-9_-], 1..24 characters.
inline bool validCmdId(const char* s) {
  if (!s) return false;
  size_t n = strlen(s);
  if (n == 0 || n > 24) return false;
  for (size_t i = 0; i < n; i++) if (!isIdChar(s[i]) && s[i] != '-') return false;
  return true;
}

// ------------------------------------------------------------------ rate limiting
// After maxFails failures inside windowMs, refuse everything for lockMs.
struct FailLimiter {
  uint8_t maxFails;
  uint32_t windowMs, lockMs;
  uint8_t fails;
  uint32_t windowStart, lockUntil;
  bool lockedFlag;

  FailLimiter(uint8_t mf = 5, uint32_t win = 60000UL, uint32_t lock = 60000UL)
      : maxFails(mf), windowMs(win), lockMs(lock), fails(0), windowStart(0), lockUntil(0), lockedFlag(false) {}

  bool locked(uint32_t now) {
    if (lockedFlag && (int32_t)(now - lockUntil) >= 0) { lockedFlag = false; fails = 0; }
    return lockedFlag;
  }
  void fail(uint32_t now) {
    if (locked(now)) return;
    if (fails == 0 || (uint32_t)(now - windowStart) > windowMs) { fails = 0; windowStart = now; }
    if (++fails >= maxFails) { lockedFlag = true; lockUntil = now + lockMs; }
  }
  void success() { fails = 0; }
};

// At most `max` events per `windowMs`.
struct RateWindow {
  uint32_t windowMs;
  uint16_t max;
  uint32_t start;
  uint16_t count;
  RateWindow(uint32_t win = 10000UL, uint16_t m = 20) : windowMs(win), max(m), start(0), count(0) {}
  bool allow(uint32_t now) {
    if (count == 0 || (uint32_t)(now - start) >= windowMs) { start = now; count = 0; }
    if (count >= max) return false;
    count++;
    return true;
  }
};

// Remembers the last 16 command ids so a re-delivered MQTT command runs once.
struct DedupeRing {
  enum { N = 16, L = 25 };
  char ids[N][L];
  uint8_t head, filled;
  DedupeRing() : head(0), filled(0) { memset(ids, 0, sizeof(ids)); }
  bool seen(const char* id) const {
    for (uint8_t i = 0; i < filled; i++) if (!strncmp(ids[i], id, L - 1)) return true;
    return false;
  }
  int find(const char* id) const {          // slot index of a remembered id, or -1
    for (uint8_t i = 0; i < filled; i++) if (!strncmp(ids[i], id, L - 1)) return i;
    return -1;
  }
  // true = duplicate (already seen); otherwise remembers it and returns false.
  bool checkAndAdd(const char* id) {
    if (seen(id)) return true;
    strncpy(ids[head], id, L - 1);
    ids[head][L - 1] = '\0';
    head = (uint8_t)((head + 1) % N);
    if (filled < N) filled++;
    return false;
  }
};

// ------------------------------------------------------------------ time / schedules
inline bool parseHHMM(const char* s, int* minutes) {
  if (!s || strlen(s) != 5 || s[2] != ':') return false;
  static const int digitAt[4] = {0, 1, 3, 4};
  for (int k = 0; k < 4; k++) if (s[digitAt[k]] < '0' || s[digitAt[k]] > '9') return false;
  int h = (s[0] - '0') * 10 + (s[1] - '0');
  int m = (s[3] - '0') * 10 + (s[4] - '0');
  if (h > 23 || m > 59) return false;
  *minutes = h * 60 + m;
  return true;
}

inline void formatHHMM(int minutes, char* out /*6 bytes*/) {
  unsigned h = (unsigned)((minutes / 60) % 24) % 100u;   // %100: tells the compiler each field is 2 digits
  unsigned m = (unsigned)(minutes % 60) % 100u;
  snprintf(out, 6, "%02u:%02u", h, m);
}

// from == to means "all day"; from > to wraps past midnight (e.g. 22:00 - 06:00).
inline bool inTimeWindow(int from, int to, int nowMin) {
  if (from == to) return true;
  if (from < to) return nowMin >= from && nowMin < to;
  return nowMin >= from || nowMin < to;
}

// daysMask: bit0 = Sunday ... bit6 = Saturday (same numbering as tm_wday).
inline bool scheduleDue(uint8_t daysMask, int schedMinute, int wday, int nowMinute) {
  return wday >= 0 && wday <= 6 && ((daysMask >> wday) & 1) && schedMinute == nowMinute;
}

// ------------------------------------------------------------------ selectors
// Action targets: "<deviceId>", "type:<switch|lock|pir>" or "icon:<icon>".
inline bool selectorMatches(const char* sel, const char* id, const char* type, const char* icon) {
  if (!strncmp(sel, "type:", 5)) return !strcmp(sel + 5, type);
  if (!strncmp(sel, "icon:", 5)) return !strcmp(sel + 5, icon);
  return !strcmp(sel, id);
}

inline bool validSelectorSyntax(const char* sel) {
  if (!sel) return false;
  if (!strncmp(sel, "type:", 5) || !strncmp(sel, "icon:", 5)) return validId(sel + 5, 12);
  return validId(sel, 15);
}

// ------------------------------------------------------------------ temperature trigger
// Fires once when the reading crosses `value` ('>' rising, '<' falling), then
// re-arms only after it has moved 0.5 degrees back. Starts disarmed so a board
// that boots already beyond the threshold does not act until it was seen on the
// safe side first (no surprise heater at power-up).
inline bool tempCrossed(char op, float value, float temp, bool& armed) {
  if (temp != temp) return false;   // NaN
  if (op == '>') {
    if (temp > value) { if (armed) { armed = false; return true; } }
    else if (temp < value - 0.5f) armed = true;
  } else {
    if (temp < value) { if (armed) { armed = false; return true; } }
    else if (temp > value + 0.5f) armed = true;
  }
  return false;
}
