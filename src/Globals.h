#pragma once

#include <Adafruit_MAX31855.h>
#include <Arduino.h>
#include <ESP32Encoder.h>
#include <HX711.h>
#include <Preferences.h>
#include <QuickPID.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <Wire.h>


#include "Config.h"

// --- Enums ---
enum State {
  WARMING,
  HOME,
  SET_RATIO,
  BREW_PROMPT,
  EXTRACTION,
  DONE,
  SETTINGS,
  SENSOR_ERROR
};
enum SettingItem {
  PROFILE_SEL,
  SOAK_PWR,
  SOAK_WGHT,
  BLOOM,
  TGT_FLOW,
  START_FLOW,
  END_FLOW,
  FLOW_FACT,
  TGT_TEMP,
  TEMP_OFF,
  SCALE_MODE,
  EXIT_MENU
};
enum BrewPhase { SOAK, BLOOM_PHASE, EXTRACT };
enum BrewProfile {
  PROFILE_FLAT,
  PROFILE_BLOOMING,
  PROFILE_LONDINIUM,
  PROFILE_SLAYER
};

// --- Hardware Objects ---
extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern HX711 scaleA, scaleB;
extern Adafruit_MAX31855 thermo;
extern ESP32Encoder encoder;
extern Preferences prefs;
extern QuickPID myPID;

// --- PID Variables ---
extern float pidIn, pidOut, pidSet;
extern unsigned long windowStartTime;
extern float smoothedTemp;

// --- Scale Variables ---
extern volatile unsigned long lastScaleReadTime;
extern volatile bool isTaring;

// --- Settings ---
extern float targetTemp, tempOffset, coffeeWeight, ratio, flowStopFactor;
extern int bloomTime;

// --- Profile Variables ---
extern int flatSoakPower, bloomSoakPower, londSoakPower, slaySoakPower;
extern float flatSoakWeight, bloomSoakWeight, londSoakWeight, slaySoakWeight;
extern float flatTargetFlow, bloomTargetFlow, slayTargetFlow;
extern float londStartFlow, londEndFlow;

extern SettingItem activeSettings[15];
extern int activeSettingsCount;
extern int currentSettingIndex;

// --- Runtime State ---
extern State currentState;
extern bool isEditingValue;
extern uint8_t currentPumpPercentage;
extern BrewPhase currentBrewPhase;
extern BrewProfile selectedProfile, lastProfile;
extern float currentTemp, currentWeight, rawCurrentWeight, currentFlowRate,
    previousWeight, previousRawWeight;
extern unsigned long brewStartTime, doneStartTime, previousWeightTime,
    bloomStartTime, extractStartTime;
extern float lastShotWeight, lastShotTime, lastShotRatio;
extern String currentProfileStr, lastProfileStr;
extern unsigned long lastUpdate;
extern bool warmingOverridden;
extern float offsetA, offsetB;

// --- Screensaver Variables ---
extern unsigned long lastActivityTime;
extern bool isScreenAsleep;
extern long lastEncoderCount;

// --- Interrupt & Input Variables ---
extern volatile unsigned long pressTime, releaseTime;
extern volatile bool isButtonPressed, newClickAvailable, isLongPressHandled;
