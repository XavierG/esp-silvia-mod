/* Todo:

Sofware:
--- FRONTEND ---


--- ENGINE & CONTROL ---
- Wifi Manager for access point and wifi management
- Mapping Pump percentage and Pressure computed based on OPV volume measured
- PI(D) For pump % in flow control
- Kickstart" a pump by setting it to 100 for a few hundred milliseconds before
dropping it down

--- HARDWARE & INTEGRATION ---
- Which power supply?


Before Production:
- Test implementation of the FORCE_HEATER_TIME variable
- Update scale initials parameters / put them in define
- Integrate PID close to XMP7100 values
https://gemini.google.com/app/a45d001951bd94d5
- Test minimum pump percentage


Roadmap and low priority:
- Water level sensor
- Estimate the pressure based on flow rate and pump curve (estimated empirically
with OPV valve)
- Possible to migrate to a touch screen?
- Integration HA for machine readiness notification

*/

#include "Display.h"
#include "Globals.h"
#include "Hardware.h"
#include "Web.h"

// --- Forward Declarations ---
void loadSettings();
void tareScales();
void updateWeight();
void handleScreensaver();
void handleButtonLogic();
void handleEncoderLogic();
void handleShortClick();
void handleLongPress();
void updateBrewCycle();
void startBrew();
void stopShot();
void finishShot();
void updateActiveSettings();
void refreshProfileList();

// --- FreeRTOS Task Handle ---
TaskHandle_t displayTaskHandle;

// --- CORE 0: Dedicated Display Task ---
// This runs infinitely on the second core, isolated from the coffee logic
void displayTaskCode(void *pvParameters) {
  bool localSleepState = false;

  for (;;) {
    // 1. Check if Core 1 requested a sleep state change
    if (isScreenAsleep != localSleepState) {
      localSleepState = isScreenAsleep;
      u8g2.setPowerSave(localSleepState ? 1 : 0); // Hardware sleep/wake
    }

    // 2. Render the UI if awake
    if (!localSleepState) {
      renderUI();
    }

    // 3. Pause this core for the defined refresh interval (e.g. 33ms)
    vTaskDelay(pdMS_TO_TICKS(UI_REFRESH_INTERVAL_MS));
  }
}

void setup() {
  Serial.begin(115200);

  initWiFi(SECRET_SSID, SECRET_PASS);
  initWebServer();
  configTzTime(TZ_INFO, ntpServer);

  initHardware();
  initDisplay();
  loadSettings();
  waitForTemperatureSensor();

  lastEncoderCount = encoder.getCount();
  lastActivityTime = millis();

  // --- START DUAL CORE MULTITASKING ---
  xTaskCreatePinnedToCore(displayTaskCode, /* Task function */
                          "DisplayTask",   /* Name of task */
                          10000, /* Stack size of task (10kb is plenty) */
                          NULL,  /* Parameter of the task */
                          1,     /* Priority of the task */
                          &displayTaskHandle, /* Task handle */
                          0);                 /* Pin task to Core 0 */
}

// --- CORE 1: Main Coffee Logic ---
void loop() {
  // Clean up WebSocket clients periodically
  static unsigned long lastCleanup = 0;
  if (millis() - lastCleanup > 2000) {
    cleanupWebClients();
    lastCleanup = millis();
  }

  handleHeater();
  updateWeight();
  updateBrewCycle();
  handleScreensaver();
  if (!isScreenAsleep) {
    handleButtonLogic();
    handleEncoderLogic();
  } else {
    newClickAvailable = false;
  }

  // vTaskDelay(pdMS_TO_TICKS(1));
}

// --- Logic Implementation ---

void updateActiveSettings() {
  activeSettingsCount = 0;
  activeSettings[activeSettingsCount++] = PROFILE_SEL;
  activeSettings[activeSettingsCount++] = FLOW_FACT;
  activeSettings[activeSettingsCount++] = TGT_TEMP;
  activeSettings[activeSettingsCount++] = TEMP_OFF;
  activeSettings[activeSettingsCount++] = SCALE_MODE;
  activeSettings[activeSettingsCount++] = EXIT_MENU;

  if (currentSettingIndex >= activeSettingsCount) {
    currentSettingIndex = 0;
  }
}

void refreshProfileList() {
  String ids = getAvailableProfileIDs(); // Assuming this returns
                                         // "london_strand,flat_9_bar"
  numAvailableProfiles = 0;
  int startIdx = 0;
  int endIdx = ids.indexOf(',');

  while (endIdx != -1 && numAvailableProfiles < 15) {
    availableProfiles[numAvailableProfiles++] = ids.substring(startIdx, endIdx);
    startIdx = endIdx + 1;
    endIdx = ids.indexOf(',', startIdx);
  }
  if (startIdx < ids.length() && numAvailableProfiles < 15) {
    availableProfiles[numAvailableProfiles++] = ids.substring(startIdx);
  }
  if (numAvailableProfiles == 0) {
    availableProfiles[0] = "new";
    numAvailableProfiles = 1;
  }
}

void loadSettings() {
  prefs.begin("silvia", false);
  targetTemp = prefs.getFloat("tTemp", DEFAULT_TARGET_TEMP);
  tempOffset = prefs.getFloat("dTempOffset", DEFAULT_TEMP_OFFSET);
  coffeeWeight = prefs.getFloat("dCoffeeWeight", DEFAULT_COFFEE_WEIGHT);
  ratio = prefs.getFloat("dRatio", DEFAULT_RATIO);
  flowStopFactor = prefs.getFloat("dflowStopFactor", DEFAULT_FLOW_STOP_FACTOR);

  lastShotWeight = prefs.getFloat("lShtWght", 0.0);
  lastShotTime = prefs.getFloat("lShtTime", 0.0);
  lastShotRatio = prefs.getFloat("lShtRatio", 0.0);

  // Load dynamic profile list and init last used profile
  refreshProfileList();
  String lastProfId = prefs.getString("lastProfId", availableProfiles[0]);

  // Find index of saved profile to set menu position
  selectedProfileIndex = 0;
  for (int i = 0; i < numAvailableProfiles; i++) {
    if (availableProfiles[i] == lastProfId) {
      selectedProfileIndex = i;
      break;
    }
  }

  // Load the actual JSON data into currentProfile
  loadProfile(availableProfiles[selectedProfileIndex], currentProfile);

  updateActiveSettings();
}

void handleScreensaver() {
  long currentCount = encoder.getCount();

  if (currentCount != lastEncoderCount || isButtonPressed) {
    lastActivityTime = millis();
    if (isScreenAsleep) {
      isScreenAsleep = false; // Core 0 will see this and wake the screen

      if (currentCount != lastEncoderCount)
        encoder.setCount(lastEncoderCount);
      if (isButtonPressed) {
        newClickAvailable = false;
        isLongPressHandled = true;
      }
    }
  }
  lastEncoderCount = encoder.getCount();

  if (!isScreenAsleep &&
      (millis() - lastActivityTime > SCREENSAVER_TIMEOUT_MS)) {
    if (currentState != EXTRACTION && currentState != BREW_PROMPT) {
      isScreenAsleep = true; // Core 0 will see this and put the screen to sleep
    }
  }
}

void startBrew() {
  loadProfile(availableProfiles[selectedProfileIndex], currentProfile);
  tareScales();
  broadcastShotStart(currentProfile.name, coffeeWeight);
  currentWeight = 0;
  rawCurrentWeight = 0;
  currentFlowRate = 0;
  previousWeight = 0;
  previousRawWeight = 0;
  previousWeightTime = millis();

  // Reset Phase Logic
  brewStartTime = millis();
  phaseStartTime = brewStartTime;
  currentPhaseNum = 1;
  lastTelemetryTime = 0;

  // Initialize History Count
  histCount = 0;

  setValve(true);
  currentState = EXTRACTION;
}

void updateBrewCycle() {
  if (currentState != EXTRACTION && currentState != DONE)
    return;

  unsigned long now = millis();
  float dt = (now - previousWeightTime) / 1000.0;

  // EMA Flow Calculation
  if (dt >= FLOW_CALC_INTERVAL_S &&
      (currentState == EXTRACTION || currentState == DONE)) {
    float instantFlow = (rawCurrentWeight - previousRawWeight) / dt;
    if (isnan(instantFlow) || isinf(instantFlow) || instantFlow < 0)
      instantFlow = 0;
    currentFlowRate = (currentFlowRate * (1.0 - EMA_ALPHA_FLOW)) +
                      (instantFlow * EMA_ALPHA_FLOW);
    if (isnan(currentFlowRate) || isinf(currentFlowRate))
      currentFlowRate = 0;
    previousRawWeight = rawCurrentWeight;
    previousWeightTime = now;
  }

  // Global Target Exit Condition
  if (currentState == EXTRACTION &&
      currentWeight >=
          ((coffeeWeight * ratio) - (currentFlowRate * flowStopFactor * 0.5))) {
    stopShot();
    return;
  }

  float totalElapsed = (now - brewStartTime) / 1000.0;
  float targetPower = 0;
  float targetFlow = 0;
  float logPower = 0; // The power logged into telemetry

  // Phase Execution Logic
  if (currentState == EXTRACTION) {
    float phaseElapsed = (now - phaseStartTime) / 1000.0;
    bool isFlowMode = false;

    // --- PHASE 1: PREINFUSION ---
    if (currentPhaseNum == 1) {
      isFlowMode = false;
      float progress =
          (currentProfile.phase1.time > 0)
              ? min(1.0f, phaseElapsed / currentProfile.phase1.time)
              : 1.0f;
      targetPower =
          currentProfile.phase1.start +
          (currentProfile.phase1.end - currentProfile.phase1.start) * progress;
      setPumpPercentage((int)targetPower);

      // Exit logic
      if (currentWeight >= currentProfile.phase1.exitWeight ||
          (currentProfile.phase1.exitTime > 0 &&
           phaseElapsed >= currentProfile.phase1.exitTime)) {
        currentPhaseNum = 2;
        phaseStartTime = now;
      }
    }
    // --- PHASE 2: HOLD / BLOOM ---
    else if (currentPhaseNum == 2) {
      isFlowMode = (currentProfile.phase2.mode == "flow");
      float progress =
          (currentProfile.phase2.time > 0)
              ? min(1.0f, phaseElapsed / currentProfile.phase2.time)
              : 1.0f;

      if (isFlowMode) {
        targetFlow = currentProfile.phase2.start +
                     (currentProfile.phase2.end - currentProfile.phase2.start) *
                         progress;
        if (currentFlowRate < targetFlow)
          setPumpPercentage(min((int)currentPumpPercentage + 2, 100));
        else if (currentFlowRate > targetFlow)
          setPumpPercentage(max((int)currentPumpPercentage - 2, 1));
      } else {
        targetPower =
            currentProfile.phase2.start +
            (currentProfile.phase2.end - currentProfile.phase2.start) *
                progress;
        setPumpPercentage((int)targetPower);
      }

      // Exit logic
      if (currentWeight >= currentProfile.phase2.exitWeight ||
          (currentProfile.phase2.exitTime > 0 &&
           phaseElapsed >= currentProfile.phase2.exitTime)) {
        currentPhaseNum = 3;
        phaseStartTime = now;
      }
    }
    // --- PHASE 3: EXTRACTION TAPERING ---
    else if (currentPhaseNum == 3) {
      isFlowMode = (currentProfile.phase3.mode == "flow");
      float progress =
          (currentProfile.phase3.time > 0)
              ? min(1.0f, phaseElapsed / currentProfile.phase3.time)
              : 1.0f;

      if (isFlowMode) {
        targetFlow = currentProfile.phase3.start +
                     (currentProfile.phase3.end - currentProfile.phase3.start) *
                         progress;
        if (currentFlowRate < targetFlow)
          setPumpPercentage(min((int)currentPumpPercentage + 2, 100));
        else if (currentFlowRate > targetFlow)
          setPumpPercentage(max((int)currentPumpPercentage - 2, 1));
      } else {
        targetPower =
            currentProfile.phase3.start +
            (currentProfile.phase3.end - currentProfile.phase3.start) *
                progress;
        setPumpPercentage((int)targetPower);
      }
    }
    logPower = currentPumpPercentage;
  } else if (currentState == DONE) {
    targetPower = 0;
    targetFlow = 0;
    logPower = 0;
  }

  // --- TELEMETRY & GRAPH UPDATING (5Hz) ---
  // Broadcasts during EXTRACTION and DONE phases
  if (now - lastTelemetryTime >= 200) {
    lastTelemetryTime = now;

    // Update Live UI
    broadcastTelemetry(totalElapsed, logPower, targetPower, targetFlow,
                       currentFlowRate, currentTemp, currentWeight);

    // Save to History Memory
    if (histCount < MAX_TELEMETRY_POINTS) {
      histTime[histCount] = totalElapsed;
      histTargetP[histCount] = targetPower;
      histTargetF[histCount] = targetFlow;
      histPower[histCount] = logPower;
      histFlow[histCount] = currentFlowRate;
      histTemp[histCount] = currentTemp;
      histCount++;
    }
    Serial.printf("Shot Time: %.1fs | State: %s | Free Heap: %u\n",
                  totalElapsed, (currentState == EXTRACTION) ? "EXT" : "DONE",
                  ESP.getFreeHeap());
  }

  // After the post-shot drip delay finishes, officially exit to home/warming
  if (currentState == DONE &&
      (millis() - doneStartTime >= DRIP_CATCH_DELAY_MS)) {
    finishShot();
  }
}

void stopShot() {
  setPump(false);
  pumpDimmer.setPower(100); // Let the pump 100% while espresso not wrewing to
                            // ensure manual operation of the pump
  setValve(false);
  lastShotTime = (millis() - brewStartTime) / 1000.0;
  doneStartTime = millis();
  currentState = DONE;
}

void finishShot() {
  lastShotWeight = currentWeight;
  lastShotRatio = lastShotWeight / coffeeWeight;

  prefs.putFloat("lShtWght", lastShotWeight);
  prefs.putFloat("lShtTime", lastShotTime);
  prefs.putFloat("lShtRatio", lastShotRatio);
  prefs.putString("lastProfId", availableProfiles[selectedProfileIndex]);

  // Send shot data to LittleFS History
  saveShotToHistory(currentProfile.name, coffeeWeight, lastShotWeight,
                    lastShotTime);

  warmingOverridden = false;
  currentState = (currentTemp >= (pidSet - TEMP_MARGIN)) ? HOME : WARMING;
  encoder.setCount(coffeeWeight * 40);
  lastActivityTime = millis();
}

void handleButtonLogic() {
  if (isButtonPressed && !isLongPressHandled &&
      (millis() - pressTime > LONG_PRESS_TIMING)) {
    isLongPressHandled = true;
    handleLongPress();
  }
  if (newClickAvailable) {
    newClickAvailable = false;
    handleShortClick();
  }
}

void handleShortClick() {
  switch (currentState) {
  case WARMING:
    warmingOverridden = true;
    currentState = HOME;
    encoder.setCount(coffeeWeight * 40);
    break;
  case HOME:
    prefs.putFloat("dCoffeeWeight", coffeeWeight);
    currentState = SET_RATIO;
    encoder.setCount(ratio * 40);
    break;
  case SET_RATIO:
    prefs.putFloat("dRatio", ratio);
    currentState = BREW_PROMPT;
    break;
  case BREW_PROMPT:
    startBrew();
    break;
  case EXTRACTION:
    stopShot();
    break;
  case DONE:
    finishShot();
    break;
  case SETTINGS:
    if (activeSettings[currentSettingIndex] == EXIT_MENU) {
      warmingOverridden = false;
      currentState = (currentTemp >= (pidSet - TEMP_MARGIN)) ? HOME : WARMING;
      encoder.setCount(coffeeWeight * 40);
    } else if (activeSettings[currentSettingIndex] == SCALE_MODE) {
      isTaring = true;
      tareScales();
      isTaring = false;
    } else {
      isEditingValue = !isEditingValue;
      if (!isEditingValue) {
        SettingItem item = activeSettings[currentSettingIndex];
        // Only save global values that exist in this simplified menu
        if (item == PROFILE_SEL)
          prefs.putString("lastProfId",
                          availableProfiles[selectedProfileIndex]);
        if (item == FLOW_FACT)
          prefs.putFloat("dflowStopFactor", flowStopFactor);
        if (item == TGT_TEMP)
          prefs.putFloat("tTemp", targetTemp);
        if (item == TEMP_OFF)
          prefs.putFloat("dTempOffset", tempOffset);

        encoder.setCount(currentSettingIndex * 4);
      } else {
        SettingItem item = activeSettings[currentSettingIndex];
        if (item == PROFILE_SEL)
          encoder.setCount(selectedProfileIndex * 4);
        if (item == FLOW_FACT)
          encoder.setCount(flowStopFactor * 40);
        if (item == TGT_TEMP)
          encoder.setCount(targetTemp * 4);
        if (item == TEMP_OFF)
          encoder.setCount(tempOffset * 40);
      }
    }
    break;
  }
}

void handleLongPress() {
  if (currentState == WARMING || currentState == HOME || currentState == DONE) {
    if (currentState == DONE)
      finishShot();

    refreshProfileList(); // Reload the JSON file list before entering menu

    currentState = SETTINGS;
    currentSettingIndex = 0;
    encoder.setCount(currentSettingIndex * 4);
    isEditingValue = false;
  } else {
    // If we are currently brewing, stop the hardware and finish the shot first
    if (currentState == EXTRACTION) {
      stopShot();
      finishShot();
    } else {
      // For SET_RATIO or BREW_PROMPT, just go back home
      warmingOverridden = false;
      currentState = (currentTemp >= (pidSet - TEMP_MARGIN)) ? HOME : WARMING;
      encoder.setCount(coffeeWeight * 40);
    }
  }
}

void handleEncoderLogic() {
  long rawCount = encoder.getCount();

  switch (currentState) {
  case HOME:
    coffeeWeight = rawCount / 40.0;
    if (coffeeWeight < 0) {
      coffeeWeight = 0;
      encoder.setCount(0);
    }
    break;

  case SET_RATIO:
    ratio = rawCount / 40.0;
    if (ratio < 0) {
      ratio = 0;
      encoder.setCount(0);
    }
    break;

  case SETTINGS: {
    long menuCount = rawCount / 4;
    if (!isEditingValue) {
      currentSettingIndex =
          (menuCount % activeSettingsCount + activeSettingsCount) %
          activeSettingsCount;
    } else {
      SettingItem item = activeSettings[currentSettingIndex];

      if (item == TGT_TEMP)
        targetTemp = menuCount;
      if (item == FLOW_FACT)
        flowStopFactor = rawCount / 40.0f;
      if (item == TEMP_OFF)
        tempOffset = rawCount / 40.0f;

      if (item == PROFILE_SEL) {
        if (numAvailableProfiles > 0) {
          selectedProfileIndex =
              (menuCount % numAvailableProfiles + numAvailableProfiles) %
              numAvailableProfiles;
          // Load the newly selected profile into RAM dynamically
          loadProfile(availableProfiles[selectedProfileIndex], currentProfile);
        }
      }
    }
    break;
  }
  default:
    break;
  }
}