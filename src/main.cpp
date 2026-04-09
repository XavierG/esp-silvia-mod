/* Todo:

Current:
- Pump dimmer
- why do I have the warm up screen after the splashscreen even if temp ok
- Which power supply?
- Check the nuber of available gpio

Before Production:
- Test implementation of the FORCE_HEATER_TIME and FORCE_HEATER_PRETIME variable
- Update scale initials parameters / put them in define
- Update refresh rate
- Update the line:   vTaskDelay(pdMS_TO_TICKS(5)); to 1 only to avoid slow
computation
- Integrate PID close to XMP7100 values
https://gemini.google.com/app/a45d001951bd94d5


Roadmap:
- Water level sensor
- Possible to migrate to a touch screen?
- Web interface
- Integration HA for machine readiness notification
- Store the last 5 shots in memory as an history? Available in the settings as
an entry?

*/

#include "Display.h"
#include "Globals.h"
#include "Hardware.h"

// --- Forward Declarations ---
void loadSettings();
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

  // Yield to FreeRTOS (Crucial for Wokwi Simulation & Watchdog Timer)
  vTaskDelay(pdMS_TO_TICKS(5));
}

// --- Logic Implementation ---

void updateActiveSettings() {
  activeSettingsCount = 0;
  activeSettings[activeSettingsCount++] = PROFILE_SEL;

  if (selectedProfile == PROFILE_FLAT) {
    activeSettings[activeSettingsCount++] = SOAK_PWR;
    activeSettings[activeSettingsCount++] = SOAK_WGHT;
    activeSettings[activeSettingsCount++] = TGT_FLOW;
  } else if (selectedProfile == PROFILE_BLOOMING) {
    activeSettings[activeSettingsCount++] = SOAK_PWR;
    activeSettings[activeSettingsCount++] = SOAK_WGHT;
    activeSettings[activeSettingsCount++] = BLOOM;
    activeSettings[activeSettingsCount++] = TGT_FLOW;
  } else if (selectedProfile == PROFILE_LONDINIUM) {
    activeSettings[activeSettingsCount++] = SOAK_PWR;
    activeSettings[activeSettingsCount++] = SOAK_WGHT;
    activeSettings[activeSettingsCount++] = START_FLOW;
    activeSettings[activeSettingsCount++] = END_FLOW;
  } else if (selectedProfile == PROFILE_SLAYER) {
    activeSettings[activeSettingsCount++] = SOAK_PWR;
    activeSettings[activeSettingsCount++] = SOAK_WGHT;
    activeSettings[activeSettingsCount++] = TGT_FLOW;
  }

  activeSettings[activeSettingsCount++] = FLOW_FACT;
  activeSettings[activeSettingsCount++] = TGT_TEMP;
  activeSettings[activeSettingsCount++] = TEMP_OFF;
  activeSettings[activeSettingsCount++] = SCALE_MODE;
  activeSettings[activeSettingsCount++] = EXIT_MENU;

  if (currentSettingIndex >= activeSettingsCount) {
    currentSettingIndex = 0;
  }
}

void loadSettings() {
  prefs.begin("silvia", false);
  targetTemp = prefs.getFloat("tTemp", DEFAULT_TARGET_TEMP);
  tempOffset = prefs.getFloat("dTempOffset", DEFAULT_TEMP_OFFSET);
  coffeeWeight = prefs.getFloat("dCoffeeWeight", DEFAULT_COFFEE_WEIGHT);
  ratio = prefs.getFloat("dRatio", DEFAULT_RATIO);
  selectedProfile = (BrewProfile)prefs.getInt("profileSel", PROFILE_FLAT);
  bloomTime = prefs.getInt("dBloomTime", DEFAULT_BLOOM_TIME);
  flowStopFactor = prefs.getFloat("dflowStopFactor", DEFAULT_FLOW_STOP_FACTOR);

  flatSoakPower = prefs.getInt("fSPwr", DEFAULT_SOAK_POWER);
  flatSoakWeight = prefs.getFloat("fSWgt", DEFAULT_SOAK_WEIGHT);
  flatTargetFlow = prefs.getFloat("fTFlw", DEFAULT_TARGET_FLOW);

  bloomSoakPower = prefs.getInt("bSPwr", DEFAULT_SOAK_POWER);
  bloomSoakWeight = prefs.getFloat("bSWgt", DEFAULT_SOAK_WEIGHT);
  bloomTargetFlow = prefs.getFloat("bTFlw", DEFAULT_TARGET_FLOW);

  londSoakPower = prefs.getInt("lSPwr", DEFAULT_SOAK_POWER);
  londSoakWeight = prefs.getFloat("lSWgt", DEFAULT_SOAK_WEIGHT);
  londStartFlow = prefs.getFloat("lSFlw", DEFAULT_START_FLOW);
  londEndFlow = prefs.getFloat("lEFlw", DEFAULT_END_FLOW);

  slaySoakPower = prefs.getInt("slSPwr", DEFAULT_SOAK_POWER);
  slaySoakWeight = prefs.getFloat("slSWgt", DEFAULT_SOAK_WEIGHT);
  slayTargetFlow = prefs.getFloat("slTFlw", DEFAULT_TARGET_FLOW);

  lastShotWeight = prefs.getFloat("lShtWght", 0.0);
  lastShotTime = prefs.getFloat("lShtTime", 0.0);
  lastShotRatio = prefs.getFloat("lShtRatio", 0.0);
  lastProfile = (BrewProfile)prefs.getInt("lShtProfile", PROFILE_FLAT);

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

void updateBrewCycle() {
  if (currentState != EXTRACTION && currentState != DONE)
    return;

  unsigned long now = millis();
  float dt = (now - previousWeightTime) / 1000.0;
  if (dt >= FLOW_CALC_INTERVAL_S && currentState == EXTRACTION) {
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

  if (currentState == EXTRACTION &&
      currentWeight >=
          ((coffeeWeight * ratio) - (currentFlowRate * flowStopFactor))) {
    stopShot();
    return;
  }

  if (currentState == EXTRACTION) {
    unsigned long elapsed = (now - brewStartTime) / 1000;

    int sPwr = 100;
    float sWgt = 0.5;
    if (selectedProfile == PROFILE_FLAT) {
      sPwr = flatSoakPower;
      sWgt = flatSoakWeight;
    } else if (selectedProfile == PROFILE_BLOOMING) {
      sPwr = bloomSoakPower;
      sWgt = bloomSoakWeight;
    } else if (selectedProfile == PROFILE_LONDINIUM) {
      sPwr = londSoakPower;
      sWgt = londSoakWeight;
    } else if (selectedProfile == PROFILE_SLAYER) {
      sPwr = slaySoakPower;
      sWgt = slaySoakWeight;
    }

    if (currentBrewPhase == SOAK) {
      setPumpPercentage(sPwr);
      if (currentWeight >= sWgt) {
        if (selectedProfile == PROFILE_BLOOMING) {
          currentBrewPhase = BLOOM_PHASE;
          bloomStartTime = now;
        } else {
          currentBrewPhase = EXTRACT;
          extractStartTime = now;
        }
      }
    }

    if (currentBrewPhase == BLOOM_PHASE) {
      setPumpPercentage(0);
      unsigned long elapsedBloom = (now - bloomStartTime) / 1000;
      if (elapsedBloom >= (unsigned long)bloomTime) {
        currentBrewPhase = EXTRACT;
        extractStartTime = now;
      }
    }

    if (currentBrewPhase == EXTRACT) {
      float targetFlow = 0;
      if (selectedProfile == PROFILE_FLAT)
        targetFlow = flatTargetFlow;
      else if (selectedProfile == PROFILE_BLOOMING)
        targetFlow = bloomTargetFlow;
      else if (selectedProfile == PROFILE_SLAYER)
        targetFlow = slayTargetFlow;
      else if (selectedProfile == PROFILE_LONDINIUM) {
        float progress = currentWeight / (coffeeWeight * ratio);
        targetFlow = londStartFlow - ((londStartFlow - londEndFlow) *
                                      constrain(progress, 0.0f, 1.0f));
      }

      if (currentFlowRate < targetFlow) {
        setPumpPercentage(min((int)currentPumpPercentage + 2, 100));
      } else if (currentFlowRate > targetFlow) {
        setPumpPercentage(
            max((int)currentPumpPercentage - 2, MIN_PUMP_PERCENTAGE));
      }
    }
  }

  if (currentState == DONE &&
      (millis() - doneStartTime >= DRIP_CATCH_DELAY_MS)) {
    finishShot();
  }
}

void startBrew() {
  if (scaleA.is_ready())
    offsetA = scaleA.get_units(1);
  if (scaleB.is_ready())
    offsetB = scaleB.get_units(1);

  currentWeight = 0;
  rawCurrentWeight = 0;
  currentFlowRate = 0;
  previousWeight = 0;
  previousRawWeight = 0;
  previousWeightTime = millis();

  brewStartTime = millis();
  bloomStartTime = 0;
  extractStartTime = 0;
  setValve(true);
  currentState = EXTRACTION;
  currentBrewPhase = SOAK;
}

void stopShot() {
  setPumpPercentage(0);
  setValve(false);
  lastShotTime = (millis() - brewStartTime) / 1000.0;
  doneStartTime = millis();
  currentState = DONE;
}

void finishShot() {
  lastShotWeight = currentWeight;
  lastShotRatio = lastShotWeight / coffeeWeight;
  lastProfile = selectedProfile;

  prefs.putFloat("lShtWght", lastShotWeight);
  prefs.putFloat("lShtTime", lastShotTime);
  prefs.putFloat("lShtRatio", lastShotRatio);
  prefs.putInt("lShtProfile", lastProfile);

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
    prefs.putFloat("dBeans", coffeeWeight);
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
      // Explicit Tare with UI Feedback
      isTaring = true;
      if (scaleA.is_ready())
        offsetA = scaleA.get_units(5); // 5 samples for a stable tare (~500ms)
      if (scaleB.is_ready())
        offsetB = scaleB.get_units(5);
      isTaring = false;
    } else {
      isEditingValue = !isEditingValue;
      if (!isEditingValue) {
        SettingItem item = activeSettings[currentSettingIndex];
        if (item == PROFILE_SEL)
          prefs.putInt("profileSel", selectedProfile);
        if (item == BLOOM)
          prefs.putInt("dBloomTime", bloomTime);
        if (item == SOAK_PWR) {
          if (selectedProfile == PROFILE_FLAT)
            prefs.putInt("fSPwr", flatSoakPower);
          else if (selectedProfile == PROFILE_BLOOMING)
            prefs.putInt("bSPwr", bloomSoakPower);
          else if (selectedProfile == PROFILE_LONDINIUM)
            prefs.putInt("lSPwr", londSoakPower);
          else if (selectedProfile == PROFILE_SLAYER)
            prefs.putInt("slSPwr", slaySoakPower);
        }
        if (item == SOAK_WGHT) {
          if (selectedProfile == PROFILE_FLAT)
            prefs.putFloat("fSWgt", flatSoakWeight);
          else if (selectedProfile == PROFILE_BLOOMING)
            prefs.putFloat("bSWgt", bloomSoakWeight);
          else if (selectedProfile == PROFILE_LONDINIUM)
            prefs.putFloat("lSWgt", londSoakWeight);
          else if (selectedProfile == PROFILE_SLAYER)
            prefs.putFloat("slSWgt", slaySoakWeight);
        }
        if (item == TGT_FLOW) {
          if (selectedProfile == PROFILE_FLAT)
            prefs.putFloat("fTFlw", flatTargetFlow);
          else if (selectedProfile == PROFILE_BLOOMING)
            prefs.putFloat("bTFlw", bloomTargetFlow);
          else if (selectedProfile == PROFILE_SLAYER)
            prefs.putFloat("slTFlw", slayTargetFlow);
        }
        if (item == START_FLOW)
          prefs.putFloat("lSFlw", londStartFlow);
        if (item == END_FLOW)
          prefs.putFloat("lEFlw", londEndFlow);

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
          encoder.setCount(selectedProfile * 4);
        if (item == BLOOM)
          encoder.setCount(bloomTime * 4);
        if (item == SOAK_PWR) {
          if (selectedProfile == PROFILE_FLAT)
            encoder.setCount(flatSoakPower * 4);
          else if (selectedProfile == PROFILE_BLOOMING)
            encoder.setCount(bloomSoakPower * 4);
          else if (selectedProfile == PROFILE_LONDINIUM)
            encoder.setCount(londSoakPower * 4);
          else if (selectedProfile == PROFILE_SLAYER)
            encoder.setCount(slaySoakPower * 4);
        }
        if (item == SOAK_WGHT) {
          if (selectedProfile == PROFILE_FLAT)
            encoder.setCount(flatSoakWeight * 40);
          else if (selectedProfile == PROFILE_BLOOMING)
            encoder.setCount(bloomSoakWeight * 40);
          else if (selectedProfile == PROFILE_LONDINIUM)
            encoder.setCount(londSoakWeight * 40);
          else if (selectedProfile == PROFILE_SLAYER)
            encoder.setCount(slaySoakWeight * 40);
        }
        if (item == TGT_FLOW) {
          if (selectedProfile == PROFILE_FLAT)
            encoder.setCount(flatTargetFlow * 40);
          else if (selectedProfile == PROFILE_BLOOMING)
            encoder.setCount(bloomTargetFlow * 40);
          else if (selectedProfile == PROFILE_SLAYER)
            encoder.setCount(slayTargetFlow * 40);
        }
        if (item == START_FLOW)
          encoder.setCount(londStartFlow * 40);
        if (item == END_FLOW)
          encoder.setCount(londEndFlow * 40);

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
      if (item == PROFILE_SEL) {
        selectedProfile = (BrewProfile)((menuCount % 4 + 4) % 4);
        updateActiveSettings();
      }
      if (item == BLOOM)
        bloomTime = menuCount;
      if (item == SOAK_PWR) {
        int pwr = max(10, min((int)menuCount, 100));
        if (pwr != menuCount)
          encoder.setCount(pwr * 4);
        if (selectedProfile == PROFILE_FLAT)
          flatSoakPower = pwr;
        else if (selectedProfile == PROFILE_BLOOMING)
          bloomSoakPower = pwr;
        else if (selectedProfile == PROFILE_LONDINIUM)
          londSoakPower = pwr;
        else if (selectedProfile == PROFILE_SLAYER)
          slaySoakPower = pwr;
      }
      if (item == SOAK_WGHT) {
        float wght = max(0.0f, rawCount / 40.0f);
        if (selectedProfile == PROFILE_FLAT)
          flatSoakWeight = wght;
        else if (selectedProfile == PROFILE_BLOOMING)
          bloomSoakWeight = wght;
        else if (selectedProfile == PROFILE_LONDINIUM)
          londSoakWeight = wght;
        else if (selectedProfile == PROFILE_SLAYER)
          slaySoakWeight = wght;
      }
      if (item == TGT_FLOW) {
        float flw = max(0.0f, rawCount / 40.0f);
        if (selectedProfile == PROFILE_FLAT)
          flatTargetFlow = flw;
        else if (selectedProfile == PROFILE_BLOOMING)
          bloomTargetFlow = flw;
        else if (selectedProfile == PROFILE_SLAYER)
          slayTargetFlow = flw;
      }
      if (item == START_FLOW)
        londStartFlow = max(0.0f, rawCount / 40.0f);
      if (item == END_FLOW)
        londEndFlow = max(0.0f, rawCount / 40.0f);

      if (item == FLOW_FACT)
        flowStopFactor = rawCount / 40.0f;
      if (item == TEMP_OFF)
        tempOffset = rawCount / 40.0f;
    }
    break;
  }
  default:
    break;
  }
}