#include "Globals.h"

// --- Hardware Objects Initialization ---
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
HX711 scaleA, scaleB;
Adafruit_MAX31855 thermo(MAX_CS);
ESP32Encoder encoder;
Preferences prefs;

// --- PID Variables Initialization ---
float pidIn = 0, pidOut = 0, pidSet = 0;
QuickPID myPID(&pidIn, &pidOut, &pidSet, 90.9, 1.51, 1363.5,
               QuickPID::Action::direct);
unsigned long windowStartTime = 0;
float smoothedTemp = 0;

// --- Scale Variables Initialization ---
volatile unsigned long lastScaleReadTime = 0;
volatile bool isTaring = false;

// --- Settings Initialization ---
float targetTemp = DEFAULT_TARGET_TEMP;
float tempOffset = DEFAULT_TEMP_OFFSET;
float coffeeWeight = DEFAULT_COFFEE_WEIGHT;
float ratio = DEFAULT_RATIO;
int bloomTime = DEFAULT_BLOOM_TIME;
float flowStopFactor = DEFAULT_FLOW_STOP_FACTOR;

int flatSoakPower = DEFAULT_SOAK_POWER;
int bloomSoakPower = DEFAULT_SOAK_POWER;
int londSoakPower = DEFAULT_SOAK_POWER;
int slaySoakPower = DEFAULT_SOAK_POWER;

float flatSoakWeight = DEFAULT_SOAK_WEIGHT;
float bloomSoakWeight = DEFAULT_SOAK_WEIGHT;
float londSoakWeight = DEFAULT_SOAK_WEIGHT;
float slaySoakWeight = DEFAULT_SOAK_WEIGHT;

float flatTargetFlow = DEFAULT_TARGET_FLOW;
float bloomTargetFlow = DEFAULT_TARGET_FLOW;
float slayTargetFlow = DEFAULT_TARGET_FLOW;

float londStartFlow = DEFAULT_START_FLOW;
float londEndFlow = DEFAULT_END_FLOW;

SettingItem activeSettings[15];
int activeSettingsCount = 0;
int currentSettingIndex = 0;

// --- Runtime State Initialization ---
State currentState = WARMING;
bool isEditingValue = false;
uint8_t currentPumpPercentage = 0;

float currentTemp = 0;
float currentWeight = 0;
float rawCurrentWeight = 0;
float currentFlowRate = 0;
float previousWeight = 0;
float previousRawWeight = 0;
unsigned long previousWeightTime = 0;
unsigned long brewStartTime = 0;
unsigned long doneStartTime = 0;
unsigned long bloomStartTime = 0;
unsigned long extractStartTime = 0;
BrewPhase currentBrewPhase = SOAK;
BrewProfile selectedProfile = PROFILE_FLAT;
BrewProfile lastProfile = PROFILE_FLAT;
float lastShotWeight = 0, lastShotTime = 0, lastShotRatio = 0;
String currentProfileStr = "", lastProfileStr = "";
unsigned long lastUpdate = 0;
bool warmingOverridden = false;
float offsetA = 0.0, offsetB = 0.0;

// --- Screensaver Initialization ---
unsigned long lastActivityTime = 0;
bool isScreenAsleep = false;
long lastEncoderCount = 0;

// --- Interrupt Variables Initialization ---
volatile unsigned long pressTime = 0;
volatile unsigned long releaseTime = 0;
volatile bool isButtonPressed = false;
volatile bool newClickAvailable = false;
volatile bool isLongPressHandled = false;