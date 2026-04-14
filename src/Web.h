#include "Globals.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>

// Function Prototypes
void initWiFi(const char *ssid, const char *password);
void initWebServer();
void cleanupWebClients();

// Telemetry
void broadcastTelemetry(float time, float pow, float tPow, float tFlow, float flow,
                        float temp, float yield);
void broadcastShotStart(String profileName, float dose);

// JSON File Management
bool loadProfile(String profileId, EspressoProfile &profile);
void saveShotToHistory(String profileName, float dose, float yield, float totalTime);
String getAvailableProfileIDs();
