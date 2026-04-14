#pragma once

// --- Pins ---
#define SSR_PUMP 17       // SSR Pump
#define SSR_VALVE 16      // SSR Valve
#define SSR_HEATER 2      // SSR Heater
#define MAX_CS 4          // SPI Temp Sensor
#define SPI_CLK 15        // SPI Temp Sensor
#define SPI_MISO 18       // SPI Temp Sensor
#define SPI_MOSI 19       // SPI Temp Sensor NOT IN USED FO NOW
#define ENCODER_CLK 5     // Encoder EC11
#define ENCODER_DT 6      // Encoder EC11
#define ENCODER_SW 7      // Encoder EC11
#define SDA_PIN 8         // OLED Screen
#define SCL_PIN 9         // OLED Screen
#define HX_SCK 10         // HX711 Scale
#define HX_A_DOUT 11      // HX711 Scale
#define HX_B_DOUT 12      // HX711 Scale
#define DIMMER_ZC_PIN 13  // Pump Dimmer
#define DIMMER_OUT_PIN 14 // Pump Dimmer

#define DEFAULT_COFFEE_WEIGHT 18.0
#define DEFAULT_RATIO 2.0
#define DEFAULT_TARGET_TEMP 93.0
#define DEFAULT_TEMP_OFFSET 7.0
#define MAX_SAFE_TEMP 105.0
#define DEFAULT_PREINF_TIME 1
#define DEFAULT_BLOOM_TIME 1
#define DEFAULT_FLOW_STOP_FACTOR 3.0
#define MIN_PUMP_PERCENTAGE 30
#define DEFAULT_SOAK_WEIGHT 0.5
#define DEFAULT_SOAK_POWER 80
#define DEFAULT_TARGET_FLOW 2.5
#define DEFAULT_START_FLOW 3.0
#define DEFAULT_END_FLOW 1.0

// --- Magic Numbers & Timings ---
#define TEMP_MARGIN 2 // Temperature below which the state goes back to warming
#define TEMP_UPDATE_INTERVAL_MS 1000 // How often to read DS18B20
#define PID_WINDOW_SIZE_MS 2000      // Slow PWM window for heater
#define UI_REFRESH_INTERVAL_MS 50    // ms screen refresh rate
#define SCREENSAVER_TIMEOUT_MS 60000 // 1 minute sleep timeout
#define SPLASH_SCREEN_DELAY_MS 500   // Splash screen minimum time
#define DRIP_CATCH_DELAY_MS 3500     // Auto-exit DONE state
#define FLOW_CALC_INTERVAL_S 0.2     // Weight polling interval for flow rate
#define LONG_PRESS_TIMING 350        // Long press timing in ms
#define FORCE_HEATER_TIME                                                      \
  2000 // ms after extraction starts where the heater needs to be fully on

#define EMA_ALPHA_FLOW                                                         \
  0.3 // Exponential Moving Average smoothing factor for Flow rate measurement
#define EMA_ALPHA_SCALE                                                        \
  0.9 // Exponential Moving Average smoothing factor for scale
#define EMA_ALPHA_TEMP                                                         \
  0.7 // Exponential Moving Average smoothing factor for temperature reading for
      // the PID

// Scale setup
#define SCALE_A 42
#define SCALE_B 42