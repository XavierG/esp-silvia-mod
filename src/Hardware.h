#pragma once

#include <Arduino.h>

void initHardware();
void waitForTemperatureSensor();
void setPump(bool state);
void setPumpPercentage(uint8_t percentage);
void setValve(bool state);
void setHeater(bool state);
void handleHeater();
void updateWeight();
void IRAM_ATTR handleButtonInterrupt();