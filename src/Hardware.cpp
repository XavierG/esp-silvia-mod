#include "Hardware.h"
#include "Globals.h"
#include <RBDdimmer.h>

dimmerLamp pumpDimmer(DIMMER_OUT_PIN, DIMMER_ZC_PIN);

void IRAM_ATTR handleButtonInterrupt() {
  unsigned long now = millis();
  if (digitalRead(ENCODER_SW) == LOW) {
    if (now - releaseTime > 10) {
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

  pumpDimmer.begin(NORMAL_MODE, OFF);

  attachInterrupt(digitalPinToInterrupt(ENCODER_SW), handleButtonInterrupt,
                  CHANGE);

  SPI.begin(SPI_CLK, SPI_MISO, SPI_MOSI, MAX_CS);
  delay(50);
  thermo.begin();

  scaleA.begin(HX_A_DOUT, HX_SCK);
  scaleB.begin(HX_B_DOUT, HX_SCK);
  scaleA.set_scale(42.0);
  scaleB.set_scale(42.0);

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
  if (percentage == 0) {
    setPump(false);
    pumpDimmer.setPower(0);
    currentPumpPercentage = 0;
  } else {
    setPump(true);
    uint8_t mappedPower = map(percentage, 1, 100, MIN_PUMP_PERCENTAGE, 100);
    pumpDimmer.setPower(mappedPower);
    currentPumpPercentage = percentage;
  }
}

void setValve(bool state) { digitalWrite(SSR_VALVE, state ? HIGH : LOW); }
void setHeater(bool state) { digitalWrite(SSR_HEATER, state ? HIGH : LOW); }

void updateWeight() {
  if (scaleA.is_ready() && scaleB.is_ready()) {
    float rawA = scaleA.get_units(1);
    float rawB = scaleB.get_units(1);
    rawCurrentWeight = (rawA - offsetA) + (rawB - offsetB);
    currentWeight = (currentWeight * (1.0 - EMA_ALPHA_SCALE)) +
                    (rawCurrentWeight * EMA_ALPHA_SCALE);
    lastScaleReadTime = millis();
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
    // kitchen temperature
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