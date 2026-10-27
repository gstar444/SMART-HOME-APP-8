// Definitions for the host stand-ins + the globals the .ino normally provides.
#include "Arduino.h"
#include "LittleFS.h"
#include "Preferences.h"
#include "WiFi.h"
#include "ESPmDNS.h"
#include "runtime.h"
#include "PubSubClient.h"
#include "WiFiClientSecure.h"
SerialClass Serial;
uint32_t g_millis = 0;
std::map<int, int> g_pins, g_pinMode;
std::vector<std::string> g_pinEvents;
EspClass ESP;
std::map<std::string, std::string> g_fs;
std::map<std::string, std::map<std::string, std::string>> g_nvs;
FSClass LittleFS;
WiFiClass WiFi;
MDNSClass MDNS;
BoardTarget gTarget = TARGET_ESP32;
bool gPsram = false;
char gBoardId[24] = "sh-aabbcc";
std::string g_tlsCa;
int g_tlsCaCalls = 0, g_tlsInsecureCalls = 0;
std::function<void(char*, uint8_t*, unsigned int)> g_mqttCb;
std::vector<MqttPub> g_mqttPub;
std::vector<std::string> g_mqttSubs;
MqttConn g_mqttConn;
std::string g_mqttHost;
int g_mqttPort = 0, g_mqttConnectCalls = 0, g_mqttState = 0, g_mqttDisconnects = 0;
bool g_mqttConnectResult = true, g_mqttConnected = false;
