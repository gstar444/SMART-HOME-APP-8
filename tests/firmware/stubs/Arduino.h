// Host stand-in for the Arduino-ESP32 core: ONLY what the firmware uses. Not a faithful emulation.
#pragma once
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <vector>

#define F(x) (x)
#define PROGMEM
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define HIGH 1
#define LOW 0
typedef bool boolean;
using std::isnan;

class String : public std::string {
 public:
  String() {}
  String(const char* s) : std::string(s ? s : "") {}
  String(const std::string& s) : std::string(s) {}
  String(int v) : std::string(std::to_string(v)) {}
  String(unsigned long v) : std::string(std::to_string(v)) {}
  size_t length() const { return size(); }
  bool startsWith(const char* p) const { return rfind(p, 0) == 0; }
  int indexOf(const char* p) const { size_t i = find(p); return i == npos ? -1 : (int)i; }
  String substring(size_t a) const { return String(substr(a)); }
  String operator+(const String& o) const { return String(std::string(*this) + std::string(o)); }
  String& operator+=(const char* o) { append(o); return *this; }
  String& operator+=(const String& o) { append(o); return *this; }
  bool operator==(const char* o) const { return std::string(*this) == o; }
  bool operator==(const String& o) const { return std::string(*this) == std::string(o); }
};

class Print {
 public:
  virtual ~Print() {}
  virtual size_t write(uint8_t c) { return write(&c, 1); }
  virtual size_t write(const uint8_t*, size_t n) { return n; }
  size_t print(const char* s) { return write((const uint8_t*)s, strlen(s)); }
};

struct SerialClass {
  void begin(unsigned long) {}
  void println() { std::puts(""); }
  void println(const char* s) { std::puts(s); }
  void println(const String& s) { std::puts(s.c_str()); }
  void print(const char* s) { std::fputs(s, stdout); }
  int printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    va_list a; va_start(a, fmt); int n = std::vfprintf(stdout, fmt, a); va_end(a); return n;
  }
};
extern SerialClass Serial;

extern uint32_t g_millis;
inline uint32_t millis() { return g_millis; }
inline void delay(uint32_t ms) { g_millis += ms; }
extern std::map<int, int> g_pins;          // last written level per pin
extern std::map<int, int> g_pinMode;
extern std::vector<std::string> g_pinEvents;   // "w<pin>=<level>" / "m<pin>=<mode>" in call order
inline void pinMode(int p, int m) { g_pinMode[p] = m; g_pinEvents.push_back("m" + std::to_string(p) + "=" + std::to_string(m)); }
inline void digitalWrite(int p, int v) { g_pins[p] = v; g_pinEvents.push_back("w" + std::to_string(p) + "=" + std::to_string(v)); }
inline int digitalRead(int p) { return g_pins.count(p) ? g_pins[p] : 1; }
inline uint32_t esp_random() { return 0x12345678u; }
inline void esp_fill_random(void* b, size_t n) { for (size_t i = 0; i < n; i++) ((uint8_t*)b)[i] = (uint8_t)(i * 37 + 11); }
inline bool psramFound() { return false; }
inline void configTzTime(const char*, const char*, const char*) {}
struct EspClass {
  void restart() {}
  uint64_t getEfuseMac() { return 0xAABBCCDDEEFFull; }
};
extern EspClass ESP;
