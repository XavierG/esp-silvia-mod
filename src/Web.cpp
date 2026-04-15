#include "web.h"
#include <Update.h>

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// --- WiFi Initialization ---
void initWiFi(const char *ssid, const char *password) {
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
}

// --- JSON File Management ---
bool loadProfile(String profileId, EspressoProfile &profile) {
  File file = LittleFS.open("/profiles.json", "r");
  if (!file) {
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error || !doc[profileId].is<JsonObject>()) {
    return false;
  }

  JsonObject pData = doc[profileId];
  profile.name = pData["name"].as<String>();

  // Phase 1
  profile.phase1.start = pData["phase1"]["start"];
  profile.phase1.end = pData["phase1"]["end"];
  profile.phase1.time = pData["phase1"]["time"];
  profile.phase1.exitWeight = pData["phase1"]["exitW"];
  profile.phase1.exitTime = pData["phase1"]["exitT"];

  // Phase 2
  profile.phase2.mode = pData["phase2"]["mode"].as<String>();
  profile.phase2.start = pData["phase2"]["start"];
  profile.phase2.end = pData["phase2"]["end"];
  profile.phase2.time = pData["phase2"]["time"];
  profile.phase2.exitWeight = pData["phase2"]["exitW"];
  profile.phase2.exitTime = pData["phase2"]["exitT"];

  // Phase 3
  profile.phase3.mode = pData["phase3"]["mode"].as<String>();
  profile.phase3.start = pData["phase3"]["start"];
  profile.phase3.end = pData["phase3"]["end"];
  profile.phase3.time = pData["phase3"]["time"];

  return true;
}

void saveShotToHistory(String profileName, float dose, float yield,
                       float totalTime) {
  File file = LittleFS.open("/history.json", "r");
  JsonDocument doc;

  // Load existing history if the file exists
  if (file) {
    deserializeJson(doc, file);
    file.close();
  }

  // Ensure the root is an array
  if (!doc.is<JsonArray>()) {
    doc.to<JsonArray>();
  }
  JsonArray arr = doc.as<JsonArray>();

  // Create the new shot object
  JsonDocument newShot;
  newShot["name"] = profileName;
  newShot["date"] = getTimestamp();
  newShot["dose"] = dose;
  newShot["yield"] = yield;
  newShot["time"] = totalTime;

  // Build the data arrays dynamically from our static C++ arrays
  JsonArray tArr = newShot["data"]["time"].to<JsonArray>();
  JsonArray tpArr = newShot["data"]["tP"].to<JsonArray>();
  JsonArray tfArr = newShot["data"]["tF"].to<JsonArray>();
  JsonArray pArr = newShot["data"]["power"].to<JsonArray>();
  JsonArray fArr = newShot["data"]["flow"].to<JsonArray>();
  JsonArray tmpArr = newShot["data"]["temp"].to<JsonArray>();

  for (int i = 0; i < histCount; i++) {
    tArr.add(histTime[i]);
    tpArr.add(histTargetP[i]);
    tfArr.add(histTargetF[i]);
    pArr.add(histPower[i]);
    fArr.add(histFlow[i]);
    tmpArr.add(histTemp[i]);
  }

  // Append new shot to the array
  arr.add(newShot);

  // Enforce the 5-shot maximum limit (removes the oldest shot at index 0)
  while (arr.size() > 5) {
    arr.remove(0);
  }

  // Save back to flash
  file = LittleFS.open("/history.json", "w");
  if (file) {
    serializeJson(doc, file);
    file.close();
    Serial.println("Shot saved to history.json!");
  } else {
    Serial.println("Failed to write history.json");
  }
}

// --- Private Helpers ---
void handleWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                          AwsEventType type, void *arg, uint8_t *data,
                          size_t len) {
  if (type == WS_EVT_CONNECT)
    Serial.printf("UI Connected: #%u\n", client->id());
}

// --- Web Server Initialization ---
void initWebServer() {
  if (!MDNS.begin("silvia")) {
    Serial.println("Error setting up MDNS responder!");
  } else {
    Serial.println("mDNS responder started: silvia.local");
  }

  // Optional: Add services so other apps can find it automatically
  MDNS.addService("http", "tcp", 80);

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS Mount Failed");
    return;
  }

  ws.onEvent(handleWebSocketEvent);
  server.addHandler(&ws);

  server.on("/", HTTP_GET | HTTP_HEAD, [](AsyncWebServerRequest *request) {
    if (request->method() == HTTP_HEAD) {
      // Just send headers back, no body. Very fast!
      request->send(200);
    } else {
      request->send(LittleFS, "/index.html", "text/html");
    }
  });
  server.on("/profiles.json", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/profiles.json", "application/json");
  });
  server.on("/history.json", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/history.json", "application/json");
  });
  server.on("/saved_shots.json", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/saved_shots.json", "application/json");
  });

  // API: Save Profiles from UI
  server.on(
      "/api/profiles", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        static String body;
        if (index == 0)
          body = "";
        for (size_t i = 0; i < len; i++)
          body += (char)data[i];

        if (index + len == total) {
          File file = LittleFS.open("/profiles.json", "w");
          if (file) {
            file.print(body);
            file.close();
            request->send(200, "text/plain", "OK");
          } else {
            request->send(500, "text/plain", "File Write Failed");
          }
        }
      });

  // API: Save Logbook (Saved Shots) from UI
  server.on(
      "/api/saved", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len,
         size_t index, size_t total) {
        static String body;
        if (index == 0)
          body = "";
        for (size_t i = 0; i < len; i++)
          body += (char)data[i];

        if (index + len == total) {
          File file = LittleFS.open("/saved_shots.json", "w");
          if (file) {
            file.print(body);
            file.close();
            request->send(200, "text/plain", "OK");
          } else {
            request->send(500, "text/plain", "File Write Failed");
          }
        }
      });

  // OTA Update Interface
  server.on("/ota", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = "<html><body><h2>Firmware Update</h2>"
                  "<form method='POST' action='/ota' enctype='multipart/form-data'>"
                  "<input type='file' accept='.bin' name='firmware'><br><br>"
                  "<input type='submit' value='Update'></form></body></html>";
    request->send(200, "text/html", html);
  });

  server.on("/ota", HTTP_POST, [](AsyncWebServerRequest *request) {
    bool shouldReboot = !Update.hasError();
    AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", shouldReboot ? "OK - Rebooting..." : "FAIL");
    response->addHeader("Connection", "close");
    request->send(response);
    if (shouldReboot) {
      delay(100);
      ESP.restart(); // Software restart
    }
  }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (!index) {
      Serial.printf("OTA Update Start: %s\n", filename.c_str());
      // Start update process
      if (!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)) {
        Update.printError(Serial);
      }
    }
    if (!Update.hasError()) {
      if (Update.write(data, len) != len) {
        Update.printError(Serial);
      }
    }
    if (final) {
      if (Update.end(true)) {
        Serial.printf("OTA Update Success: %uB\n", index + len);
      } else {
        Update.printError(Serial);
      }
    }
  });

  server.begin();
  Serial.println("Web Server Started");
}

void cleanupWebClients() { ws.cleanupClients(); }

void broadcastTelemetry(float time, float pow, float tPow, float tFlow,
                        float flow, float temp, float yield) {
  if (ws.count() == 0)
    return;
  char buffer[200];
  snprintf(buffer, sizeof(buffer),
           "{\"time\":%.2f,\"power\":%.1f,\"tP\":%.1f,\"tF\":%.2f,\"flow\":%."
           "2f,\"temp\":%.1f,\"yield\":%.1f}",
           time, pow, tPow, tFlow, flow, temp, yield);
  for (auto &client : ws.getClients()) {
    if (client.status() == WS_CONNECTED && client.queueLen() == 0) {
      client.text(buffer);
    }
  }
}

void broadcastShotStart(String profileName, float dose) {
  if (ws.count() == 0)
    return;

  // Zero dynamic memory for the start message as well
  char buffer[128];
  snprintf(buffer, sizeof(buffer),
           "{\"status\":\"start\",\"profileName\":\"%s\",\"dose\":%.1f}",
           profileName.c_str(), dose);

  ws.textAll(buffer);
}

String getAvailableProfileIDs() {
  File file = LittleFS.open("/profiles.json", "r");
  if (!file)
    return "";

  JsonDocument doc;
  deserializeJson(doc, file);
  file.close();

  String ids = "";
  JsonObject root = doc.as<JsonObject>();
  for (JsonPair kv : root) {
    if (ids.length() > 0)
      ids += ",";
    ids += kv.key().c_str();
  }
  return ids;
}