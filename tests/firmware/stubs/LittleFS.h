#pragma once
#include "Arduino.h"
extern std::map<std::string, std::string> g_fs;   // in-memory flash
class File : public Print {
  std::string path_, mode_, buf_; bool ok_ = false;
 public:
  File() {}
  File(std::string p, std::string m) : path_(p), mode_(m), ok_(true) { if (m == "r") buf_ = g_fs[p]; }
  explicit operator bool() const { return ok_; }
  size_t write(const uint8_t* b, size_t n) override { buf_.append((const char*)b, n); return n; }
  size_t print(const char* s) { return write((const uint8_t*)s, strlen(s)); }
  void flush() { if (mode_ == "w") g_fs[path_] = buf_; }
  void close() { if (ok_ && mode_ == "w") g_fs[path_] = buf_; ok_ = false; }
  String readString() { return String(buf_); }
  const std::string& contents() const { return buf_; }
};
struct FSClass {
  bool begin(bool = false, const char* = "/littlefs", uint8_t = 10, const char* = "spiffs") { return true; }
  File open(const char* p, const char* m = "r") {
    if (std::string(m) == "r" && !g_fs.count(p)) return File();
    return File(p, m);
  }
  bool exists(const char* p) { return g_fs.count(p) > 0; }
  bool remove(const char* p) { return g_fs.erase(p) > 0; }
  bool rename(const char* a, const char* b) {
    if (!g_fs.count(a)) return false;
    g_fs[b] = g_fs[a]; g_fs.erase(a); return true;
  }
};
extern FSClass LittleFS;
