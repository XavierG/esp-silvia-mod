#include "Hardware.h"
#include "Config.h"
#include "FelicitaScale.h"
#include "Globals.h"

dimmerLamp pumpDimmer(DIMMER_OUT_PIN, DIMMER_ZC_PIN);

// BLE Scale Instances
FelicitaScanner bleScanner;
FelicitaScale bleScale;
bool usingBleScale = false;

void IRAM_ATTR handleButtonInterrupt() {
  unsigned long now = millis();
  if (digitalRead(ENCODER_SW) == LOW) {
    if (now - releaseTime > 20) {
      pressTime = now;
      isButtonPressed = true;
      isLongPressHandled = false;
    }
  } else {
    isButtonPressed = false;
    releaseTime = now;
    if (!isLongPressHandled && (releaseTime - pressTime > 20)) {
      newClickAvailable = true;
    }
  }
}

void initHardware() {
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);

  pinMode(SSR_PUMP, OUTPUT);
  pinMode(SSR_VALVE, OUTPUT);
  pinMode(SSR_HEATER, OUTPUT);
  pinMode(ENCODER_SW, INPUT_PULLUP);
  pinMode(WATER_LEVEL_PIN, INPUT_PULLUP);

  pumpDimmer.begin(NORMAL_MODE, ON);
  pumpDimmer.setPower(
      100); // Set dimmer 100% by default to ensure manual operation of the pump

  attachInterrupt(digitalPinToInterrupt(ENCODER_SW), handleButtonInterrupt,
                  CHANGE);

  SPI.begin(SPI_CLK, SPI_MISO, SPI_MOSI, MAX_CS);
  delay(50);
  thermo.begin();

  // --- HX711_ADC Migration: Initialization ---
  scaleA.begin();
  scaleB.begin();

  // Pullup resistor to avoid crazy reading if no HX711 module connected.
  pinMode(HX_A_DOUT, INPUT_PULLUP);
  pinMode(HX_B_DOUT, INPUT_PULLUP);

  // start(stabilizing_time_ms, tare_on_boot)
  // We use 100ms and false so it doesn't block the UI/boot process
  // unnecessarily.
  scaleA.start(100, false);
  scaleB.start(100, false);

  // Set the calibration factors (replaces set_scale())
  scaleA.setCalFactor(SCALE_A);
  scaleB.setCalFactor(SCALE_B);

  bleScanner.startScan();

  ESP32Encoder::useInternalWeakPullResistors = UP;
  encoder.attachFullQuad(ENCODER_CLK, ENCODER_DT);

  windowStartTime = millis();
  myPID.SetOutputLimits(0, PID_WINDOW_SIZE_MS);
  myPID.SetMode(QuickPID::Control::automatic);
}

void waitForTemperatureSensor() {
  delay(500);
  currentTemp = thermo.readCelsius();
  uint8_t error = thermo.readError();
  unsigned long sensorWaitStart = millis();

  while (isnan(currentTemp) || error != 0) {
    if (millis() - sensorWaitStart > 5000) {
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_helvB08_tf);
      u8g2.drawStr((128 - u8g2.getStrWidth("Sensor Error!")) / 2, 35,
                   "Sensor Error!");
      u8g2.sendBuffer();
    }
    delay(500);
    currentTemp = thermo.readCelsius();
    error = thermo.readError();
  }
}

void setPump(bool state) { digitalWrite(SSR_PUMP, state ? HIGH : LOW); }

void setPumpPercentage(uint8_t percentage) {
  static unsigned long pumpStartTime = 0;
  static bool isPumping = false;

  if (percentage == 0) {
    setPump(false);
    pumpDimmer.setPower(0);
    currentPumpPercentage = 0;
    isPumping = false;
  } else {
    unsigned long now = millis();
    if (!isPumping) {
      isPumping = true;
      pumpStartTime = now;
      setPump(true);
    }

    unsigned long elapsed = now - pumpStartTime;
    uint8_t mappedTargetPower = map(percentage, 1, 100, minPumpPercentage, 100);

    if (elapsed < 200) {
      // Kickstart Phase: 100%
      pumpDimmer.setPower(100);
    } else if (elapsed < 400) {
      // Smoothing Phase: 200ms linear ramp down to mappedTargetPower
      float progress = (elapsed - 200) / 200.0f;
      uint8_t smoothedPower = 100 - (100 - mappedTargetPower) * progress;
      pumpDimmer.setPower(smoothedPower);
    } else {
      // Normal Operation
      pumpDimmer.setPower(mappedTargetPower);
    }
    currentPumpPercentage = percentage;
  }
}

void setValve(bool state) { digitalWrite(SSR_VALVE, state ? HIGH : LOW); }
void setHeater(bool state) { digitalWrite(SSR_HEATER, state ? HIGH : LOW); }

void tareScales() {
  if (usingBleScale) {
    bleScale.tare();
    unsigned long waitStart = millis();
    while (millis() - waitStart <
           1500) { // Check if scale is actually correctly tared before moving
                   // on (block brewing to see the weight of the cup)
      bleScale.update();
      if (millis() - waitStart > 250 && abs(bleScale.getWeight()) <= 0.2) {
        break;
      }
      delay(10);
    }
  } else {
    scaleA.tareNoDelay();
    scaleB.tareNoDelay();
  }
}

void updateWeight() {
  unsigned long now = millis();
  static unsigned long lastBleScanAttempt = 0;
  static unsigned long lastSeenConnected = 0;

  // 1. Throttled BLE Connection Logic
  if (!usingBleScale && (now - lastBleScanAttempt > 1000)) {
    lastBleScanAttempt = now;
    if (bleScanner.hasFoundDevice()) {
      bleScale.init(bleScanner.getFoundAddress());
      if (bleScale.connect()) {
        usingBleScale = true;
        lastSeenConnected = now;
        bleScanner.stopScan();
      } else {
        bleScanner.reset();
        bleScanner.startScan();
      }
    } else {
      bleScanner.startScan();
    }
  }

  float incomingRawWeight = 0.0;
  bool gotNewReading = false;

  // 2. Read from BLE Scale if connected
  if (usingBleScale) {
    bleScale.update();
    if (bleScale.isConnected()) {
      lastSeenConnected = now;
      incomingRawWeight = bleScale.getWeight();
      gotNewReading = true;
    } else {
      if (now - lastSeenConnected > 1000) {
        usingBleScale = false;
        bleScanner.reset();
      }
    }
  }

  // 3. Fallback to HX711 if BLE is not ready/connected
  if (!usingBleScale) {
    // Calling update() as frequently as possible is vital for HX711_ADC
    bool updatedA = scaleA.update();
    bool updatedB = scaleB.update();

    // Even if only one updated, we take the sum of current available data.
    // HX711_ADC's getData() returns the last filtered value.
    if (updatedA || updatedB) {
      incomingRawWeight = scaleA.getData() + scaleB.getData();
      gotNewReading = true;
    }
  }

  // 4. Global Spike Filter & Value Assignment
  if (gotNewReading) {
    static int spikeStreak = 0;
    // We increase the MAX_PHYSICAL_JUMP slightly to 15g because if both scales
    // update at once, a real fast change might be > 10g.
    const float MAX_PHYSICAL_JUMP = 15.0;
    const int MAX_STREAK = 3;

    float deltaWeight = abs(incomingRawWeight - rawCurrentWeight);

    // SPIKE PROTECTION:
    // If we see a massive jump, we ignore it unless it persists for 3 frames.
    // This prevents the pump's electrical EMF from triggering a "Target Weight
    // Reached" stop.
    if (deltaWeight > MAX_PHYSICAL_JUMP && spikeStreak < MAX_STREAK) {
      spikeStreak++;
      // We do NOT update lastScaleReadTime here, effectively ignoring the
      // noise.
      return;
    } else {
      spikeStreak = 0;
      rawCurrentWeight = incomingRawWeight;

      // HX711_ADC is already filtered internally (Moving Average).
      // BLE scales are also filtered. No further EMA needed.
      currentWeight = rawCurrentWeight;
      lastScaleReadTime = now;
    }
  }
}

void handleHeater() {
  unsigned long now = millis();
  static unsigned long lastTempRequest = 0;

  // 1. Update Temperature and Compute PID
  if (now - lastTempRequest > TEMP_UPDATE_INTERVAL_MS) {
    currentTemp = thermo.readCelsius();
    uint8_t error = thermo.readError();
    lastTempRequest = now;

    // Safety: Trigger SENSOR_ERROR if NAN, explicit fault bit, or impossible
    // temperature
    if (isnan(currentTemp) || error != 0 || currentTemp < 1) {
      currentState = SENSOR_ERROR;
      setHeater(false);
      return;
    }

    if (smoothedTemp == 0 && currentTemp > 0)
      smoothedTemp = currentTemp;

    smoothedTemp = (EMA_ALPHA_TEMP * currentTemp) +
                   ((1.0 - EMA_ALPHA_TEMP) * smoothedTemp);
    pidIn = smoothedTemp;
    pidSet = targetTemp + tempOffset;
    myPID.Compute();

    // Handle state transitions
    if (currentState == WARMING && currentTemp >= (pidSet - TEMP_MARGIN)) {
      warmingOverridden = false;
      currentState = HOME;
      encoder.setCount(coffeeWeight * 40);
    } else if (currentState == HOME && currentTemp < (pidSet - TEMP_MARGIN) &&
               !warmingOverridden) {
      currentState = WARMING;
    }
  }

  // 2. Determine if we should Force the heater or use PID
  bool forceHeaterOn = false;
  if (currentState == EXTRACTION && currentBrewPhase == EXTRACT &&
      extractStartTime > 0) {
    // Force heater during the first FORCE_HEATER_TIME ms of the main extraction
    if ((unsigned long)(now - extractStartTime) <= FORCE_HEATER_TIME) {
      forceHeaterOn = true;
    }
  }

  // 3. Apply Heater State
  if (currentTemp >= MAX_SAFE_TEMP) {
    setHeater(false); // Safety override
  } else if (forceHeaterOn) {
    setHeater(true);
  } else {
    // Revert to PID PWM Window
    if (now - windowStartTime > PID_WINDOW_SIZE_MS) {
      windowStartTime = now;
    }

    float tempGap = pidSet - currentTemp;
    if (tempGap > 20.0) {
      setHeater(true); // Safety Turbo: Full on if significantly under target
    } else {
      setHeater(pidOut > (now - windowStartTime));
    }
  }
}

void checkWaterLevel() {
  static unsigned long lastCheckTime = 0;
  if (millis() - lastCheckTime > 500) {
    lastCheckTime = millis();
    // XKC-Y25-NPN: Open collector. Pulled up, so LOW = water detected, HIGH =
    // no water.
    bool newIsLow = (digitalRead(WATER_LEVEL_PIN) == HIGH);
    if (newIsLow != isWaterLow) {
      isWaterLow = newIsLow;
      if (!isWaterLow) {
        // Reset dismissed state when water is refilled
        waterLowDismissed = false;
      }
    }
  }
}