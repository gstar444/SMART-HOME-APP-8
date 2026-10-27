#pragma once
#include "Arduino.h"
extern std::string g_tlsCa;          // PEM text most recently passed to setCACert (empty = none)
extern int g_tlsCaCalls, g_tlsInsecureCalls;
struct WiFiClientSecure {
  void setCACert(const char* pem) { g_tlsCa = pem ? pem : ""; g_tlsCaCalls++; }
  void setHandshakeTimeout(unsigned long) {}
  void stop() {}
  int lastError(char* b, const size_t n) { if (n) b[0] = 0; return 0; }
  // Deliberately present so a call to it is COUNTED. The firmware must never make it.
  void setInsecure() { g_tlsInsecureCalls++; }
};
