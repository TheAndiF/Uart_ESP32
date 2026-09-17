#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <time.h>
#include <esp_system.h>

#include "MqttManager.h"
#include "DeepSleepManager.h"
#include "OtaManager.h"
#include "BatteryMonitor.h"
#include "ConfigDefaults.h"

static const char* FW_NAME = "Uart_Esp32";
static const char* FW_BUILD_VERSION = "v0.6";
static const char* TZ_CET_CEST = "CET-1CEST,M3.5.0,M10.5.0/3";

AsyncWebServer server(80);
WiFiClient wifiClient;
MqttManager mqtt;
DeepSleepManager deepSleep;
OtaManager ota;
BatteryMonitor battery;
Preferences otaPrefs;

String staticIp, gatewayIp, subnetMask;
String nvsWifiSsid, nvsWifiPassword;
String apSsid, apPassword, hostname, ntpServer;
bool mqttEnabled = false;
String mqttHost, mqttUser, mqttPassword, mqttTopicBase, mqttClientId;
uint16_t mqttPort = 1883;
uint32_t heartbeatMs = 60000UL;

bool apActive = false;
bool apOnlyRecovery = false;
bool wifiLoadedFromNvs = false;
bool otaStarted = false;
bool wifiAttemptActive = false;
bool wifiDhcpFallback = false;
bool pendingWifiForceDhcp = false;
String activeWifiSource = "keine";
String pendingWifiSource;
String pendingWifiSsid;
uint8_t nextWifiCandidate = 0;
uint32_t wifiReconnectCount = 0;
uint32_t wifiFailureCount = 0;
unsigned long wifiAttemptStarted = 0;
unsigned long wifiDisconnectedSince = 0;
unsigned long apShutdownAt = 0;
unsigned long lastWifiTry = 0;
unsigned long lastMqttTry = 0;
unsigned long lastHeartbeat = 0;
unsigned long lastBattery = 0;

static String chipId() {
  uint64_t mac = ESP.getEfuseMac();
  char id[13];
  snprintf(id, sizeof(id), "%04X%08X", (uint16_t)(mac >> 32), (uint32_t)mac);
  return String(id);
}

static String defaultIdentity() { return "Uart_Esp32_" + chipId(); }

static String resetReasonText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "POWERON_RESET";
    case ESP_RST_EXT: return "EXTERNAL_RESET";
    case ESP_RST_SW: return "SOFTWARE_RESET";
    case ESP_RST_PANIC: return "PANIC_RESET";
    case ESP_RST_INT_WDT: return "INT_WDT_RESET";
    case ESP_RST_TASK_WDT: return "TASK_WDT_RESET";
    case ESP_RST_WDT: return "OTHER_WDT_RESET";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP_RESET";
    case ESP_RST_BROWNOUT: return "BROWNOUT_RESET";
    case ESP_RST_SDIO: return "SDIO_RESET";
    default: return "UNKNOWN_RESET";
  }
}

static String wifiModeText() {
  switch (WiFi.getMode()) {
    case WIFI_OFF: return "OFF";
    case WIFI_STA: return "STA";
    case WIFI_AP: return "AP";
    case WIFI_AP_STA: return "AP+STA";
    default: return "unbekannt";
  }
}

static String htmlEscape(String s) {
  s.replace("&", "&amp;"); s.replace("<", "&lt;"); s.replace(">", "&gt;");
  s.replace("\"", "&quot;"); s.replace("'", "&#39;"); return s;
}

static bool validIp(const String& text, IPAddress& out) {
  return text.length() && out.fromString(text);
}

static String nowText() {
  struct tm ti;
  if (!getLocalTime(&ti, 20)) return "nicht synchronisiert";
  char buf[32];
  strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M:%S", &ti);
  return String(buf);
}

static float espTemperature() {
  return temperatureRead();
}

static String pageHead(const String& title) {
  String h = F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
  h += "<title>" + htmlEscape(title) + "</title><style>body{font-family:Arial;margin:0;padding:18px;background:#f4f4f4;color:#111}.box{max-width:760px;margin:auto;background:#fff;padding:20px;border-radius:10px;box-shadow:0 1px 5px #bbb}h1{text-align:center}fieldset{margin:14px 0;padding:12px;border:1px solid #ccc;border-radius:7px}label{display:block;font-weight:600;margin-top:9px}input,select{width:100%;box-sizing:border-box;padding:9px;margin-top:4px;font-size:16px}.btn,button,input[type=submit]{display:block;width:100%;box-sizing:border-box;padding:11px;margin:10px 0;text-align:center;border:1px solid #aaa;border-radius:5px;background:#eee;color:#000;text-decoration:none;font-size:16px}.mono{font-family:monospace;word-break:break-all}.ok{color:#087a1c}.bad{color:#a00000}.small{font-size:13px;color:#666}</style></head><body><div class='box'>";
  return h;
}

static String prefString(Preferences& p, const char* key, const char* fallback) {
  return p.isKey(key) ? p.getString(key, fallback) : String(fallback);
}

static bool prefBool(Preferences& p, const char* key, bool fallback) {
  return p.isKey(key) ? p.getBool(key, fallback) : fallback;
}

static uint32_t prefUInt(Preferences& p, const char* key, uint32_t fallback) {
  return p.isKey(key) ? p.getUInt(key, fallback) : fallback;
}

static void loadSettings() {
  Preferences p;
  p.begin("netconf", true);

  // WLAN has two independent credential sources. Arduino secrets are the
  // primary boot candidate when usable; a user-provisioned NVS network is
  // retained as an independent fallback candidate.
  wifiLoadedFromNvs = p.isKey("ssid") && prefString(p, "ssid", "").length();
  nvsWifiSsid = prefString(p, "ssid", "");
  nvsWifiPassword = prefString(p, "wifipw", "");
  // For normal runtime settings, NVS remains authoritative. The values from
  // arduino_secrets.h are used as defaults whenever an NVS key is absent.
  staticIp = prefString(p, "static_ip", UART_STATIC_IP);
  gatewayIp = prefString(p, "gateway", UART_GATEWAY_IP);
  subnetMask = prefString(p, "subnet", UART_SUBNET_MASK);
  apSsid = prefString(p, "ap_ssid", UART_AP_SSID);
  apPassword = prefString(p, "ap_pw", UART_AP_PASSWORD);
  hostname = prefString(p, "hostname", UART_HOSTNAME);
  ntpServer = prefString(p, "ntp_server", UART_NTP_SERVER);

  mqttEnabled = prefBool(p, "mqtt_enabled", UART_MQTT_ENABLED);
  mqttHost = prefString(p, "mqtt_host", UART_MQTT_HOST);
  mqttPort = (uint16_t)prefUInt(p, "mqtt_port", UART_MQTT_PORT);
  mqttUser = prefString(p, "mqtt_user", UART_MQTT_USER);
  mqttPassword = prefString(p, "mqtt_pw", UART_MQTT_PASSWORD);
  mqttTopicBase = prefString(p, "mqtt_topic", UART_MQTT_TOPIC_BASE);
  mqttClientId = prefString(p, "mqtt_client_id", UART_MQTT_CLIENT_ID);
  heartbeatMs = prefUInt(p, "mqtt_hb_ms", UART_MQTT_HEARTBEAT_MS);
  p.end();

  if (hostname.length() == 0) hostname = UART_HOSTNAME;
  if (apSsid.length() == 0) apSsid = UART_AP_SSID;
  if (mqttTopicBase.length() == 0) mqttTopicBase = defaultIdentity();
  if (mqttClientId.length() == 0) mqttClientId = mqttTopicBase;
  if (mqttPort == 0) mqttPort = 1883;
  if (heartbeatMs < 1000) heartbeatMs = 1000;

  Serial.printf("[CONFIG] arduino_secrets.h: %s\n", UART_ESP32_HAS_SECRETS ? "vorhanden" : "nicht vorhanden - sichere Defaults aktiv");
  if (uartSecretTextUsable(UART_WIFI_SSID)) Serial.printf("[CONFIG] WLAN Primaer: arduino_secrets.h (%s)\n", UART_WIFI_SSID);
  if (wifiLoadedFromNvs) Serial.printf("[CONFIG] WLAN Fallback: NVS (%s)\n", nvsWifiSsid.c_str());
  if (!uartSecretTextUsable(UART_WIFI_SSID) && !wifiLoadedFromNvs) Serial.println("[CONFIG] Keine WLAN-Zugangsdaten - AP-Modus wird gestartet");
}

static void saveNetworkSettings() {
  Preferences p; p.begin("netconf", false);
  p.putString("ssid", nvsWifiSsid); p.putString("wifipw", nvsWifiPassword);
  p.putString("static_ip", staticIp); p.putString("gateway", gatewayIp); p.putString("subnet", subnetMask);
  p.putString("ap_ssid", apSsid); p.putString("ap_pw", apPassword);
  p.putString("hostname", hostname); p.putString("ntp_server", ntpServer); p.end();
}

static void saveMqttSettings() {
  Preferences p; p.begin("netconf", false);
  p.putBool("mqtt_enabled", mqttEnabled); p.putString("mqtt_host", mqttHost); p.putUInt("mqtt_port", mqttPort);
  p.putString("mqtt_user", mqttUser); p.putString("mqtt_pw", mqttPassword);
  p.putString("mqtt_topic", mqttTopicBase); p.putString("mqtt_client_id", mqttClientId);
  p.putULong("mqtt_hb_ms", heartbeatMs); p.end();
}

struct WifiCandidate {
  String ssid;
  String password;
  String source;
};

static bool secretWifiAvailable() {
  return UART_SECRET_HAS_WIFI_SSID && uartSecretTextUsable(UART_WIFI_SSID);
}

static bool nvsWifiAvailable() {
  return UART_WIFI_ALLOW_NVS_FALLBACK && nvsWifiSsid.length();
}

static bool wifiCandidatesDuplicate() {
  return secretWifiAvailable() && nvsWifiAvailable() &&
         nvsWifiSsid == String(UART_WIFI_SSID) &&
         nvsWifiPassword == (uartSecretTextUsable(UART_WIFI_PASSWORD) ? String(UART_WIFI_PASSWORD) : String());
}

static uint8_t wifiCandidateCount() {
  uint8_t count = secretWifiAvailable() ? 1 : 0;
  if (nvsWifiAvailable() && !wifiCandidatesDuplicate()) ++count;
  return count;
}

static bool wifiCandidateAt(uint8_t index, WifiCandidate& out) {
  const bool secretOk = secretWifiAvailable();
  const bool nvsOk = nvsWifiAvailable() && !wifiCandidatesDuplicate();

  auto assignSecret = [&out]() {
    out.ssid = UART_WIFI_SSID;
    out.password = uartSecretTextUsable(UART_WIFI_PASSWORD) ? String(UART_WIFI_PASSWORD) : String();
    out.source = "arduino_secrets.h";
  };
  auto assignNvs = [&out]() {
    out.ssid = nvsWifiSsid;
    out.password = nvsWifiPassword;
    out.source = "NVS";
  };

  if (UART_WIFI_PREFER_SECRETS) {
    if (secretOk) { if (index == 0) { assignSecret(); return true; } --index; }
    if (nvsOk && index == 0) { assignNvs(); return true; }
  } else {
    if (nvsOk) { if (index == 0) { assignNvs(); return true; } --index; }
    if (secretOk && index == 0) { assignSecret(); return true; }
  }
  return false;
}

static bool setWifiModeRobust(wifi_mode_t desired) {
  if (WiFi.getMode() == desired) return true;
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    if (WiFi.mode(desired)) {
      delay(100);
      return true;
    }
    Serial.printf("[WIFI] Modus %d fehlgeschlagen, Recovery %u/3\n", (int)desired, (unsigned)(attempt + 1));
    WiFi.mode(WIFI_OFF);
    delay(250);
  }
  return false;
}

static void applyIpConfig(bool forceDhcp) {
  IPAddress ip, gw, mask;
  const bool haveStatic = validIp(staticIp, ip) && validIp(gatewayIp, gw) && validIp(subnetMask, mask);
  if (!forceDhcp && haveStatic) {
    if (WiFi.config(ip, gw, mask)) Serial.printf("[WIFI] IP-Konfiguration: statisch %s\n", ip.toString().c_str());
    else Serial.println("[WIFI] Statische IP fehlgeschlagen - DHCP wird verwendet");
  } else {
    IPAddress none(0, 0, 0, 0);
    WiFi.config(none, none, none);
    if (forceDhcp && haveStatic) Serial.println("[WIFI] Fallback: DHCP statt statischer IP");
  }
}

static String emergencyApSsid() {
  String id = chipId();
  if (id.length() > 4) id = id.substring(id.length() - 4);
  return String(UART_EMERGENCY_AP_PREFIX) + "-" + id;
}

static bool startFallbackAp() {
  if (apActive && (WiFi.getMode() == WIFI_AP_STA || WiFi.getMode() == WIFI_AP)) return true;

  bool modeOk = setWifiModeRobust(WIFI_AP_STA);
  apOnlyRecovery = false;
  if (!modeOk) {
    Serial.println("[WIFI] AP+STA nicht verfuegbar - versuche reinen AP-Modus");
    modeOk = setWifiModeRobust(WIFI_AP);
    apOnlyRecovery = modeOk;
  }
  if (!modeOk) {
    Serial.println("[WIFI] FEHLER: WLAN-AP-Modus konnte nicht aktiviert werden");
    return false;
  }

  WiFi.softAPdisconnect(false);
  delay(80);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));

  bool ok = false;
  if (apPassword.length() >= 8) ok = WiFi.softAP(apSsid.c_str(), apPassword.c_str());
  else {
    if (apPassword.length()) Serial.println("[WIFI] AP-Passwort < 8 Zeichen - starte AP offen");
    ok = WiFi.softAP(apSsid.c_str());
  }

  if (!ok && apPassword.length() >= 8) {
    Serial.println("[WIFI] Geschuetzter AP fehlgeschlagen - versuche denselben AP offen");
    ok = WiFi.softAP(apSsid.c_str());
  }

  if (!ok && UART_WIFI_ALLOW_EMERGENCY_AP) {
    String emergency = emergencyApSsid();
    Serial.printf("[WIFI] Konfigurierter AP fehlgeschlagen - Emergency-AP: %s\n", emergency.c_str());
    WiFi.softAPdisconnect(true);
    delay(100);
    ok = WiFi.softAP(emergency.c_str());
    if (ok) apSsid = emergency;
  }

  apActive = ok;
  if (ok) {
    Serial.printf("[WIFI] Fallback AP: %s, IP %s, Modus %s\n", apSsid.c_str(), WiFi.softAPIP().toString().c_str(), wifiModeText().c_str());
    if (WiFi.getMode() == WIFI_AP_STA) {
      WiFi.scanDelete();
      WiFi.scanNetworks(true, true);
    }
  } else Serial.println("[WIFI] FEHLER: Auch Emergency-AP konnte nicht gestartet werden");
  return ok;
}

static void stopFallbackAp() {
  if (!apActive) return;
  WiFi.softAPdisconnect(true);
  apActive = false;
  apOnlyRecovery = false;
  apShutdownAt = 0;
  Serial.println("[WIFI] Fallback-AP nach erfolgreicher STA-Verbindung beendet");
}

static bool beginWifiAttempt(const WifiCandidate& candidate, bool forceDhcp) {
  wifi_mode_t wantedMode = apActive ? WIFI_AP_STA : WIFI_STA;
  if (!setWifiModeRobust(wantedMode)) {
    Serial.println("[WIFI] Station-Modus konnte nicht aktiviert werden");
    return false;
  }
  WiFi.setHostname(hostname.c_str());
  applyIpConfig(forceDhcp);
  WiFi.disconnect(false, false);
  delay(80);
  WiFi.begin(candidate.ssid.c_str(), candidate.password.c_str());
  pendingWifiSource = candidate.source;
  pendingWifiSsid = candidate.ssid;
  pendingWifiForceDhcp = forceDhcp;
  wifiAttemptActive = true;
  wifiAttemptStarted = millis();
  lastWifiTry = millis();
  ++wifiReconnectCount;
  Serial.printf("[WIFI] Verbindungsversuch %lu: %s (%s)%s\n", (unsigned long)wifiReconnectCount, candidate.ssid.c_str(), candidate.source.c_str(), forceDhcp ? " [DHCP-Fallback]" : "");
  return true;
}

static void markWifiConnected() {
  if (WiFi.status() != WL_CONNECTED) return;
  wifiAttemptActive = false;
  wifiFailureCount = 0;
  wifiDisconnectedSince = 0;
  if (pendingWifiSource.length()) {
    activeWifiSource = pendingWifiSource;
  } else if (!activeWifiSource.length() || activeWifiSource == "keine") {
    activeWifiSource = "bereits verbunden";
  }
  wifiDhcpFallback = pendingWifiForceDhcp;
  Serial.printf("[WIFI] Verbunden: %s, SSID %s, Quelle %s, RSSI %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.SSID().c_str(), activeWifiSource.c_str(), WiFi.RSSI());
  pendingWifiSource = "";
  pendingWifiSsid = "";
  if (apActive && !UART_AP_KEEP_AFTER_CONNECT) apShutdownAt = millis() + UART_AP_SHUTDOWN_DELAY_MS;
}

static bool connectWifiInitial() {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.setHostname(hostname.c_str());

  const uint8_t candidates = wifiCandidateCount();
  if (!candidates) {
    startFallbackAp();
    return false;
  }

  IPAddress ip, gw, mask;
  const bool staticConfigured = validIp(staticIp, ip) && validIp(gatewayIp, gw) && validIp(subnetMask, mask);
  const uint8_t phases = (staticConfigured && UART_WIFI_ALLOW_DHCP_FALLBACK) ? 2 : 1;

  for (uint8_t phase = 0; phase < phases; ++phase) {
    const bool forceDhcp = staticConfigured && phase == 1;
    for (uint8_t i = 0; i < candidates; ++i) {
      WifiCandidate candidate;
      if (!wifiCandidateAt(i, candidate)) continue;
      if (!beginWifiAttempt(candidate, forceDhcp)) continue;
      const unsigned long started = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - started < UART_WIFI_CONNECT_TIMEOUT_MS) {
        delay(250);
        Serial.print('.');
      }
      Serial.println();
      if (WiFi.status() == WL_CONNECTED) {
        wifiDhcpFallback = forceDhcp;
        nextWifiCandidate = (uint8_t)((i + 1) % candidates);
        markWifiConnected();
        return true;
      }
      wifiAttemptActive = false;
      ++wifiFailureCount;
      Serial.printf("[WIFI] Verbindung fehlgeschlagen: %s (%s)\n", candidate.ssid.c_str(), candidate.source.c_str());
    }
  }

  Serial.println("[WIFI] Alle Client-Fallbacks fehlgeschlagen - starte Provisionierungs-AP");
  startFallbackAp();
  wifiDisconnectedSince = millis();
  return false;
}

static void serviceWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (wifiAttemptActive || wifiDisconnectedSince) markWifiConnected();
    if (apActive && !UART_AP_KEEP_AFTER_CONNECT && apShutdownAt && (long)(millis() - apShutdownAt) >= 0) stopFallbackAp();
    return;
  }

  if (!wifiDisconnectedSince) wifiDisconnectedSince = millis();

  if (!apActive && millis() - wifiDisconnectedSince >= UART_WIFI_AP_START_AFTER_MS) startFallbackAp();

  if (wifiAttemptActive) {
    if (millis() - wifiAttemptStarted < UART_WIFI_CONNECT_TIMEOUT_MS) return;
    wifiAttemptActive = false;
    ++wifiFailureCount;
    Serial.printf("[WIFI] Reconnect-Timeout fuer %s (%s), Fehlerzaehler %lu\n", pendingWifiSsid.c_str(), pendingWifiSource.c_str(), (unsigned long)wifiFailureCount);
    pendingWifiSource = "";
    pendingWifiSsid = "";
    return;
  }

  // If AP+STA itself is unavailable, preserve the pure recovery AP instead of
  // tearing it down every reconnect interval. A saved WLAN triggers reboot.
  if (apOnlyRecovery && apActive) return;

  const uint8_t candidates = wifiCandidateCount();
  if (!candidates) {
    if (!apActive) startFallbackAp();
    return;
  }

  if (millis() - lastWifiTry < UART_WIFI_RECONNECT_INTERVAL_MS) return;

  WifiCandidate candidate;
  if (!wifiCandidateAt(nextWifiCandidate % candidates, candidate)) {
    nextWifiCandidate = 0;
    return;
  }
  nextWifiCandidate = (uint8_t)((nextWifiCandidate + 1) % candidates);
  IPAddress ip, gw, mask;
  const bool staticConfigured = validIp(staticIp, ip) && validIp(gatewayIp, gw) && validIp(subnetMask, mask);
  bool forceDhcp = wifiDhcpFallback;
  if (staticConfigured && UART_WIFI_ALLOW_DHCP_FALLBACK && !wifiDhcpFallback) {
    forceDhcp = ((wifiFailureCount / candidates) % 2U) == 1U;
  }
  beginWifiAttempt(candidate, forceDhcp);
}

static void configureTime() {
  setenv("TZ", TZ_CET_CEST, 1); tzset();
  if (ntpServer.length()) configTime(0, 0, ntpServer.c_str());
}

static void publishBattery(bool retained = false) {
  if (!mqtt.isConnected()) return;
  String b = mqttTopicBase + "/battery/";
  mqtt.publish(b + "enabled", battery.enabled ? "true" : "false", true);
  mqtt.publish(b + "status", battery.status, false);
  mqtt.publish(b + "valid", battery.valid ? "true" : "false", false);
  if (battery.valid) {
    mqtt.publish(b + "voltage_v", String(battery.voltage, 3), retained);
    mqtt.publish(b + "adc_voltage_v", String(battery.adcVoltage, 3), false);
    mqtt.publish(b + "raw_mv", String(battery.rawMilliVolts), false);
  }
  mqtt.publish(b + "time", battery.timestamp, false);
}

static void publishSleepStatus() {
  if (!mqtt.isConnected()) return;
  String b = mqttTopicBase + "/sleep/";
  mqtt.publish(b + "enabled", deepSleep.isEnabled() ? "true" : "false", true);
  mqtt.publish(b + "wakeup_mode", deepSleep.wakeupMode(), true);
  mqtt.publish(b + "remaining_online_s", String(deepSleep.remainingOnlineSeconds()), false);
  mqtt.publish(b + "next_wakeup_s", String(deepSleep.nextWakeupSeconds()), false);
  mqtt.publish(b + "next_wakeup_epoch", String(deepSleep.nextWakeupEpoch()), false);
  mqtt.publish(b + "next_wakeup_text", deepSleep.nextWakeupText(), true);
  mqtt.publish(b + "settings_json", deepSleep.settingsJson(), true);
}

static void publishHeartbeat() {
  if (!mqttEnabled) return;
  mqtt.connectIfNeeded();
  if (!mqtt.isConnected()) return;
  String b = mqttTopicBase + "/status/";
  mqtt.publish(b + "online", "true", true);
  mqtt.publish(b + "hostname", hostname, true);
  mqtt.publish(b + "ip", WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString(), true);
  mqtt.publish(b + "rssi", WiFi.status() == WL_CONNECTED ? String(WiFi.RSSI()) : "", false);
  mqtt.publish(b + "uptime_s", String(millis()/1000UL), false);
  mqtt.publish(b + "heartbeat_interval_s", String(heartbeatMs/1000UL), true);
  mqtt.publish(b + "esp32_temp_c", String(espTemperature(), 2), false);
  mqtt.publish(b + "time", nowText(), false);
  mqtt.publish(b + "mqtt_status", mqtt.getLastStatus(), false);
  mqtt.publish(b + "firmware", FW_NAME, true);
  publishBattery(false);
  publishSleepStatus();
  lastHeartbeat = millis();
}

static void beforeSleep(const String& reason) {
  battery.measure("before_sleep");
  if (mqtt.isConnected()) {
    mqtt.publish(mqttTopicBase + "/sleep/state", "sleeping", false);
    mqtt.publish(mqttTopicBase + "/sleep/reason", reason, false);
    publishBattery(false);
    publishSleepStatus();
  }
}

static void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String full(topic), body;
  for (unsigned int i=0;i<length;i++) body += (char)payload[i];
  String prefix = mqttTopicBase + "/cmd/";
  if (!full.startsWith(prefix)) return;
  String cmd = full.substring(prefix.length());
  bool handled = true;

  if (cmd == "heartbeat_interval_s") {
    const long requestedSeconds = body.toInt();
    const uint32_t seconds = requestedSeconds > 1L ? (uint32_t)requestedSeconds : 1U;
    heartbeatMs = seconds * 1000U; saveMqttSettings();
  } else if (cmd == "status/get" || cmd == "get") {
    publishHeartbeat();
  } else if (cmd == "battery/measure") {
    battery.measure("mqtt"); publishBattery(false);
  } else if (cmd == "reboot") {
    mqtt.publish(mqttTopicBase + "/event/last_command", "reboot", false); delay(150); ESP.restart();
  } else if (cmd == "sleep/get") {
    publishSleepStatus();
  } else if (cmd.startsWith("sleep/")) {
    String sub = cmd.substring(6);
    if (sub == "now") mqtt.publish(mqttTopicBase + "/event/last_command", "sleep/now", false);
    handled = deepSleep.handleMqttCommand(sub, body);
    if (handled && sub != "now") publishSleepStatus();
  } else handled = false;

  if (handled && cmd != "reboot" && cmd != "sleep/now") {
    mqtt.publish(mqttTopicBase + "/event/last_command", cmd, false);
    mqtt.publish(mqttTopicBase + "/event/last_payload", body, false);
  }
}

static String mainPage() {
  String h = pageHead(FW_NAME);
  h += "<meta http-equiv='refresh' content='10'>";
  h += "<h1>" + String(FW_NAME) + "</h1><p class='small'>Firmware " + String(FW_BUILD_VERSION) + " | Build " + String(__DATE__) + " " + String(__TIME__) + "</p>";
  h += "<fieldset><legend>System</legend><p><b>Chip:</b> ESP32-WROOM-32 / NodeMCU-32S</p>";
  h += "<p><b>Resetgrund:</b> " + resetReasonText() + "</p><p><b>Zeit:</b> " + nowText() + "</p><p><b>Uptime:</b> " + String(millis()/1000UL) + " s</p>";
  h += "<p><b>ESP32 Temperatur:</b> " + String(espTemperature(),2) + " &deg;C</p></fieldset>";
  h += "<fieldset><legend>Netzwerk</legend><p><b>WLAN:</b> " + String(WiFi.status()==WL_CONNECTED ? "verbunden" : "nicht verbunden") + "</p>";
  h += "<p><b>Modus:</b> " + wifiModeText() + " &nbsp; <b>Quelle:</b> " + htmlEscape(activeWifiSource) + "</p>";
  if (WiFi.status()==WL_CONNECTED) h += "<p><b>SSID:</b> " + htmlEscape(WiFi.SSID()) + "<br><b>IP:</b> " + WiFi.localIP().toString() + " &nbsp; <b>RSSI:</b> " + String(WiFi.RSSI()) + " dBm</p>";
  if (apActive) h += "<p><b>Fallback-AP:</b> " + htmlEscape(apSsid) + " / " + WiFi.softAPIP().toString() + "</p>";
  h += "<p><b>Reconnects:</b> " + String(wifiReconnectCount) + " &nbsp; <b>Fehler:</b> " + String(wifiFailureCount) + "</p></fieldset>";
  h += "<fieldset><legend>MQTT</legend><p><b>Status:</b> " + mqtt.getLastStatus() + "</p><p class='mono'><b>Basis:</b> " + htmlEscape(mqttTopicBase) + "</p></fieldset>";
  h += "<fieldset><legend>Batterie fuer Deep Sleep</legend><p><b>Status:</b> " + battery.status + "</p>";
  if (battery.valid) h += "<p><b>Spannung:</b> " + String(battery.voltage,3) + " V</p>";
  h += "</fieldset>";
  h += "<fieldset><legend>Deep Sleep</legend><p><b>Aktiv:</b> " + String(deepSleep.isEnabled()?"ja":"nein") + "</p><p><b>Modus:</b> " + deepSleep.wakeupMode() + "</p><p><b>Naechster Wakeup:</b> " + deepSleep.nextWakeupText() + "</p></fieldset>";
  h += "<a class='btn' href='/network'>WLAN / NTP</a><a class='btn' href='/mqtt'>MQTT</a><a class='btn' href='/battery'>Batterie / ADC</a><a class='btn' href='/deepsleep'>Deep Sleep</a><a class='btn' href='/ota'>OTA Update</a>";
  h += "<form method='post' action='/reboot'><button type='submit'>ESP32 neu starten</button></form></div></body></html>";
  return h;
}

static String networkPage() {
  String h = pageHead("WLAN / NTP");
  h += "<h1>WLAN / NTP</h1>";
  h += "<fieldset><legend>Status</legend><p><b>Modus:</b> " + wifiModeText() + "</p><p><b>STA:</b> " + String(WiFi.status()==WL_CONNECTED ? "verbunden" : "getrennt") + "</p>";
  h += "<p><b>Aktive Quelle:</b> " + htmlEscape(activeWifiSource) + "</p><p><b>Reconnect-Versuche:</b> " + String(wifiReconnectCount) + " &nbsp; <b>Fehler:</b> " + String(wifiFailureCount) + "</p>";
  if (WiFi.status()==WL_CONNECTED) h += "<p><b>SSID:</b> " + htmlEscape(WiFi.SSID()) + "<br><b>IP:</b> " + WiFi.localIP().toString() + "<br><b>RSSI:</b> " + String(WiFi.RSSI()) + " dBm</p>";
  if (apActive) h += "<p><b>AP:</b> " + htmlEscape(apSsid) + " / " + WiFi.softAPIP().toString() + "</p>";
  h += "</fieldset>";
  h += "<p class='small'>Boot-Fallback: 1) gueltige WLAN-Daten aus arduino_secrets.h (standardmaessig bevorzugt), 2) separat gespeichertes NVS-WLAN, 3) bei statischer IP zusaetzlicher DHCP-Versuch, 4) AP+STA-Provisionierung, 5) offener Emergency-AP falls der konfigurierte AP nicht startet.</p>";

  int scanState = WiFi.scanComplete();
  if (scanState == WIFI_SCAN_FAILED && WiFi.getMode() == WIFI_AP_STA) {
    WiFi.scanNetworks(true, true);
    scanState = WIFI_SCAN_RUNNING;
  }

  h += "<form method='post' action='/save_network'><fieldset><legend>WLAN Client / NVS-Fallback</legend>";
  if (UART_SECRET_HAS_WIFI_SSID && uartSecretTextUsable(UART_WIFI_SSID)) h += "<p class='small'>arduino_secrets.h WLAN: <b>" + htmlEscape(String(UART_WIFI_SSID)) + "</b> (Primaerkandidat; Passwort wird nicht angezeigt)</p>";
  if (nvsWifiSsid.length()) h += "<p class='small'>NVS WLAN: <b>" + htmlEscape(nvsWifiSsid) + "</b> (Fallback)</p>";
  h += "<label>Gefundene WLANs</label><select name='ssid_scan' onchange=\"if(this.value){document.getElementById('ssid_manual').value=this.value;}\"><option value=''>-- manuelle SSID verwenden --</option>";
  if (scanState >= 0) {
    for (int i = 0; i < scanState; ++i) {
      String ssid = WiFi.SSID(i);
      if (!ssid.length()) continue;
      h += "<option value='" + htmlEscape(ssid) + "'>" + htmlEscape(ssid) + " (" + String(WiFi.RSSI(i)) + " dBm";
      h += WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? ", offen" : ", geschuetzt";
      h += ")</option>";
    }
  }
  h += "</select>";
  if (scanState == WIFI_SCAN_RUNNING) h += "<p class='small'>WLAN-Scan laeuft. Seite in wenigen Sekunden neu laden.</p>";
  else if (WiFi.getMode() == WIFI_AP_STA || WiFi.getMode() == WIFI_STA) h += "<p class='small'><a href='/wifi_rescan'>WLANs neu scannen</a></p>";
  else h += "<p class='small'>Scan im reinen AP-Recovery-Modus nicht verfuegbar; SSID kann manuell eingetragen werden.</p>";

  h += "<label>SSID fuer NVS-Fallback</label><input id='ssid_manual' name='ssid' value='"+htmlEscape(nvsWifiSsid)+"'>";
  h += "<label>Passwort</label><input type='password' name='wifipw' placeholder='leer = bei gleicher SSID unveraendert'>";
  h += "<label>Statische IP (leer = DHCP)</label><input name='static_ip' value='"+htmlEscape(staticIp)+"'><label>Gateway</label><input name='gateway' value='"+htmlEscape(gatewayIp)+"'><label>Subnetz</label><input name='subnet' value='"+htmlEscape(subnetMask)+"'></fieldset>";
  h += "<fieldset><legend>Fallback Access Point</legend><label>AP SSID</label><input name='ap_ssid' value='"+htmlEscape(apSsid)+"'><label>AP Passwort (leer = offen, sonst min. 8 Zeichen)</label><input type='password' name='ap_pw' placeholder='leer = unveraendert'><p class='small'>Falls dieser AP nicht startet, verwendet die Firmware automatisch einen offenen Emergency-AP mit eindeutiger Chip-ID.</p></fieldset>";
  h += "<fieldset><legend>System</legend><label>Hostname</label><input name='hostname' value='"+htmlEscape(hostname)+"'><label>NTP Server</label><input name='ntp_server' value='"+htmlEscape(ntpServer)+"'><p class='small'>Zeitzone: Deutschland (CET/CEST), Sommer-/Winterzeit automatisch.</p></fieldset><button type='submit'>NVS-Fallback speichern und neu starten</button></form>";
  h += "<form method='post' action='/clear_wifi_nvs'><button type='submit'>Gespeichertes NVS-WLAN loeschen</button></form><a class='btn' href='/'>Zurueck</a></div></body></html>";
  return h;
}

static String mqttPage() {
  String h=pageHead("MQTT"); h += "<h1>MQTT</h1><form method='post' action='/save_mqtt'><fieldset><legend>Verbindung</legend>";
  h += "<label>MQTT aktiv</label><select name='enabled'><option value='0'"+String(mqttEnabled?"":" selected")+">AUS</option><option value='1'"+String(mqttEnabled?" selected":"")+">EIN</option></select>";
  h += "<label>Broker</label><input name='host' value='"+htmlEscape(mqttHost)+"'><label>Port</label><input type='number' min='1' max='65535' name='port' value='"+String(mqttPort)+"'><label>Benutzer</label><input name='user' value='"+htmlEscape(mqttUser)+"'><label>Passwort</label><input type='password' name='password' placeholder='leer = unveraendert'></fieldset>";
  h += "<fieldset><legend>Topics</legend><label>Topic-Basis</label><input name='topic' value='"+htmlEscape(mqttTopicBase)+"'><label>Client-ID</label><input name='client_id' value='"+htmlEscape(mqttClientId)+"'><label>Heartbeat [s]</label><input type='number' min='1' name='heartbeat_s' value='"+String(heartbeatMs/1000UL)+"'><p class='small'>Fernsteuerung: &lt;Basis&gt;/cmd/#</p></fieldset><button type='submit'>Speichern</button></form>";
  h += "<form method='post' action='/test_mqtt'><button type='submit'>Verbindung testen</button></form><p><b>Status:</b> "+mqtt.getLastStatus()+"</p><a class='btn' href='/'>Zurueck</a></div></body></html>"; return h;
}

static String batteryPage() {
  String h=pageHead("Batterie / ADC"); h += "<h1>Batterie / ADC</h1><p>Nur fuer batterieabhaengigen Deep Sleep. Beim ESP32-WROOM werden bewusst nur ADC1 GPIO32..39 zugelassen.</p><form method='post' action='/save_battery'>";
  h += "<label>Messung aktiv</label><select name='enabled'><option value='0'"+String(battery.enabled?"":" selected")+">AUS</option><option value='1'"+String(battery.enabled?" selected":"")+">EIN</option></select>";
  h += "<label>ADC1 GPIO</label><input type='number' min='32' max='39' name='pin' value='"+String(battery.adcPin)+"'><label>R1 Batterie+ -> ADC [Ohm]</label><input type='number' name='r1' value='"+String(battery.r1,0)+"'><label>R2 ADC -> GND [Ohm]</label><input type='number' name='r2' value='"+String(battery.r2,0)+"'><label>Kalibrierfaktor</label><input type='number' step='0.001' name='cal' value='"+String(battery.calibration,3)+"'><label>Mittelungen</label><input type='number' min='1' max='200' name='samples' value='"+String(battery.samples)+"'><button type='submit'>Speichern</button></form>";
  h += "<form method='post' action='/measure_battery'><button type='submit'>Jetzt messen</button></form><p><b>Status:</b> "+battery.status+"</p>";
  if (battery.valid) h += "<p><b>Batterie:</b> "+String(battery.voltage,3)+" V &nbsp; <b>ADC:</b> "+String(battery.adcVoltage,3)+" V</p>";
  h += "<a class='btn' href='/'>Zurueck</a></div></body></html>"; return h;
}

static void registerRoutes() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* r){ r->send(200,"text/html; charset=utf-8",mainPage()); });
  server.on("/network", HTTP_GET, [](AsyncWebServerRequest* r){ r->send(200,"text/html; charset=utf-8",networkPage()); });
  server.on("/wifi_rescan", HTTP_GET, [](AsyncWebServerRequest* r){
    if (WiFi.getMode() == WIFI_AP_STA || WiFi.getMode() == WIFI_STA) {
      WiFi.scanDelete();
      WiFi.scanNetworks(true, true);
    }
    r->redirect("/network");
  });
  server.on("/save_network", HTTP_POST, [](AsyncWebServerRequest* r){
    String oldSsid = nvsWifiSsid;
    String selectedSsid = r->hasArg("ssid_scan") ? r->arg("ssid_scan") : String();
    selectedSsid.trim();
    if (selectedSsid.length()) nvsWifiSsid = selectedSsid;
    else if (r->hasArg("ssid")) { nvsWifiSsid = r->arg("ssid"); nvsWifiSsid.trim(); }
    if (r->hasArg("wifipw") && r->arg("wifipw").length()) nvsWifiPassword=r->arg("wifipw");
    else if (nvsWifiSsid != oldSsid) nvsWifiPassword = "";
    if (r->hasArg("static_ip")) staticIp=r->arg("static_ip"); if (r->hasArg("gateway")) gatewayIp=r->arg("gateway"); if (r->hasArg("subnet")) subnetMask=r->arg("subnet");
    if (r->hasArg("ap_ssid")) apSsid=r->arg("ap_ssid"); if (r->hasArg("ap_pw") && r->arg("ap_pw").length()) apPassword=r->arg("ap_pw");
    if (r->hasArg("hostname")) hostname=r->arg("hostname"); if (r->hasArg("ntp_server")) ntpServer=r->arg("ntp_server");
    saveNetworkSettings(); r->send(200,"text/html; charset=utf-8",pageHead("Gespeichert")+"<h1>Gespeichert</h1><p>Neustart ...</p></div></body></html>"); delay(500); ESP.restart();
  });
  server.on("/clear_wifi_nvs", HTTP_POST, [](AsyncWebServerRequest* r){
    Preferences p; p.begin("netconf", false); p.remove("ssid"); p.remove("wifipw"); p.end();
    nvsWifiSsid = ""; nvsWifiPassword = "";
    r->send(200,"text/html; charset=utf-8",pageHead("WLAN geloescht")+"<h1>NVS-WLAN geloescht</h1><p>Neustart ...</p></div></body></html>");
    delay(500); ESP.restart();
  });
  server.on("/mqtt", HTTP_GET, [](AsyncWebServerRequest* r){ r->send(200,"text/html; charset=utf-8",mqttPage()); });
  server.on("/save_mqtt", HTTP_POST, [](AsyncWebServerRequest* r){
    if(r->hasArg("enabled")) mqttEnabled=r->arg("enabled").toInt(); if(r->hasArg("host")) mqttHost=r->arg("host"); if(r->hasArg("port")) mqttPort=r->arg("port").toInt();
    if(r->hasArg("user")) mqttUser=r->arg("user"); if(r->hasArg("password")&&r->arg("password").length()) mqttPassword=r->arg("password"); if(r->hasArg("topic")) mqttTopicBase=r->arg("topic"); if(r->hasArg("client_id")) mqttClientId=r->arg("client_id");
    if(r->hasArg("heartbeat_s")) {
      const long requestedSeconds = r->arg("heartbeat_s").toInt();
      const uint32_t seconds = requestedSeconds > 1L ? (uint32_t)requestedSeconds : 1U;
      heartbeatMs = seconds * 1000U;
    }
    if(mqttTopicBase.length() == 0) mqttTopicBase=defaultIdentity(); if(mqttClientId.length() == 0) mqttClientId=mqttTopicBase;
    saveMqttSettings(); mqtt.disconnect(); mqtt.configure(mqttHost,mqttPort,mqttUser,mqttPassword,mqttTopicBase,mqttClientId,mqttEnabled); r->redirect("/mqtt");
  });
  server.on("/test_mqtt", HTTP_POST, [](AsyncWebServerRequest* r){ bool ok=mqtt.tryConnectOnce(); r->send(200,"text/html; charset=utf-8",pageHead("MQTT Test")+"<h1>MQTT Test</h1><p>"+String(ok?"Verbindung OK":"Verbindung fehlgeschlagen")+"</p><p>"+mqtt.getLastStatus()+"</p><a class='btn' href='/mqtt'>Zurueck</a></div></body></html>"); });
  server.on("/battery", HTTP_GET, [](AsyncWebServerRequest* r){ r->send(200,"text/html; charset=utf-8",batteryPage()); });
  server.on("/save_battery", HTTP_POST, [](AsyncWebServerRequest* r){
    if(r->hasArg("enabled")) battery.enabled=r->arg("enabled").toInt(); if(r->hasArg("pin")) battery.adcPin=r->arg("pin").toInt(); if(r->hasArg("r1")) battery.r1=r->arg("r1").toFloat(); if(r->hasArg("r2")) battery.r2=r->arg("r2").toFloat(); if(r->hasArg("cal")) battery.calibration=r->arg("cal").toFloat(); if(r->hasArg("samples")) battery.samples=r->arg("samples").toInt();
    if(!battery.isValidAdc1Pin()) battery.adcPin=34; if(battery.r1<1) battery.r1=100000; if(battery.r2<1) battery.r2=33000; if(battery.samples<1) battery.samples=1; if(battery.samples>200) battery.samples=200; battery.save(); battery.begin(); battery.measure("web_save"); r->redirect("/battery");
  });
  server.on("/measure_battery", HTTP_POST, [](AsyncWebServerRequest* r){ battery.measure("web"); publishBattery(false); r->redirect("/battery"); });
  server.on("/reboot", HTTP_POST, [](AsyncWebServerRequest* r){ r->send(200,"text/plain","Neustart"); delay(250); ESP.restart(); });
  server.onNotFound([](AsyncWebServerRequest* r){ r->send(404,"text/plain","404 - nicht gefunden"); });
  deepSleep.registerRoutes(server);
  ota.registerRoutes(server);
}

void setup() {
  Serial.begin(115200); delay(200);
  Serial.printf("\n%s %s\n", FW_NAME, FW_BUILD_VERSION);
  Serial.printf("[BOOT] Build: %s %s\n", __DATE__, __TIME__);
  Serial.printf("[BOOT] Resetgrund: %s (%d)\n", resetReasonText().c_str(), (int)esp_reset_reason());
  loadSettings();
  battery.load(); battery.begin(); battery.measure("boot");
  deepSleep.begin(); deepSleep.setBeforeSleepCallback(beforeSleep);
  ota.begin(otaPrefs);

  connectWifiInitial();
  configureTime();

  mqtt.begin(wifiClient); mqtt.setCallback(mqttCallback);
  mqtt.configure(mqttHost,mqttPort,mqttUser,mqttPassword,mqttTopicBase,mqttClientId,mqttEnabled);

  registerRoutes(); server.begin();

  if (WiFi.status()==WL_CONNECTED) { ota.startArduinoOta(hostname); otaStarted=true; }
  if (mqttEnabled) { mqtt.tryConnectOnce(); if(mqtt.isConnected()) publishHeartbeat(); }
}

void loop() {
  ota.loop(); mqtt.loop();

  serviceWifi();
  if (WiFi.status()==WL_CONNECTED && !otaStarted) { configureTime(); ota.startArduinoOta(hostname); otaStarted=true; }

  if (mqttEnabled && millis()-lastMqttTry >= 5000UL) {
    lastMqttTry=millis(); bool was=mqtt.isConnected(); mqtt.connectIfNeeded(); if(!was && mqtt.isConnected()) publishHeartbeat();
  }
  if (millis()-lastBattery >= 60000UL) { lastBattery=millis(); battery.measure("periodic"); }
  if (mqttEnabled && (millis()-lastHeartbeat >= heartbeatMs)) publishHeartbeat();

  deepSleep.loop(mqtt.isConnected(), battery.valid ? battery.voltage : NAN);
  delay(2);
}
