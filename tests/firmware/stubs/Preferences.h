#pragma once
#include "Arduino.h"
extern std::map<std::string, std::map<std::string, std::string>> g_nvs;
class Preferences {
  std::string ns_; bool open_ = false;
 public:
  bool begin(const char* ns, bool readOnly = false) {
    if (readOnly && !g_nvs.count(ns)) return false;
    ns_ = ns; open_ = true; return true;
  }
  void end() { open_ = false; }
  void clear() { g_nvs[ns_].clear(); }
  String getString(const char* k, const String& d = String()) { auto& m = g_nvs[ns_]; return m.count(k) ? String(m[k]) : d; }
  unsigned int getUInt(const char* k, unsigned int d = 0) { auto& m = g_nvs[ns_]; return m.count(k) ? (unsigned)std::stoul(m[k]) : d; }
  size_t putString(const char* k, const String& v) { g_nvs[ns_][k] = v; return v.size(); }
  size_t putUInt(const char* k, unsigned int v) { g_nvs[ns_][k] = std::to_string(v); return 4; }
};
