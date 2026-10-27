#pragma once
#include "Arduino.h"
struct MDNSClass { bool begin(const char*) { return true; } void end() {} void addService(const char*, const char*, int) {} };
extern MDNSClass MDNS;
