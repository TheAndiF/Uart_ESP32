#include "DeepSleepManager.h"
#include "ConfigDefaults.h"
#include <Preferences.h>
#include <time.h>
#include <math.h>

void DeepSleepManager::begin() {
  _bootMillis = millis();
  _lastStatus = wakeupReasonText();
  load();
}

void DeepSleepManager::load() {
  Preferences p;
  if (!p.begin("netconf", true)) return;
  _enabled = p.isKey("sleep_enabled") ? p.getBool("sleep_enabled", UART_SLEEP_ENABLED) : UART_SLEEP_ENABLED;
  _sleepIntervalMin = p.isKey("sleep_int_min") ? p.getUInt("sleep_int_min", UART_SLEEP_INTERVAL_MIN) : UART_SLEEP_INTERVAL_MIN;
  _awakeSeconds = p.isKey("sleep_awake_s") ? p.getUInt("sleep_awake_s", UART_SLEEP_AWAKE_S) : UART_SLEEP_AWAKE_S;
  _minOnlineSeconds = p.isKey("sleep_min_on_s") ? p.getUInt("sleep_min_on_s", UART_SLEEP_MIN_ONLINE_S) : UART_SLEEP_MIN_ONLINE_S;
  _otaWindowSeconds = p.isKey("sleep_ota_win") ? p.getUInt("sleep_ota_win", UART_SLEEP_OTA_WINDOW_S) : UART_SLEEP_OTA_WINDOW_S;
  _mqttOkOnly = p.isKey("sleep_mqtt_ok") ? p.getBool("sleep_mqtt_ok", UART_SLEEP_MQTT_OK_ONLY) : UART_SLEEP_MQTT_OK_ONLY;
  _mqttTimeoutSeconds = p.isKey("sleep_mqtt_to") ? p.getUInt("sleep_mqtt_to", UART_SLEEP_MQTT_TIMEOUT_S) : UART_SLEEP_MQTT_TIMEOUT_S;
  _nightEnabled = p.isKey("sleep_night_en") ? p.getBool("sleep_night_en", UART_SLEEP_NIGHT_ENABLED) : UART_SLEEP_NIGHT_ENABLED;
  _nightStartHour = (uint8_t)(p.isKey("sleep_night_st") ? p.getUInt("sleep_night_st", UART_SLEEP_NIGHT_START_H) : UART_SLEEP_NIGHT_START_H);
  _nightEndHour = (uint8_t)(p.isKey("sleep_night_end") ? p.getUInt("sleep_night_end", UART_SLEEP_NIGHT_END_H) : UART_SLEEP_NIGHT_END_H);
  _nightIntervalMin = p.isKey("sleep_night_int") ? p.getUInt("sleep_night_int", UART_SLEEP_NIGHT_INTERVAL_MIN) : UART_SLEEP_NIGHT_INTERVAL_MIN;
  _wakeupMode = p.isKey("sleep_wake_mode") ? p.getString("sleep_wake_mode", UART_SLEEP_WAKE_MODE) : String(UART_SLEEP_WAKE_MODE);
  _wakeupOffsetMin = (uint8_t)(p.isKey("sleep_wake_off") ? p.getUInt("sleep_wake_off", UART_SLEEP_WAKE_OFFSET_MIN) : UART_SLEEP_WAKE_OFFSET_MIN);
  _fixedWakeTimes = p.isKey("sleep_fixed") ? p.getString("sleep_fixed", UART_SLEEP_FIXED_TIMES) : String(UART_SLEEP_FIXED_TIMES);
  _fallbackIntervalMin = p.isKey("sleep_fallback") ? p.getUInt("sleep_fallback", UART_SLEEP_FALLBACK_MIN) : UART_SLEEP_FALLBACK_MIN;
  _wakeOnceEnabled = p.isKey("sleep_once_en") ? p.getBool("sleep_once_en", false) : false;
  _wakeOnceEpoch = p.isKey("sleep_once_ep") ? p.getUInt("sleep_once_ep", 0) : 0;
  _wakeOnceText = p.isKey("sleep_once_txt") ? p.getString("sleep_once_txt", "") : String();
  _batteryAdaptive = p.isKey("sleep_bat_ad") ? p.getBool("sleep_bat_ad", UART_SLEEP_BATTERY_ADAPTIVE) : UART_SLEEP_BATTERY_ADAPTIVE;
  _lowVoltageV = p.isKey("sleep_low_v") ? p.getFloat("sleep_low_v", UART_SLEEP_LOW_V) : UART_SLEEP_LOW_V;
  _lowIntervalMin = p.isKey("sleep_low_int") ? p.getUInt("sleep_low_int", UART_SLEEP_LOW_INTERVAL_MIN) : UART_SLEEP_LOW_INTERVAL_MIN;
  _criticalVoltageV = p.isKey("sleep_crit_v") ? p.getFloat("sleep_crit_v", UART_SLEEP_CRITICAL_V) : UART_SLEEP_CRITICAL_V;
  _criticalIntervalMin = p.isKey("sleep_crit_int") ? p.getUInt("sleep_crit_int", UART_SLEEP_CRITICAL_INTERVAL_MIN) : UART_SLEEP_CRITICAL_INTERVAL_MIN;
  p.end();
  validate();
}

void DeepSleepManager::save() const {
  Preferences p;
  if (!p.begin("netconf", false)) return;
  p.putBool("sleep_enabled", _enabled);
  p.putUInt("sleep_int_min", _sleepIntervalMin);
  p.putUInt("sleep_awake_s", _awakeSeconds);
  p.putUInt("sleep_min_on_s", _minOnlineSeconds);
  p.putUInt("sleep_ota_win", _otaWindowSeconds);
  p.putBool("sleep_mqtt_ok", _mqttOkOnly);
  p.putUInt("sleep_mqtt_to", _mqttTimeoutSeconds);
  p.putBool("sleep_night_en", _nightEnabled);
  p.putUInt("sleep_night_st", _nightStartHour);
  p.putUInt("sleep_night_end", _nightEndHour);
  p.putUInt("sleep_night_int", _nightIntervalMin);
  p.putString("sleep_wake_mode", _wakeupMode);
  p.putUInt("sleep_wake_off", _wakeupOffsetMin);
  p.putString("sleep_fixed", _fixedWakeTimes);
  p.putUInt("sleep_fallback", _fallbackIntervalMin);
  p.putBool("sleep_once_en", _wakeOnceEnabled);
  p.putUInt("sleep_once_ep", _wakeOnceEpoch);
  p.putString("sleep_once_txt", _wakeOnceText);
  p.putBool("sleep_bat_ad", _batteryAdaptive);
  p.putFloat("sleep_low_v", _lowVoltageV);
  p.putUInt("sleep_low_int", _lowIntervalMin);
  p.putFloat("sleep_crit_v", _criticalVoltageV);
  p.putUInt("sleep_crit_int", _criticalIntervalMin);
  p.end();
}

void DeepSleepManager::validate() {
  if (_sleepIntervalMin < 1) _sleepIntervalMin = 1;
  if (_awakeSeconds < 10) _awakeSeconds = 10;
  if (_minOnlineSeconds < 10) _minOnlineSeconds = 10;
  if (_mqttTimeoutSeconds < 10) _mqttTimeoutSeconds = 10;
  if (_nightStartHour > 23) _nightStartHour = 22;
  if (_nightEndHour > 23) _nightEndHour = 6;
  if (_nightIntervalMin < 1) _nightIntervalMin = 1;
  if (_wakeupOffsetMin > 59) _wakeupOffsetMin = 0;
  if (_fallbackIntervalMin < 1) _fallbackIntervalMin = 1;
  if (_lowIntervalMin < 1) _lowIntervalMin = 1;
  if (_criticalIntervalMin < 1) _criticalIntervalMin = 1;
  if (_wakeupMode != "interval" && _wakeupMode != "full_hour" &&
      _wakeupMode != "fixed" && _wakeupMode != "mixed") _wakeupMode = "interval";
}

uint32_t DeepSleepManager::awakeSecondsElapsed() const {
  return (millis() - _bootMillis) / 1000UL;
}

String DeepSleepManager::wakeupReasonText() const {
  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_TIMER: return "Wakeup: Timer";
    case ESP_SLEEP_WAKEUP_EXT0: return "Wakeup: EXT0";
    case ESP_SLEEP_WAKEUP_EXT1: return "Wakeup: EXT1";
    case ESP_SLEEP_WAKEUP_TOUCHPAD: return "Wakeup: Touchpad";
    case ESP_SLEEP_WAKEUP_ULP: return "Wakeup: ULP";
    default: return "Wakeup: Power-On / Reset";
  }
}

bool DeepSleepManager::isNightNow() const {
  if (!_nightEnabled) return false;
  struct tm ti;
  if (!getLocalTime(&ti, 20)) return false;
  uint8_t h = (uint8_t)ti.tm_hour;
  if (_nightStartHour == _nightEndHour) return false;
  if (_nightStartHour < _nightEndHour) return h >= _nightStartHour && h < _nightEndHour;
  return h >= _nightStartHour || h < _nightEndHour;
}

uint32_t DeepSleepManager::currentIntervalSeconds(float batteryVoltage) const {
  uint32_t minutes = _sleepIntervalMin;
  if (isNightNow() && _nightIntervalMin > minutes) minutes = _nightIntervalMin;
  if (_batteryAdaptive && isfinite(batteryVoltage) && batteryVoltage > 0.0f) {
    if (batteryVoltage <= _criticalVoltageV && _criticalIntervalMin > minutes) minutes = _criticalIntervalMin;
    else if (batteryVoltage <= _lowVoltageV && _lowIntervalMin > minutes) minutes = _lowIntervalMin;
  }
  if (minutes < 1) minutes = 1;
  return minutes * 60UL;
}

uint32_t DeepSleepManager::effectiveAwakeSeconds() const {
  uint32_t required = _awakeSeconds;
  if (_minOnlineSeconds > required) required = _minOnlineSeconds;
  if (_otaWindowSeconds > required) required = _otaWindowSeconds;
  return required;
}

uint32_t DeepSleepManager::secondsUntilSleep(bool mqttOk) const {
  if (!_enabled) return 0;
  uint32_t target = effectiveAwakeSeconds();
  if (_mqttOkOnly && !mqttOk && _mqttTimeoutSeconds > target) target = _mqttTimeoutSeconds;
  uint32_t elapsed = awakeSecondsElapsed();
  return elapsed >= target ? 0 : target - elapsed;
}

uint32_t DeepSleepManager::secondsUntilFullHour(uint32_t minSeconds) const {
  time_t now = time(nullptr);
  if (now < 1000000000) {
    const uint32_t fallbackSeconds = _fallbackIntervalMin * 60U;
    return minSeconds > fallbackSeconds ? minSeconds : fallbackSeconds;
  }
  struct tm local;
  localtime_r(&now, &local);
  local.tm_sec = 0;
  local.tm_min = _wakeupOffsetMin;
  time_t target = mktime(&local);
  if (target <= now) {
    local.tm_hour += 1;
    target = mktime(&local);
  }
  while ((uint32_t)(target - now) < minSeconds) {
    localtime_r(&target, &local);
    local.tm_hour += 1;
    target = mktime(&local);
  }
  return (uint32_t)(target - now);
}

uint32_t DeepSleepManager::secondsUntilFixed(uint32_t minSeconds) const {
  time_t now = time(nullptr);
  if (now < 1000000000) {
    const uint32_t fallbackSeconds = _fallbackIntervalMin * 60U;
    return minSeconds > fallbackSeconds ? minSeconds : fallbackSeconds;
  }
  struct tm base;
  localtime_r(&now, &base);
  uint32_t best = UINT32_MAX;
  String list = _fixedWakeTimes;
  int start = 0;
  while (start < list.length()) {
    int comma = list.indexOf(',', start);
    String item = comma >= 0 ? list.substring(start, comma) : list.substring(start);
    item.trim();
    int colon = item.indexOf(':');
    if (colon > 0) {
      int h = item.substring(0, colon).toInt();
      int m = item.substring(colon + 1).toInt();
      if (h >= 0 && h <= 23 && m >= 0 && m <= 59) {
        for (int day = 0; day < 3; ++day) {
          struct tm t = base;
          t.tm_mday += day;
          t.tm_hour = h; t.tm_min = m; t.tm_sec = 0;
          time_t ep = mktime(&t);
          if (ep > now) {
            uint32_t diff = (uint32_t)(ep - now);
            if (diff >= minSeconds && diff < best) best = diff;
          }
        }
      }
    }
    if (comma < 0) break;
    start = comma + 1;
  }
  if (best != UINT32_MAX) return best;
  const uint32_t fallbackSeconds = _fallbackIntervalMin * 60U;
  return minSeconds > fallbackSeconds ? minSeconds : fallbackSeconds;
}

uint32_t DeepSleepManager::secondsUntilWakeOnce() const {
  if (!_wakeOnceEnabled || _wakeOnceEpoch == 0) return 0;
  time_t now = time(nullptr);
  if (now < 1000000000) return 0;
  return _wakeOnceEpoch <= (uint32_t)now ? 1 : _wakeOnceEpoch - (uint32_t)now;
}

uint32_t DeepSleepManager::calculateNextWakeSeconds(float batteryVoltage) const {
  uint32_t once = secondsUntilWakeOnce();
  if (once) return once;
  uint32_t interval = currentIntervalSeconds(batteryVoltage);
  if (_wakeupMode == "full_hour") return secondsUntilFullHour(interval);
  if (_wakeupMode == "fixed") return secondsUntilFixed(interval);
  if (_wakeupMode == "mixed") {
    uint32_t fixed = secondsUntilFixed(0);
    return interval < fixed ? interval : fixed;
  }
  return interval;
}

uint32_t DeepSleepManager::remainingOnlineSeconds() const { return secondsUntilSleep(_lastMqttOk); }
uint32_t DeepSleepManager::nextWakeupSeconds() const { return calculateNextWakeSeconds(_lastBatteryVoltage); }

uint32_t DeepSleepManager::nextWakeupEpoch() const {
  time_t now = time(nullptr);
  return now < 1000000000 ? 0 : (uint32_t)now + nextWakeupSeconds();
}

String DeepSleepManager::epochText(uint32_t epoch) {
  if (!epoch) return "";
  time_t t = epoch;
  struct tm ti;
  localtime_r(&t, &ti);
  char buf[32];
  strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M:%S", &ti);
  return String(buf);
}

String DeepSleepManager::nextWakeupText() const {
  uint32_t ep = nextWakeupEpoch();
  return ep ? epochText(ep) : "in " + durationText(nextWakeupSeconds());
}

bool DeepSleepManager::parseLocalDateTime(const String& input, uint32_t& epochOut) {
  String s = input; s.trim();
  if (s.length() == 0) return false;
  bool digits = true;
  for (size_t i=0; i<s.length(); ++i) if (!isDigit(s[i])) { digits = false; break; }
  if (digits) {
    uint32_t e = strtoul(s.c_str(), nullptr, 10);
    if (e > 1000000000UL) { epochOut = e; return true; }
  }
  int y=0,mo=0,d=0,h=0,mi=0,sec=0;
  int n = sscanf(s.c_str(), "%d-%d-%d %d:%d:%d", &y,&mo,&d,&h,&mi,&sec);
  if (n < 5) n = sscanf(s.c_str(), "%d.%d.%d %d:%d:%d", &d,&mo,&y,&h,&mi,&sec);
  if (n < 5 || y < 2020 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59) return false;
  struct tm ti = {};
  ti.tm_year = y - 1900; ti.tm_mon = mo - 1; ti.tm_mday = d;
  ti.tm_hour = h; ti.tm_min = mi; ti.tm_sec = sec; ti.tm_isdst = -1;
  time_t e = mktime(&ti);
  if (e <= 0) return false;
  epochOut = (uint32_t)e;
  return true;
}

bool DeepSleepManager::parseBool(const String& input) {
  String s = input; s.trim(); s.toLowerCase();
  return s == "1" || s == "true" || s == "on" || s == "ein" || s == "ja" || s == "yes";
}

String DeepSleepManager::durationText(uint32_t seconds) {
  if (seconds < 60) return String(seconds) + " s";
  uint32_t m = seconds / 60, s = seconds % 60;
  if (m < 60) return String(m) + " min" + (s ? " " + String(s) + " s" : "");
  uint32_t h = m / 60; m %= 60;
  return String(h) + " h" + (m ? " " + String(m) + " min" : "");
}

void DeepSleepManager::loop(bool mqttOk, float batteryVoltage) {
  _lastMqttOk = mqttOk;
  _lastBatteryVoltage = batteryVoltage;

  if (_wakeOnceEnabled && _wakeOnceEpoch > 0) {
    time_t now = time(nullptr);
    if (now > 1000000000 && (uint32_t)now >= _wakeOnceEpoch) {
      _wakeOnceEnabled = false; _wakeOnceEpoch = 0; _wakeOnceText = "";
      _lastStatus = "Einmal-Aufwachzeit verbraucht"; save();
    }
  }

  if (!_enabled || secondsUntilSleep(mqttOk) > 0) return;
  String reason = "Online-Zeit abgelaufen";
  if (_mqttOkOnly && !mqttOk) reason += ", MQTT Timeout";
  sleepNow(reason);
}

void DeepSleepManager::sleepNow(const String& reason) {
  _lastStatus = reason;
  if (_beforeSleepCb) _beforeSleepCb(reason);
  delay(250);
  uint32_t seconds = calculateNextWakeSeconds(_lastBatteryVoltage);
  if (seconds < 1) seconds = 1;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);
  esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
  Serial.printf("[SLEEP] %s; fuer %lu s\n", reason.c_str(), (unsigned long)seconds);
  Serial.flush();
  esp_deep_sleep_start();
}

bool DeepSleepManager::handleMqttCommand(const String& subTopic, const String& payload) {
  String t = subTopic; t.trim();
  if (t == "enabled") _enabled = parseBool(payload);
  else if (t == "interval_min") _sleepIntervalMin = payload.toInt();
  else if (t == "awake_s") _awakeSeconds = payload.toInt();
  else if (t == "min_online_s") _minOnlineSeconds = payload.toInt();
  else if (t == "ota_window_s") _otaWindowSeconds = payload.toInt();
  else if (t == "mqtt_ok_only") _mqttOkOnly = parseBool(payload);
  else if (t == "mqtt_timeout_s") _mqttTimeoutSeconds = payload.toInt();
  else if (t == "night_enabled") _nightEnabled = parseBool(payload);
  else if (t == "night_start_h") _nightStartHour = payload.toInt();
  else if (t == "night_end_h") _nightEndHour = payload.toInt();
  else if (t == "night_interval_min") _nightIntervalMin = payload.toInt();
  else if (t == "battery_adaptive") _batteryAdaptive = parseBool(payload);
  else if (t == "low_voltage_threshold_v") _lowVoltageV = payload.toFloat();
  else if (t == "low_voltage_interval_min") _lowIntervalMin = payload.toInt();
  else if (t == "critical_voltage_threshold_v") _criticalVoltageV = payload.toFloat();
  else if (t == "critical_voltage_interval_min") _criticalIntervalMin = payload.toInt();
  else if (t == "wakeup_mode") _wakeupMode = payload;
  else if (t == "wakeup_offset_min") _wakeupOffsetMin = payload.toInt();
  else if (t == "fixed_times") _fixedWakeTimes = payload;
  else if (t == "fallback_interval_min") _fallbackIntervalMin = payload.toInt();
  else if (t == "wake_once") {
    uint32_t e; if (!parseLocalDateTime(payload, e)) return false;
    _wakeOnceEnabled = true; _wakeOnceEpoch = e; _wakeOnceText = epochText(e);
  }
  else if (t == "wake_once_in_min") {
    time_t now = time(nullptr);
    const long requestedMinutes = payload.toInt();
    const uint32_t minutes = requestedMinutes > 1L ? (uint32_t)requestedMinutes : 1U;
    if (now < 1000000000) return false;
    _wakeOnceEnabled = true; _wakeOnceEpoch = (uint32_t)now + minutes * 60UL; _wakeOnceText = epochText(_wakeOnceEpoch);
  }
  else if (t == "wake_once_clear") {
    _wakeOnceEnabled = false; _wakeOnceEpoch = 0; _wakeOnceText = "";
  }
  else if (t == "now") {
    if (parseBool(payload) || payload == "sleep" || payload == "start") sleepNow("MQTT sleep/now");
    return true;
  }
  else return false;

  validate(); save(); _lastStatus = "MQTT gespeichert: " + t;
  return true;
}

String DeepSleepManager::settingsJson() const {
  String j = "{";
  j += "\"enabled\":" + String(_enabled ? "true" : "false");
  j += ",\"interval_min\":" + String(_sleepIntervalMin);
  j += ",\"awake_s\":" + String(_awakeSeconds);
  j += ",\"min_online_s\":" + String(_minOnlineSeconds);
  j += ",\"ota_window_s\":" + String(_otaWindowSeconds);
  j += ",\"mqtt_ok_only\":" + String(_mqttOkOnly ? "true" : "false");
  j += ",\"mqtt_timeout_s\":" + String(_mqttTimeoutSeconds);
  j += ",\"night_enabled\":" + String(_nightEnabled ? "true" : "false");
  j += ",\"night_start_h\":" + String(_nightStartHour);
  j += ",\"night_end_h\":" + String(_nightEndHour);
  j += ",\"night_interval_min\":" + String(_nightIntervalMin);
  j += ",\"battery_adaptive\":" + String(_batteryAdaptive ? "true" : "false");
  j += ",\"low_voltage_threshold_v\":" + String(_lowVoltageV, 2);
  j += ",\"low_voltage_interval_min\":" + String(_lowIntervalMin);
  j += ",\"critical_voltage_threshold_v\":" + String(_criticalVoltageV, 2);
  j += ",\"critical_voltage_interval_min\":" + String(_criticalIntervalMin);
  j += ",\"wakeup_mode\":\"" + _wakeupMode + "\"";
  j += ",\"wakeup_offset_min\":" + String(_wakeupOffsetMin);
  j += ",\"fixed_times\":\"" + _fixedWakeTimes + "\"";
  j += ",\"fallback_interval_min\":" + String(_fallbackIntervalMin);
  j += ",\"wake_once_enabled\":" + String(_wakeOnceEnabled ? "true" : "false");
  j += ",\"wake_once_epoch\":" + String(_wakeOnceEpoch);
  j += ",\"wake_once_text\":\"" + _wakeOnceText + "\"";
  j += ",\"remaining_online_s\":" + String(remainingOnlineSeconds());
  j += ",\"next_wakeup_s\":" + String(nextWakeupSeconds());
  j += ",\"next_wakeup_epoch\":" + String(nextWakeupEpoch());
  j += ",\"next_wakeup_text\":\"" + nextWakeupText() + "\"";
  j += "}";
  return j;
}

String DeepSleepManager::pageHtml() const {
  String h = F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Deep Sleep</title><style>body{font-family:Arial;margin:20px;background:#f5f5f5}.box{max-width:720px;margin:auto;background:white;padding:20px;border-radius:10px}fieldset{margin:14px 0;padding:12px}label{display:block;margin-top:8px;font-weight:600}input,select{width:100%;box-sizing:border-box;padding:8px;margin-top:3px}button,.btn{display:block;width:100%;box-sizing:border-box;padding:10px;margin:10px 0;text-align:center;border:1px solid #aaa;border-radius:5px;background:#eee;color:#000;text-decoration:none}</style></head><body><div class='box'><h1>Deep Sleep</h1>");
  h += "<p><b>Status:</b> " + String(_enabled ? "aktiv" : "aus") + "</p>";
  h += "<p><b>Online:</b> " + durationText(awakeSecondsElapsed()) + " &nbsp; <b>Rest:</b> " + (_enabled ? durationText(remainingOnlineSeconds()) : "deaktiviert") + "</p>";
  h += "<p><b>Naechster Wakeup:</b> " + nextWakeupText() + "</p>";
  h += "<p><b>Wakeup-Grund:</b> " + wakeupReasonText() + "</p>";
  if (isfinite(_lastBatteryVoltage)) h += "<p><b>Batterie:</b> " + String(_lastBatteryVoltage, 3) + " V</p>";
  h += "<form method='post' action='/savedeep_sleep'>";
  h += "<fieldset><legend>Basis</legend><label>Deep Sleep</label><select name='enabled'><option value='0'"+selected(!_enabled)+">AUS</option><option value='1'"+selected(_enabled)+">EIN</option></select>";
  h += "<label>Schlafintervall [min]</label><input type='number' min='1' name='interval_min' value='"+String(_sleepIntervalMin)+"'>";
  h += "<label>Online-Zeit [s]</label><input type='number' min='10' name='awake_s' value='"+String(_awakeSeconds)+"'>";
  h += "<label>Mindest-Onlinezeit [s]</label><input type='number' min='10' name='min_online_s' value='"+String(_minOnlineSeconds)+"'>";
  h += "<label>OTA-Schutzfenster [s]</label><input type='number' min='0' name='ota_window_s' value='"+String(_otaWindowSeconds)+"'></fieldset>";
  h += "<fieldset><legend>Wakeup</legend><label>Modus</label><select name='wakeup_mode'><option value='interval'"+selected(_wakeupMode=="interval")+">Intervall</option><option value='full_hour'"+selected(_wakeupMode=="full_hour")+">Volle Stunde</option><option value='fixed'"+selected(_wakeupMode=="fixed")+">Feste Zeiten</option><option value='mixed'"+selected(_wakeupMode=="mixed")+">Mixed</option></select>";
  h += "<label>Minuten-Offset bei voller Stunde</label><input type='number' min='0' max='59' name='wakeup_offset' value='"+String(_wakeupOffsetMin)+"'>";
  h += "<label>Feste Zeiten (z.B. 06:00,12:00,18:00)</label><input name='fixed_times' value='"+_fixedWakeTimes+"'>";
  h += "<label>Fallback [min]</label><input type='number' min='1' name='fallback_min' value='"+String(_fallbackIntervalMin)+"'>";
  h += "<label>Einmal-Wakeup</label><input name='wake_once' placeholder='2026-09-18 08:00:00' value='"+_wakeOnceText+"'></fieldset>";
  h += "<fieldset><legend>Nachtmodus</legend><label>Aktiv</label><select name='night_enabled'><option value='0'"+selected(!_nightEnabled)+">AUS</option><option value='1'"+selected(_nightEnabled)+">EIN</option></select>";
  h += "<label>Startstunde</label><input type='number' min='0' max='23' name='night_start' value='"+String(_nightStartHour)+"'><label>Endstunde</label><input type='number' min='0' max='23' name='night_end' value='"+String(_nightEndHour)+"'><label>Nachtintervall [min]</label><input type='number' min='1' name='night_interval' value='"+String(_nightIntervalMin)+"'></fieldset>";
  h += "<fieldset><legend>Batterieabhaengig</legend><label>Aktiv</label><select name='bat_adaptive'><option value='0'"+selected(!_batteryAdaptive)+">AUS</option><option value='1'"+selected(_batteryAdaptive)+">EIN</option></select>";
  h += "<label>Low [V]</label><input type='number' step='0.01' name='low_v' value='"+String(_lowVoltageV,2)+"'><label>Low Intervall [min]</label><input type='number' min='1' name='low_int' value='"+String(_lowIntervalMin)+"'><label>Kritisch [V]</label><input type='number' step='0.01' name='crit_v' value='"+String(_criticalVoltageV,2)+"'><label>Kritisch Intervall [min]</label><input type='number' min='1' name='crit_int' value='"+String(_criticalIntervalMin)+"'></fieldset>";
  h += "<fieldset><legend>MQTT-Abhaengigkeit</legend><label>Nur nach MQTT-Verbindung schlafen</label><select name='mqtt_ok'><option value='0'"+selected(!_mqttOkOnly)+">NEIN</option><option value='1'"+selected(_mqttOkOnly)+">JA</option></select><label>MQTT Timeout [s]</label><input type='number' min='10' name='mqtt_timeout' value='"+String(_mqttTimeoutSeconds)+"'></fieldset>";
  h += "<button type='submit'>Speichern</button></form><form method='post' action='/sleep_now'><button type='submit'>Jetzt schlafen</button></form><a class='btn' href='/'>Zurueck</a></div></body></html>";
  return h;
}

void DeepSleepManager::saveFromRequest(AsyncWebServerRequest* r) {
  if (r->hasArg("enabled")) _enabled = r->arg("enabled").toInt();
  if (r->hasArg("interval_min")) _sleepIntervalMin = r->arg("interval_min").toInt();
  if (r->hasArg("awake_s")) _awakeSeconds = r->arg("awake_s").toInt();
  if (r->hasArg("min_online_s")) _minOnlineSeconds = r->arg("min_online_s").toInt();
  if (r->hasArg("ota_window_s")) _otaWindowSeconds = r->arg("ota_window_s").toInt();
  if (r->hasArg("wakeup_mode")) _wakeupMode = r->arg("wakeup_mode");
  if (r->hasArg("wakeup_offset")) _wakeupOffsetMin = r->arg("wakeup_offset").toInt();
  if (r->hasArg("fixed_times")) _fixedWakeTimes = r->arg("fixed_times");
  if (r->hasArg("fallback_min")) _fallbackIntervalMin = r->arg("fallback_min").toInt();
  if (r->hasArg("wake_once")) {
    String s=r->arg("wake_once"); s.trim();
    if (s.length() == 0) { _wakeOnceEnabled=false; _wakeOnceEpoch=0; _wakeOnceText=""; }
    else { uint32_t e; if (parseLocalDateTime(s,e)) { _wakeOnceEnabled=true; _wakeOnceEpoch=e; _wakeOnceText=epochText(e); } }
  }
  if (r->hasArg("night_enabled")) _nightEnabled = r->arg("night_enabled").toInt();
  if (r->hasArg("night_start")) _nightStartHour = r->arg("night_start").toInt();
  if (r->hasArg("night_end")) _nightEndHour = r->arg("night_end").toInt();
  if (r->hasArg("night_interval")) _nightIntervalMin = r->arg("night_interval").toInt();
  if (r->hasArg("bat_adaptive")) _batteryAdaptive = r->arg("bat_adaptive").toInt();
  if (r->hasArg("low_v")) _lowVoltageV = r->arg("low_v").toFloat();
  if (r->hasArg("low_int")) _lowIntervalMin = r->arg("low_int").toInt();
  if (r->hasArg("crit_v")) _criticalVoltageV = r->arg("crit_v").toFloat();
  if (r->hasArg("crit_int")) _criticalIntervalMin = r->arg("crit_int").toInt();
  if (r->hasArg("mqtt_ok")) _mqttOkOnly = r->arg("mqtt_ok").toInt();
  if (r->hasArg("mqtt_timeout")) _mqttTimeoutSeconds = r->arg("mqtt_timeout").toInt();
  validate(); save(); _bootMillis = millis(); _lastStatus = "Einstellungen gespeichert";
}

void DeepSleepManager::registerRoutes(AsyncWebServer& server) {
  server.on("/deepsleep", HTTP_GET, [this](AsyncWebServerRequest* r){ r->send(200,"text/html; charset=utf-8",pageHtml()); });
  server.on("/savedeep_sleep", HTTP_POST, [this](AsyncWebServerRequest* r){ saveFromRequest(r); r->redirect("/deepsleep"); });
  server.on("/sleep_now", HTTP_POST, [this](AsyncWebServerRequest* r){ r->send(200,"text/plain","Deep Sleep startet"); delay(250); sleepNow("Web sleep_now"); });
}
