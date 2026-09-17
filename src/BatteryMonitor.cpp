#include "BatteryMonitor.h"
#include "ConfigDefaults.h"
#include <time.h>

static String currentTimeText() {
  struct tm ti;
  if (getLocalTime(&ti, 20)) {
    char buf[24];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &ti);
    return String(buf);
  }
  return String(millis() / 1000UL) + " s seit Start";
}

bool BatteryMonitor::isValidAdc1Pin() const {
  // Klassischer ESP32: ADC1 = GPIO32..39. ADC2 wird wegen WLAN bewusst nicht erlaubt.
  return adcPin >= 32 && adcPin <= 39;
}

void BatteryMonitor::load() {
  Preferences p;
  if (!p.begin("netconf", true)) return;
  enabled = p.isKey("bat_enabled") ? p.getBool("bat_enabled", UART_BATTERY_ENABLED) : UART_BATTERY_ENABLED;
  adcPin = (uint8_t)(p.isKey("bat_adc_pin") ? p.getUInt("bat_adc_pin", UART_BATTERY_ADC_PIN) : UART_BATTERY_ADC_PIN);
  r1 = p.isKey("bat_div_r1") ? p.getFloat("bat_div_r1", UART_BATTERY_R1) : UART_BATTERY_R1;
  r2 = p.isKey("bat_div_r2") ? p.getFloat("bat_div_r2", UART_BATTERY_R2) : UART_BATTERY_R2;
  calibration = p.isKey("bat_adc_cal") ? p.getFloat("bat_adc_cal", UART_BATTERY_CAL) : UART_BATTERY_CAL;
  samples = (uint16_t)(p.isKey("bat_samples") ? p.getUInt("bat_samples", UART_BATTERY_SAMPLES) : UART_BATTERY_SAMPLES);
  p.end();

  if (r1 < 1) r1 = 100000.0f;
  if (r2 < 1) r2 = 33000.0f;
  if (calibration < 0.1f || calibration > 5.0f) calibration = 1.0f;
  if (samples < 1) samples = 1;
  if (samples > 200) samples = 200;
  if (!isValidAdc1Pin()) adcPin = 34;
}

void BatteryMonitor::save() const {
  Preferences p;
  if (!p.begin("netconf", false)) return;
  p.putBool("bat_enabled", enabled);
  p.putUInt("bat_adc_pin", adcPin);
  p.putFloat("bat_div_r1", r1);
  p.putFloat("bat_div_r2", r2);
  p.putFloat("bat_adc_cal", calibration);
  p.putUInt("bat_samples", samples);
  p.end();
}

void BatteryMonitor::begin() {
  if (!enabled) { status = "deaktiviert"; return; }
  if (!isValidAdc1Pin()) { status = "ungueltiger ADC1-Pin"; return; }
  pinMode(adcPin, INPUT);
  analogSetPinAttenuation(adcPin, ADC_11db);
}

bool BatteryMonitor::measure(const String& src) {
  source = src;
  timestamp = currentTimeText();
  if (!enabled) {
    valid = false; voltage = NAN; adcVoltage = NAN; rawMilliVolts = 0;
    status = "deaktiviert"; return false;
  }
  if (!isValidAdc1Pin() || r2 <= 0 || samples == 0) {
    valid = false; status = "ungueltige Einstellungen"; return false;
  }

  uint64_t sumMv = 0;
  for (uint16_t i = 0; i < samples; ++i) {
    sumMv += analogReadMilliVolts(adcPin);
    delay(2);
  }
  rawMilliVolts = (uint32_t)(sumMv / samples);
  adcVoltage = rawMilliVolts / 1000.0f;
  voltage = adcVoltage * ((r1 + r2) / r2) * calibration;
  valid = isfinite(voltage) && voltage >= 0.0f && voltage < 20.0f;
  status = valid ? "ok" : "Messwert unplausibel";
  return valid;
}
