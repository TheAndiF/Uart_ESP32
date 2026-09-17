#pragma once

#include <Arduino.h>
#include <Preferences.h>

class BatteryMonitor {
public:
  void load();
  void save() const;
  void begin();
  bool measure(const String& source = "manual");

  bool enabled = true;
  uint8_t adcPin = 34;       // ADC1, WLAN-kompatibel auf ESP32-WROOM-32
  float r1 = 100000.0f;      // Batterie+ -> ADC
  float r2 = 33000.0f;       // ADC -> GND
  float calibration = 1.0f;
  uint16_t samples = 20;

  bool valid = false;
  float voltage = NAN;
  float adcVoltage = NAN;
  uint32_t rawMilliVolts = 0;
  String status = "noch nicht gemessen";
  String source = "";
  String timestamp = "";

  bool isValidAdc1Pin() const;
};
