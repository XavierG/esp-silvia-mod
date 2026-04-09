#include "Display.h"
#include "Config.h"
#include "Globals.h"
#include "HeatIcon54X45.h"

// --- Private Helper Functions ---
void renderHeader() {
  u8g2.setFont(u8g2_font_helvR12_tf);
  int displayTemp = round(currentTemp - tempOffset);

  String phaseStr = "";
  switch (currentState) {
  case SETTINGS:
    phaseStr = "Settings";
    break;
  case HOME:
    phaseStr = "Dose";
    break;
  case SET_RATIO:
    phaseStr = "Ratio";
    break;
  case BREW_PROMPT:
    phaseStr = "Target";
    break;
  case EXTRACTION:
    if (currentBrewPhase == SOAK)
      phaseStr = "Soak";
    else if (currentBrewPhase == BLOOM_PHASE)
      phaseStr = "Bloom";
    else
      phaseStr = "Extract";
    break;
  case DONE:
    phaseStr = "Done";
    break;
  default:
    break;
  }
  u8g2.drawStr(0, 12, phaseStr.c_str());

  if (currentState != WARMING) {
    u8g2.setFont(u8g2_font_helvR12_tf);
    String tempStr = String(displayTemp) + "\xb0" + "C";
    u8g2.drawStr(128 - u8g2.getStrWidth(tempStr.c_str()) - 0, 12,
                 tempStr.c_str());
  }
}

void renderWarming() {
  u8g2.drawBitmap(5, 9, 7, 45, heat_icon_bitmap_54X45);
  u8g2.setFont(u8g2_font_luBS19_tf); // u8g2_font_luBS19_tf
  int displayTemp = round(currentTemp - tempOffset);
  String tempStr = String(displayTemp) + " \xb0" + "C";
  u8g2.drawStr(57, 40, tempStr.c_str());
}

void renderHome() {
  u8g2.setFont(u8g2_font_luBS19_tf);
  String val = String(coffeeWeight, 1) + " g";
  u8g2.drawStr((128 - u8g2.getStrWidth(val.c_str())) / 2, 43, val.c_str());

  u8g2.setFont(u8g2_font_helvR08_tf);
  String lastShotStr = "- none -";
  switch (lastProfile) {
  case PROFILE_FLAT:
    lastProfileStr = "Flat";
    break;
  case PROFILE_BLOOMING:
    lastProfileStr = "Blooming";
    break;
  case PROFILE_LONDINIUM:
    lastProfileStr = "Londinium";
    break;
  case PROFILE_SLAYER:
    lastProfileStr = "Slayer";
    break;
  }
  if (lastShotWeight > 0.1)
    lastShotStr = lastProfileStr + " - " + String(lastShotWeight, 1) + "g - " +
                  String(lastShotRatio, 1) + " - " +
                  String(round(lastShotTime), 0) + "s";
  u8g2.drawStr((128 - u8g2.getStrWidth(lastShotStr.c_str())) / 2, 61,
               lastShotStr.c_str());
}

void renderSetRatio() {
  u8g2.setFont(u8g2_font_luBS19_tf);
  String val = "1:" + String(ratio, 1);
  u8g2.drawStr((128 - u8g2.getStrWidth(val.c_str())) / 2, 43, val.c_str());

  u8g2.setFont(u8g2_font_helvR08_tf); // u8g2_font_helvR08_tf
  switch (selectedProfile) {
  case PROFILE_FLAT:
    currentProfileStr = "Flat";
    break;
  case PROFILE_BLOOMING:
    currentProfileStr = "Blooming";
    break;
  case PROFILE_LONDINIUM:
    currentProfileStr = "Londinium";
    break;
  case PROFILE_SLAYER:
    currentProfileStr = "Slayer";
    break;
  }
  u8g2.drawStr((128 - u8g2.getStrWidth(currentProfileStr.c_str())) / 2, 61,
               currentProfileStr.c_str());
}

void renderBrewPrompt() {
  u8g2.setFont(u8g2_font_luBS19_tf);
  String tgt = String(coffeeWeight * ratio, 1) + " g";
  u8g2.drawStr((128 - u8g2.getStrWidth(tgt.c_str())) / 2, 43, tgt.c_str());

  u8g2.setFont(u8g2_font_helvR08_tf);
  u8g2.drawStr((128 - u8g2.getStrWidth("Click to Start")) / 2, 61,
               "Click to Start");
}

void renderProgressBar(float percent) {
  u8g2.drawRFrame(0, 54, 128, 8, 3);
  int barWidth = (int)(124.0f * constrain(percent, 0.0f, 1.0f));
  if (barWidth > 0) {
    u8g2.drawBox(2, 56, barWidth, 4);
  }
}

void renderExtraction() {
  // Current weight
  u8g2.setFont(u8g2_font_luBS19_tf); // u8g2_font_h_elvB18_tf
  String wStr = String(currentWeight, 0) + " g";
  u8g2.drawStr(0, 41, wStr.c_str());

  // Pump Percentage Vertical Line
  int lineHeight = 35;

  int fillHeight =
      (int)((lineHeight) * ((currentPumpPercentage - MIN_PUMP_PERCENTAGE) /
                            (100.0f - MIN_PUMP_PERCENTAGE)));
  if (fillHeight > 0) {
    u8g2.drawLine(72, 50 - fillHeight, 72, 50);
  }

  // Timer & Flow Rate
  u8g2.setFont(u8g2_font_helvR12_tf);
  String tStr = String((millis() - brewStartTime) / 1000.0, 0) + " s";
  u8g2.drawStr(128 - u8g2.getStrWidth(tStr.c_str()), 31, tStr.c_str());
  String fStr = String(currentFlowRate, 1) + " g/s";
  u8g2.drawStr(128 - u8g2.getStrWidth(fStr.c_str()), 48, fStr.c_str());

  // Progress bar
  float targetW = coffeeWeight * ratio;
  renderProgressBar(currentWeight / targetW);
}

void renderDone() {
  // Final weight
  u8g2.setFont(u8g2_font_luBS19_tf);
  String resWStr = String(currentWeight, 1) + " g";
  u8g2.drawStr((128 - u8g2.getStrWidth(resWStr.c_str())) / 2, 43,
               resWStr.c_str());

  // Final time
  u8g2.setFont(u8g2_font_helvR08_tf);
  String resTStr = "in " + String(lastShotTime, 0) + "s";
  u8g2.drawStr((128 - u8g2.getStrWidth(resTStr.c_str())) / 2, 61,
               resTStr.c_str());
}

void renderSettings() {
  static int firstItem = 0;
  const int visibleCount = 4;
  const int totalItems = activeSettingsCount;

  if (currentSettingIndex < firstItem)
    firstItem = currentSettingIndex;
  else if (currentSettingIndex >= firstItem + visibleCount)
    firstItem = currentSettingIndex - (visibleCount - 1);
  if (firstItem > totalItems - visibleCount)
    firstItem = totalItems - visibleCount;
  if (firstItem < 0)
    firstItem = 0;

  for (int i = 0; i < visibleCount; i++) {
    int itemIdx = firstItem + i;
    if (itemIdx >= totalItems)
      break;

    SettingItem item = activeSettings[itemIdx];
    int yPos = 26 + (i * 12);

    if (itemIdx == currentSettingIndex) {
      u8g2.setDrawColor(1);
      u8g2.drawBox(0, yPos - 10, 128, 12);
      u8g2.setDrawColor(0);
    } else {
      u8g2.setDrawColor(1);
    }

    u8g2.setFont(u8g2_font_helvR08_tf);
    String label = "", val = "";

    switch (item) {
    case PROFILE_SEL:
      label = "Profile";
      if (selectedProfile == PROFILE_FLAT)
        val = "Flat";
      else if (selectedProfile == PROFILE_BLOOMING)
        val = "Blooming";
      else if (selectedProfile == PROFILE_SLAYER)
        val = "Slayer";
      else
        val = "Londinium";
      break;
    case SOAK_PWR:
      label = "Soak Power";
      if (selectedProfile == PROFILE_FLAT)
        val = String(flatSoakPower) + " %";
      else if (selectedProfile == PROFILE_BLOOMING)
        val = String(bloomSoakPower) + " %";
      else if (selectedProfile == PROFILE_LONDINIUM)
        val = String(londSoakPower) + " %";
      else if (selectedProfile == PROFILE_SLAYER)
        val = String(slaySoakPower) + " %";
      break;
    case SOAK_WGHT:
      label = "Soak Wght";
      if (selectedProfile == PROFILE_FLAT)
        val = String(flatSoakWeight, 1) + " g";
      else if (selectedProfile == PROFILE_BLOOMING)
        val = String(bloomSoakWeight, 1) + " g";
      else if (selectedProfile == PROFILE_LONDINIUM)
        val = String(londSoakWeight, 1) + " g";
      else if (selectedProfile == PROFILE_SLAYER)
        val = String(slaySoakWeight, 1) + " g";
      break;
    case BLOOM:
      label = "Bloom Time";
      val = String(bloomTime) + " s";
      break;
    case TGT_FLOW:
      label = "Target Flow";
      if (selectedProfile == PROFILE_FLAT)
        val = String(flatTargetFlow, 1) + " g/s";
      else if (selectedProfile == PROFILE_BLOOMING)
        val = String(bloomTargetFlow, 1) + " g/s";
      else if (selectedProfile == PROFILE_SLAYER)
        val = String(slayTargetFlow, 1) + " g/s";
      break;
    case START_FLOW:
      label = "Start Flow";
      val = String(londStartFlow, 1) + " g/s";
      break;
    case END_FLOW:
      label = "End Flow";
      val = String(londEndFlow, 1) + " g/s";
      break;
    case FLOW_FACT:
      label = "Stop Factor";
      val = String(flowStopFactor, 1) + " s";
      break;
    case TGT_TEMP:
      label = "Target Temp";
      val = String((int)targetTemp) + " \xb0" + "C";
      break;
    case TEMP_OFF:
      label = "Temp Offset";
      val = String(tempOffset, 1) + " \xb0" + "C";
      break;
    case SCALE_MODE:
      label = "Scale Mode";
      if (isTaring || (millis() - lastScaleReadTime > 1000)) {
        val = "Wait...";
      } else {
        val = String(currentWeight, 1) + " g";
      }
      break;
    case EXIT_MENU:
      label = "Exit Menu";
      break;
    }

    u8g2.drawStr(2, yPos, label.c_str());
    if (itemIdx == currentSettingIndex && isEditingValue)
      val = "[" + val + "]";
    u8g2.drawStr(124 - u8g2.getStrWidth(val.c_str()), yPos, val.c_str());
  }
  u8g2.setDrawColor(1);
}

void renderSensorError() {
  u8g2.setFont(u8g2_font_helvB08_tf);
  u8g2.drawStr((128 - u8g2.getStrWidth("SENSOR ERROR")) / 2, 30,
               "SENSOR ERROR");
  u8g2.setFont(u8g2_font_helvR08_tf);
  u8g2.drawStr((128 - u8g2.getStrWidth("Check Thermocouple!")) / 2, 45,
               "Check Thermocouple!");
}

// --- Public Entry Point ---
void initDisplay() {
  u8g2.begin();
  unsigned long splashStart = millis();

  u8g2.clearBuffer();
  u8g2.drawBitmap(37, 2, 7, 45, heat_icon_bitmap_54X45);
  u8g2.setFont(u8g2_font_helvR14_tf);
  u8g2.drawStr((128 - u8g2.getStrWidth("Coffee Time")) / 2, 64, "Coffee Time");
  u8g2.sendBuffer();

  while (millis() - splashStart < SPLASH_SCREEN_DELAY_MS) {
    delay(10);
  }
}

void renderUI() {
  u8g2.clearBuffer();

  renderHeader();

  switch (currentState) {
  case WARMING:
    renderWarming();
    break;
  case HOME:
    renderHome();
    break;
  case SET_RATIO:
    renderSetRatio();
    break;
  case BREW_PROMPT:
    renderBrewPrompt();
    break;
  case EXTRACTION:
    renderExtraction();
    break;
  case DONE:
    renderDone();
    break;
  case SETTINGS:
    renderSettings();
    break;
  case SENSOR_ERROR:
    renderSensorError();
    break;
  }

  u8g2.sendBuffer();
}