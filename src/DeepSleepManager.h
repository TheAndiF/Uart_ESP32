#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include <esp_sleep.h>

class DeepSleepManager {
public:
  using BeforeSleepCallback = void (*)(const String& reason);

  void begin();
  void registerRoutes(AsyncWebServer& server);
  void loop(bool mqttOk, float batteryVoltage);
  void sleepNow(const String& reason);
  bool handleMqttCommand(const String& subTopic, const String& payload);
  void setBeforeSleepCallback(BeforeSleepCallback cb) { _beforeSleepCb = cb; }

  bool isEnabled() const { return _enabled; }
  String wakeupMode() const { return _wakeupMode; }
  String lastStatus() const { return _lastStatus; }
  String wakeupReasonText() const;
  uint32_t awakeSecondsElapsed() const;
  uint32_t remainingOnlineSeconds() const;
  uint32_t nextWakeupSeconds() const;
  uint32_t nextWakeupEpoch() const;
  String nextWakeupText() const;
  String settingsJson() const;

private:
  bool _enabled = false;
  uint32_t _sleepIntervalMin = 15;
  uint32_t _awakeSeconds = 300;
  uint32_t _minOnlineSeconds = 60;
  uint32_t _otaWindowSeconds = 300;

  bool _mqttOkOnly = true;
  uint32_t _mqttTimeoutSeconds = 120;

  bool _nightEnabled = false;
  uint8_t _nightStartHour = 22;
  uint8_t _nightEndHour = 6;
  uint32_t _nightIntervalMin = 60;

  String _wakeupMode = "interval"; // interval | full_hour | fixed | mixed
  uint8_t _wakeupOffsetMin = 0;
  String _fixedWakeTimes = "06:00,12:00,18:00";
  uint32_t _fallbackIntervalMin = 15;

  bool _wakeOnceEnabled = false;
  uint32_t _wakeOnceEpoch = 0;
  String _wakeOnceText = "";

  bool _batteryAdaptive = false;
  float _lowVoltageV = 3.50f;
  uint32_t _lowIntervalMin = 60;
  float _criticalVoltageV = 3.30f;
  uint32_t _criticalIntervalMin = 180;

  unsigned long _bootMillis = 0;
  bool _lastMqttOk = false;
  float _lastBatteryVoltage = NAN;
  String _lastStatus = "inaktiv";
  BeforeSleepCallback _beforeSleepCb = nullptr;

  void load();
  void save() const;
  void validate();
  bool isNightNow() const;
  uint32_t currentIntervalSeconds(float batteryVoltage) const;
  uint32_t effectiveAwakeSeconds() const;
  uint32_t secondsUntilSleep(bool mqttOk) const;
  uint32_t secondsUntilFullHour(uint32_t minSeconds) const;
  uint32_t secondsUntilFixed(uint32_t minSeconds = 0) const;
  uint32_t secondsUntilWakeOnce() const;
  uint32_t calculateNextWakeSeconds(float batteryVoltage) const;

  static bool parseBool(const String& value);
  static String durationText(uint32_t seconds);
  static String selected(bool value) { return value ? " selected" : ""; }
  static String checked(bool value) { return value ? " checked" : ""; }
  static String epochText(uint32_t epoch);
  static bool parseLocalDateTime(const String& text, uint32_t& epochOut);
  String pageHtml() const;
  void saveFromRequest(AsyncWebServerRequest* request);
};
