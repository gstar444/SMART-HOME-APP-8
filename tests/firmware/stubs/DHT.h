#pragma once
#include "Arduino.h"
#define DHT22 22
struct DHT { DHT(int, int) {} void begin() {} float readTemperature() { return 23.5f; } float readHumidity() { return 41.0f; } };
