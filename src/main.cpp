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
#include "UartManager.h"
#include "ConfigDefaults.h"

static const char* FW_NAME = "Uart_Esp32";
static const char* FW_BUILD_VERSION = "v0.16";
static const char* TZ_CET_CEST = "CET-1CEST,M3.5.0,M10.5.0/3";

AsyncWebServer server(80);
WiFiClient wifiClient;
MqttManager mqtt;
DeepSleepManager deepSleep;
OtaManager ota;
BatteryMonitor battery;
UartManager uartMonitor;
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
  h += "<title>" + htmlEscape(title) + "</title><style>body{font-family:Arial;margin:0;padding:18px;background:#f4f4f4;color:#111}.box{max-width:760px;margin:auto;background:#fff;padding:20px;border-radius:10px;box-shadow:0 1px 5px #bbb}h1{text-align:center}fieldset{margin:14px 0;padding:12px;border:1px solid #ccc;border-radius:7px}label{display:block;font-weight:600;margin-top:9px}input,select{width:100%;box-sizing:border-box;padding:9px;margin-top:4px;font-size:16px}.btn,button,input[type=submit]{display:block;width:100%;box-sizing:border-box;padding:11px;margin:10px 0;text-align:center;border:1px solid #aaa;border-radius:5px;background:#eee;color:#000;text-decoration:none;font-size:16px}.mono{font-family:monospace;word-break:break-all}.raw{font-family:monospace;white-space:pre-wrap;background:#111;color:#eee;padding:10px;border-radius:5px;min-height:90px;overflow:auto}.terminal{font-family:monospace;white-space:pre-wrap;background:#0a0a0a;color:#e8e8e8;padding:12px;border-radius:5px;min-height:320px;max-height:60vh;overflow:auto;word-break:break-word}.tbl{width:100%;border-collapse:collapse}.tbl th,.tbl td{border:1px solid #ccc;padding:7px;text-align:left}.tbl th{background:#eee}.ok{color:#087a1c}.bad{color:#a00000}.warn{background:#fff3cd;border:1px solid #e4c96a;padding:10px;border-radius:5px}.small{font-size:13px;color:#666}.inlinecheck{display:flex;gap:8px;align-items:center;margin-top:8px}.inlinecheck input{width:auto;margin:0}</style></head><body><div class='box'>";
  return h;
}

static String uartNav() {
  String h;
  h += "<a class='btn' href='/uart/settings'>UART Einstellungen</a>";
  h += "<a class='btn' href='/uart/console'>UART Konsole</a>";
  h += "<a class='btn' href='/uart/decoder'>UART Decoder</a>";
  h += "<a class='btn' href='/uart/probe'>UART Probe-Runner</a>";
  return h;
}

static int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + c - 'a';
  if (c >= 'A' && c <= 'F') return 10 + c - 'A';
  return -1;
}

static bool parseHexBytes(const String& input, uint8_t* out, size_t capacity, size_t& outLen) {
  outLen = 0;
  int high = -1;
  for (size_t i = 0; i < input.length(); ++i) {
    const char c = input[i];
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ':' || c == ',' || c == '-') continue;
    const int n = hexNibble(c);
    if (n < 0) return false;
    if (high < 0) high = n;
    else {
      if (outLen >= capacity) return false;
      out[outLen++] = (uint8_t)((high << 4) | n);
      high = -1;
    }
  }
  return high < 0;
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

static void publishUartStatus() {
  if (!mqtt.isConnected()) return;
  String b = mqttTopicBase + "/uart/";
  mqtt.publish(b + "mode", uartMonitor.modeText(), true);
  mqtt.publish(b + "enabled", uartMonitor.enabled() ? "true" : "false", true);
  mqtt.publish(b + "tx_enabled", uartMonitor.txEnabled() ? "true" : "false", true);
  mqtt.publish(b + "tx_owner", uartMonitor.txOwnerText(), false);
  mqtt.publish(b + "status", uartMonitor.status(), false);
  mqtt.publish(b + "bytes", String((unsigned long)(uartMonitor.totalBytes() & 0xFFFFFFFFULL)), false);
  mqtt.publish(b + "packets", String(uartMonitor.packetCount()), false);
  mqtt.publish(b + "valid_packets", String(uartMonitor.validPacketCount()), false);
  mqtt.publish(b + "invalid_packets", String(uartMonitor.invalidPacketCount()), false);
  mqtt.publish(b + "last_route", String(uartMonitor.lastRoute()), false);
  mqtt.publish(b + "last_inner_type", String(uartMonitor.lastInnerType()), false);
  mqtt.publish(b + "last_command", String(uartMonitor.lastCommand()), false);
  mqtt.publish(b + "last_data_length", String(uartMonitor.lastDataLength()), false);
  mqtt.publish(b + "cmd0021_count", String(uartMonitor.command0021Count()), false);
  mqtt.publish(b + "cmd0023_count", String(uartMonitor.command0023Count()), false);
  mqtt.publish(b + "cmd0031_count", String(uartMonitor.command0031Count()), false);
  mqtt.publish(b + "cmd0033_count", String(uartMonitor.command0033Count()), false);
  if (!uartMonitor.hasMainPacket()) return;
  for (uint8_t i=0;i<6;++i) {
    String f = b + "field" + String(i+1) + "/";
    mqtt.publish(f + "raw", String(uartMonitor.rawField(i)), false);
    mqtt.publish(f + "alias", uartMonitor.fieldAlias(i), true);
    if (i < 5) mqtt.publish(f + "norm", String(uartMonitor.normalizedField(i), 4), false);
  }
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
  mqtt.publish(b + "uart_mode", uartMonitor.modeText(), true);
  mqtt.publish(b + "uart_bytes", String((unsigned long)(uartMonitor.totalBytes() & 0xFFFFFFFFULL)), false);
  publishUartStatus();
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
  h += "<fieldset><legend>UART</legend><p><b>Basisbetrieb:</b> " + String(uartMonitor.enabled()?"aktiviert":"deaktiviert") + "</p><p><b>Status:</b> " + htmlEscape(uartMonitor.status()) + "</p><p><b>TX:</b> " + htmlEscape(uartMonitor.txOwnerText()) + "</p><p><b>Empfangene Bytes:</b> " + String((unsigned long)(uartMonitor.totalBytes() & 0xFFFFFFFFULL)) + "</p></fieldset>";
  h += uartNav(); h += "<a class='btn' href='/network'>WLAN / NTP</a><a class='btn' href='/mqtt'>MQTT</a><a class='btn' href='/battery'>Batterie / ADC</a><a class='btn' href='/deepsleep'>Deep Sleep</a><a class='btn' href='/ota'>OTA Update</a>";
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

static String uartDecoderPage() {
  String h = pageHead("UART Decoder");
  h += "<h1>UART Decoder - st10</h1>";
  h += "<p class='small'>Der Decoder arbeitet seit v0.15 parallel zur UART Konsole auf demselben RX-Datenstrom. Ein Umschalten zwischen Konsole und Decoder ist nicht mehr erforderlich. OUTER: <b>FF FB Route OuterLength</b>; INNER: <b>FF FD/FE InnerLength Command DATA XOR</b>.</p>";
  h += "<fieldset><legend>Live-Status</legend><p><b>UART:</b> <span id='uart_status'>" + htmlEscape(uartMonitor.status()) + "</span></p><p><b>Live-API:</b> <span id='uart_api'>warte ...</span></p><p><b>RX Bytes:</b> <span id='uart_bytes'>" + String((unsigned long)(uartMonitor.totalBytes() & 0xFFFFFFFFULL)) + "</span> &nbsp; <b>TX:</b> <span id='tx_owner'>" + htmlEscape(uartMonitor.txOwnerText()) + "</span></p>";
  h += "<p><b>Letzter Frame:</b> Route <span id='last_route'>-</span> &nbsp; Inner <span id='last_inner'>-</span> &nbsp; Command <span id='last_cmd'>-</span> &nbsp; DataLen <span id='last_dlen'>-</span></p>";
  h += "<table class='tbl'><tr><th>Technisches Feld 0x0021</th><th>Alias</th><th>Rohwert</th><th>Normiert</th></tr>";
  for (uint8_t i=0;i<6;++i) {
    String alias = uartMonitor.fieldAlias(i); if (!alias.length()) alias = "-";
    h += "<tr><td><b>Feld " + String(i+1) + "</b></td><td>" + htmlEscape(alias) + "</td><td id='raw" + String(i+1) + "'>-</td><td id='norm" + String(i+1) + "'>-</td></tr>";
  }
  h += "</table><p><b>Frames:</b> <span id='pkt'>0</span> &nbsp; <b>gueltig:</b> <span id='pkt_ok'>0</span> &nbsp; <b>ungueltig:</b> <span id='pkt_bad'>0</span></p>";
  h += "<p><b>0x0021:</b> <span id='cmd21'>0</span> &nbsp; <b>0x0023:</b> <span id='cmd23'>0</span> &nbsp; <b>0x0031:</b> <span id='cmd31'>0</span> &nbsp; <b>0x0033:</b> <span id='cmd33'>0</span></p></fieldset>";

  h += "<fieldset><legend>Weitere bekannte Commands</legend>";
  h += "<p><b>0x0023:</b> V1=<span id='c23v1'>-</span> &nbsp; V2=<span id='c23v2'>-</span> &nbsp; V3=<span id='c23v3'>-</span> <span class='small'>(Semantik offen)</span></p>";
  h += "<p><b>0x0031:</b> Wert=<span id='c31v'>-</span> &nbsp; Status=<span id='c31s'>-</span></p>";
  h += "<p><b>0x0033:</b> Felder=<span id='c33f'>-</span> <span class='small'>(2 Meta-Bytes + 6 x uint16 LE; Reihenfolge/Funktion offen)</span></p></fieldset>";

  String field5Alias = uartMonitor.fieldAlias(4); if (!field5Alias.length()) field5Alias = "-";
  h += "<fieldset><legend>Experimentelles Senden / Paketkopie - Feld 5</legend>";
  h += "<p class='small'><b>Wichtig:</b> Diese bestehende TX-Funktion ist weiterhin experimentell. Sie ist nur nutzbar, wenn TX unter UART Einstellungen bewusst freigegeben wurde. Waehren der Probe-Runner sendet, ist die Konsole fuer TX gesperrt.</p>";
  h += "<p><b>Feld 5 Alias:</b> " + htmlEscape(field5Alias) + " &nbsp; <b>TX GPIO:</b> " + String(uartMonitor.txPin()) + "</p>";
  h += "<p><b>TX Status:</b> <span id='tx_status'>" + htmlEscape(uartMonitor.txStatus()) + "</span></p>";
  h += "<p><b>Ziel:</b> <span id='tx_target'>" + String(uartMonitor.txTargetNormalized(), 2) + "</span> &nbsp; <b>Feld-5-Rohwert:</b> <span id='tx_raw'>" + String(uartMonitor.txField5Raw()) + "</span> &nbsp; <b>gesendete Pakete:</b> <span id='tx_packets'>" + String(uartMonitor.txPacketCount()) + "</span></p>";
  h += "<form method='post' action='/uart/send_f5_neg'><button id='tx_neg_btn' type='submit'>Feld 5 = -0,5 fuer ca. 1 s senden</button></form>";
  h += "<form method='post' action='/uart/send_f5_pos'><button id='tx_pos_btn' type='submit'>Feld 5 = +0,5 fuer ca. 1 s senden</button></form></fieldset>";

  h += "<form method='post' action='/save_uart_decoder'><fieldset><legend>Decoder-Feldnamen und Kalibrierung</legend><p class='small'>Diese Werte aendern nur die Darstellung/Auswertung des st10-Decoders; die Hardware-UART-Konfiguration bleibt unveraendert.</p>";
  for (uint8_t i=0;i<6;++i) h += "<label>Alias fuer Feld " + String(i+1) + "</label><input maxlength='32' name='label" + String(i+1) + "' value='" + htmlEscape(uartMonitor.fieldAlias(i)) + "'>";
  h += "<label>Totzone 0.00 ... 0.50</label><input type='number' step='0.001' min='0' max='0.5' name='deadband' value='" + String(uartMonitor.deadband(),3) + "'>";
  h += "<table class='tbl'><tr><th>Feld</th><th>Minimum</th><th>Mitte</th><th>Maximum</th></tr>";
  for (uint8_t i=0;i<5;++i) {
    auto c = uartMonitor.calibration(i);
    h += "<tr><td>Feld " + String(i+1) + "</td><td><input type='number' name='min" + String(i+1) + "' value='" + String(c.minV) + "'></td><td><input type='number' name='ctr" + String(i+1) + "' value='" + String(c.centerV) + "'></td><td><input type='number' name='max" + String(i+1) + "' value='" + String(c.maxV) + "'></td></tr>";
  }
  h += "</table><button type='submit'>Decoder-Einstellungen speichern</button></fieldset></form>";

  h += "<fieldset><legend>Letzte Rohdaten</legend><p class='small'>Die Rohdaten werden unabhaengig von Konsole und Decoder in einem 512-Byte-Ringpuffer mitgefuehrt.</p><b>HEX</b><pre class='raw' id='raw_hex'></pre><b>ASCII</b><pre class='raw' id='raw_ascii'></pre></fieldset>";
  h += "<form method='post' action='/uart_clear'><button type='submit'>Rohdatenpuffer / Bytezaehler leeren</button></form>";
  h += uartNav(); h += "<a class='btn' href='/'>Zurueck</a>";
  h += R"rawliteral(<script>
function hx(v,w){return '0x'+Number(v).toString(16).toUpperCase().padStart(w,'0');}
async function updateUart(){const api=document.getElementById('uart_api');try{const r=await fetch('/uart/status',{cache:'no-store'});const d=await r.json();
api.textContent='OK';document.getElementById('uart_status').textContent=d.status;document.getElementById('uart_bytes').textContent=d.total_bytes;document.getElementById('tx_owner').textContent=d.tx_owner;
document.getElementById('last_route').textContent=hx(d.last_route,2);document.getElementById('last_inner').textContent=hx(d.last_inner_type,2)+' / IL='+d.last_inner_length;document.getElementById('last_cmd').textContent=hx(d.last_command,4);document.getElementById('last_dlen').textContent=d.last_data_length;
document.getElementById('pkt').textContent=d.packets;document.getElementById('pkt_ok').textContent=d.valid;document.getElementById('pkt_bad').textContent=d.invalid;document.getElementById('cmd21').textContent=d.cmd0021;document.getElementById('cmd23').textContent=d.cmd0023;document.getElementById('cmd31').textContent=d.cmd0031;document.getElementById('cmd33').textContent=d.cmd0033;
for(let i=0;i<6;i++){document.getElementById('raw'+(i+1)).textContent=d.fields[i].raw;document.getElementById('norm'+(i+1)).textContent=i<5?Number(d.fields[i].norm).toFixed(3):'nicht normiert';}
document.getElementById('c23v1').textContent=d.cmd0023_state.available?d.cmd0023_state.v1:'-';document.getElementById('c23v2').textContent=d.cmd0023_state.available?d.cmd0023_state.v2:'-';document.getElementById('c23v3').textContent=d.cmd0023_state.available?d.cmd0023_state.v3:'-';document.getElementById('c31v').textContent=d.cmd0031_state.available?d.cmd0031_state.value:'-';document.getElementById('c31s').textContent=d.cmd0031_state.available?d.cmd0031_state.status:'-';document.getElementById('c33f').textContent=d.cmd0033_state.available?d.cmd0033_state.fields.join(', '):'-';
document.getElementById('raw_hex').textContent=d.raw_hex;document.getElementById('raw_ascii').textContent=d.raw_ascii;document.getElementById('tx_status').textContent=d.tx_status;document.getElementById('tx_target').textContent=Number(d.tx_target).toFixed(2);document.getElementById('tx_raw').textContent=d.tx_raw;document.getElementById('tx_packets').textContent=d.tx_packets;
const a=document.getElementById('tx_neg_btn'),b=document.getElementById('tx_pos_btn');if(a)a.disabled=!d.tx_ready||d.tx_active||d.probe_active;if(b)b.disabled=!d.tx_ready||d.tx_active||d.probe_active;
}catch(e){api.textContent='FEHLER: '+e;}}
setInterval(updateUart,1000);updateUart();
</script>)rawliteral";
  h += "</div></body></html>";
  return h;
}

static String uartConsolePage() {
  String h = pageHead("UART Konsole");
  h += "<h1>UART Konsole</h1>";
  h += "<p class='small'>Universelle Live-Konsole fuer denselben UART-Datenstrom wie der Decoder. RX bleibt sichtbar, auch wenn parallel der Decoder oder Probe-Runner arbeitet. TX muss separat unter UART Einstellungen freigegeben sein.</p>";
  h += "<fieldset><legend>Status</legend><p><b>UART:</b> <span id='con_running'>" + String(uartMonitor.isRunning()?"laeuft":"gestoppt") + "</span> &nbsp; <b>TX-Freigabe:</b> <span id='con_tx_enabled'>" + String(uartMonitor.txEnabled()?"ja":"nein") + "</span></p>";
  h += "<p><b>RX:</b> <span id='con_rx'>" + String((unsigned long)(uartMonitor.totalBytes() & 0xFFFFFFFFULL)) + "</span> Bytes &nbsp; <b>Konsolen-TX:</b> <span id='con_tx'>" + String(uartMonitor.consoleTxBytes()) + "</span> Bytes</p><p><b>TX-Belegung:</b> <span id='con_tx_owner'>" + htmlEscape(uartMonitor.txOwnerText()) + "</span></p>";
  h += "<p class='small'>Aktuell: UART" + String(uartMonitor.uartNumber()) + ", RX GPIO" + String(uartMonitor.rxPin()) + ", TX GPIO" + String(uartMonitor.txPin()) + ", " + String(uartMonitor.baud()) + " " + htmlEscape(uartMonitor.frame()) + ".</p></fieldset>";

  h += "<fieldset><legend>Terminal</legend><label>Anzeige</label><select id='display_mode'><option value='text' selected>Text</option><option value='hex'>HEX</option><option value='hexascii'>HEX + ASCII</option></select>";
  h += "<div class='inlinecheck'><input id='autoscroll' type='checkbox' checked><label for='autoscroll' style='margin:0;font-weight:normal'>Autoscroll bei neuen UART-Daten</label></div>";
  h += "<pre class='terminal' id='terminal'>Verbinde mit UART-Puffer ...</pre>";
  h += "<label>Eingabeformat</label><select id='input_mode'><option value='text' selected>Text / ASCII</option><option value='hex'>HEX-Bytes</option></select>";
  h += "<label>Eingabe</label><input id='console_input' autocomplete='off' autocapitalize='off' spellcheck='false' placeholder='Text oder z. B. 0D 0A FF'>";
  h += "<div class='inlinecheck'><input id='hide_input' type='checkbox'><label for='hide_input' style='margin:0;font-weight:normal'>Texteingabe verdecken (z. B. Passwort)</label></div>";
  h += "<label>Zeilenabschluss</label><select id='line_ending'><option value='cr' selected>CR (0x0D)</option><option value='lf'>LF (0x0A)</option><option value='crlf'>CRLF</option><option value='none'>keiner</option></select>";
  h += "<button id='send_btn' type='button' onclick='sendLine()'>Eingabe senden</button><button id='enter_btn' type='button' onclick='sendEnter()'>Nur Enter senden</button>";
  h += "<button id='ctrlc_btn' type='button' onclick='sendControl(3)'>Ctrl+C</button><button id='ctrld_btn' type='button' onclick='sendControl(4)'>Ctrl+D</button><button id='tab_btn' type='button' onclick='sendControl(9)'>TAB</button><button id='esc_btn' type='button' onclick='sendControl(27)'>ESC</button>";
  h += "<button type='button' onclick='clearConsole()'>Ansicht / UART-Konsolenpuffer leeren</button><p class='small' id='console_api'>warte ...</p></fieldset>";
  h += "<p class='small'><b>Puffer:</b> 8192 RX-Bytes, inkrementelle Abfrage alle 150 ms. Bei sehr hohen kontinuierlichen Baudraten kann HTTP-Polling weiterhin Daten verlieren; der Decoder und Rohpuffer auf dem ESP32 arbeiten davon unabhaengig. BX3-Arbeitswert laut Leitfaden: 115200/8N1.</p>";
  h += uartNav(); h += "<a class='btn' href='/'>Zurueck</a>";
  h += R"rawliteral(<script>
let consoleSeq=0,bytes=[],truncNote=false;const terminal=document.getElementById('terminal'),input=document.getElementById('console_input'),api=document.getElementById('console_api'),autoscroll=document.getElementById('autoscroll');
try{const saved=localStorage.getItem('uartConsoleAutoscroll');if(saved!==null)autoscroll.checked=saved==='1';}catch(e){}
function endingHex(){const v=document.getElementById('line_ending').value;return v==='cr'?'0D':v==='lf'?'0A':v==='crlf'?'0D0A':'';}
function ingestHex(h){for(let i=0;i+1<h.length;i+=2)bytes.push(parseInt(h.substr(i,2),16));if(bytes.length>32768)bytes=bytes.slice(-32768);}
function textView(){let o=truncNote?'[... aeltere UART-Daten wurden im ESP32-Ringpuffer verworfen ...]\n':'';for(const b of bytes){if(b===10)o+='\n';else if(b===13)o+='\r';else if(b===9)o+='\t';else if(b>=32&&b<=126)o+=String.fromCharCode(b);else o+='.';}return o;}
function hexView(withAscii){let o=truncNote?'[... Pufferueberlauf ...]\n':'';for(let i=0;i<bytes.length;i+=16){const row=bytes.slice(i,i+16);const hs=row.map(b=>b.toString(16).toUpperCase().padStart(2,'0')).join(' ');if(withAscii){const as=row.map(b=>b>=32&&b<=126?String.fromCharCode(b):'.').join('');o+=i.toString(16).toUpperCase().padStart(6,'0')+'  '+hs.padEnd(47,' ')+'  |'+as+'|\n';}else o+=hs+'\n';}return o;}
function render(){const m=document.getElementById('display_mode').value;terminal.textContent=m==='text'?textView():hexView(m==='hexascii');if(autoscroll.checked)terminal.scrollTop=terminal.scrollHeight;}
function setTxButtons(ok){for(const id of ['send_btn','enter_btn','ctrlc_btn','ctrld_btn','tab_btn','esc_btn'])document.getElementById(id).disabled=!ok;}
async function pollConsole(){try{const r=await fetch('/uart/console/data?since='+consoleSeq,{cache:'no-store'});const d=await r.json();if(consoleSeq===0&&terminal.textContent.startsWith('Verbinde'))terminal.textContent='';if(d.truncated){bytes=[];truncNote=true;}ingestHex(d.hex||'');consoleSeq=Number(d.sequence)||0;document.getElementById('con_running').textContent=d.running?'laeuft':'gestoppt';document.getElementById('con_tx_enabled').textContent=d.tx_enabled?'ja':'nein';document.getElementById('con_rx').textContent=d.rx_bytes;document.getElementById('con_tx').textContent=d.tx_bytes;document.getElementById('con_tx_owner').textContent=d.tx_owner;setTxButtons(!!d.tx_ready);api.textContent=d.tx_ready?'Live - TX bereit':'Live - TX nicht bereit: '+d.tx_owner;render();}catch(e){api.textContent='FEHLER: '+e;}}
async function sendForm(data,fmt){try{const r=await fetch('/uart/console/send',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded;charset=UTF-8'},body:'format='+encodeURIComponent(fmt)+'&data='+encodeURIComponent(data)});if(!r.ok){api.textContent='SENDEN FEHLER: '+await r.text();return false;}api.textContent='gesendet';return true;}catch(e){api.textContent='SENDEN FEHLER: '+e;return false;}}
async function sendLine(){const m=document.getElementById('input_mode').value;if(m==='hex'){const d=input.value+(input.value&&endingHex()?' ':'')+endingHex();if(await sendForm(d,'hex'))input.value='';}else{const e=endingHex(),suffix=e==='0D'?'\r':e==='0A'?'\n':e==='0D0A'?'\r\n':'';if(await sendForm(input.value+suffix,'text'))input.value='';}input.focus();}
async function sendEnter(){const e=endingHex()||'0D';await sendForm(e,'hex');input.focus();}
async function sendControl(code){try{const r=await fetch('/uart/console/control',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded;charset=UTF-8'},body:'code='+code});if(!r.ok)api.textContent='CONTROL FEHLER: '+await r.text();else api.textContent='Steuerzeichen gesendet';}catch(e){api.textContent='CONTROL FEHLER: '+e;}input.focus();}
async function clearConsole(){try{const r=await fetch('/uart/console/clear',{method:'POST'});const d=await r.json();bytes=[];truncNote=false;consoleSeq=Number(d.sequence)||0;render();api.textContent='Puffer geleert';}catch(e){api.textContent='LOESCHEN FEHLER: '+e;}}
document.getElementById('display_mode').addEventListener('change',render);autoscroll.addEventListener('change',()=>{try{localStorage.setItem('uartConsoleAutoscroll',autoscroll.checked?'1':'0');}catch(e){}if(autoscroll.checked)terminal.scrollTop=terminal.scrollHeight;});document.getElementById('input_mode').addEventListener('change',e=>{const hex=e.target.value==='hex';document.getElementById('hide_input').disabled=hex;if(hex){input.type='text';input.placeholder='z. B. 48 65 6C 6C 6F 0D';}else{input.placeholder='Text / Benutzername / Shell-Befehl';}});document.getElementById('hide_input').addEventListener('change',e=>{if(document.getElementById('input_mode').value==='text')input.type=e.target.checked?'password':'text';});input.addEventListener('keydown',e=>{if(e.key==='Enter'){e.preventDefault();sendLine();}});setInterval(pollConsole,150);pollConsole();input.focus();
</script>)rawliteral";
  h += "</div></body></html>";
  return h;
}

static String uartProbeRunnerPage() {
  String h = pageHead("UART Probe-Runner");
  h += "<h1>UART Probe-Runner</h1>";
  h += "<p class='small'>Eigener aktiver Testbereich fuer den Katalog mit 1000 Kandidaten. Der gemeinsame RX-Datenstrom wird waehrenddessen weiter an Konsole und Decoder verteilt. Der Probe-Runner reserviert TX, damit keine manuellen Konsoleneingaben dazwischen gesendet werden.</p>";
  h += "<fieldset><legend>Voraussetzungen</legend><p><b>UART:</b> <span id='pr_uart'>" + htmlEscape(uartMonitor.status()) + "</span></p><p><b>TX freigegeben:</b> <span id='pr_tx'>" + String(uartMonitor.txEnabled()?"ja":"nein") + "</span> &nbsp; <b>TX-Belegung:</b> <span id='pr_owner'>" + htmlEscape(uartMonitor.txOwnerText()) + "</span></p><p class='small'>Start ist nur moeglich, wenn UART laeuft, TX freigegeben und ein gueltiger TX-GPIO konfiguriert ist.</p></fieldset>";
  h += "<fieldset><legend>Automatischer Test</legend><p>Baseline: 2 s. Jeder Kandidat: 5 s. Senden: alle 250 ms. Reihenfolge: P0 -&gt; P1 -&gt; P2 -&gt; P3.</p>";
  h += "<p><b>Status:</b> <span id='probe_status'>" + htmlEscape(uartMonitor.probeStatus()) + "</span></p><p><b>Fortschritt:</b> <span id='probe_progress'>" + String(uartMonitor.probeCandidatesCompleted()) + "/1000</span></p><p><b>Kandidat:</b> <span id='probe_candidate'>" + htmlEscape(uartMonitor.probeCandidateId()) + "</span></p><p><b>TX im Kandidat:</b> <span id='probe_sent'>" + String(uartMonitor.probeCandidateSentCount()) + "</span> &nbsp; <b>Reaktion:</b> <span id='probe_reaction'>" + htmlEscape(uartMonitor.probeReactionText()) + "</span></p>";
  h += "<form method='post' action='/uart/probe/start_hit'><button id='probe_start_hit' type='submit'>Sweep starten - bei Treffer stoppen</button></form><form method='post' action='/uart/probe/start_all'><button id='probe_start_all' type='submit'>Sweep starten - alle 1000 testen</button></form><form method='post' action='/uart/probe/stop'><button id='probe_stop' type='submit'>Probe-Runner STOP</button></form></fieldset>";
  h += "<fieldset><legend>Live-Auswertung</legend><p><b>Gueltige Decoder-Frames:</b> <span id='pr_valid'>0</span> &nbsp; <b>Unbekannte Commands:</b> <span id='pr_unknown'>0</span></p><p><b>Letzter Command:</b> <span id='pr_cmd'>-</span></p></fieldset>";
  h += "<div class='warn'><b>Sicherheit:</b> Der Probe-Runner sendet aktiv unbekannte, formal gueltige Kandidaten. Reale Funktionen, Aktoren oder Zustandsaenderungen koennen ausgeloest werden. Nur an autorisierten, elektrisch geprueften Testaufbauten verwenden.</div>";
  h += uartNav(); h += "<a class='btn' href='/'>Zurueck</a>";
  h += R"rawliteral(<script>
function hx(v,w){return '0x'+Number(v).toString(16).toUpperCase().padStart(w,'0');}
async function updateProbe(){try{const r=await fetch('/uart/status',{cache:'no-store'});const d=await r.json();document.getElementById('pr_uart').textContent=d.status;document.getElementById('pr_tx').textContent=d.tx_enabled?'ja':'nein';document.getElementById('pr_owner').textContent=d.tx_owner;document.getElementById('probe_status').textContent=d.probe_status;document.getElementById('probe_progress').textContent=d.probe_completed+'/1000';document.getElementById('probe_candidate').textContent=(d.probe_id||'-')+(d.probe_catalog?' (#'+d.probe_catalog+', P'+d.probe_priority+')':'');document.getElementById('probe_sent').textContent=d.probe_sent;document.getElementById('probe_reaction').textContent=d.probe_reaction_text;document.getElementById('pr_valid').textContent=d.valid;document.getElementById('pr_unknown').textContent=d.unknown_cmd;document.getElementById('pr_cmd').textContent=hx(d.last_command,4);const ready=d.running&&d.tx_enabled&&!d.probe_active&&!d.tx_active&&d.tx_owner==='frei';document.getElementById('probe_start_hit').disabled=!ready;document.getElementById('probe_start_all').disabled=!ready;document.getElementById('probe_stop').disabled=!d.probe_active;}catch(e){document.getElementById('probe_status').textContent='FEHLER: '+e;}}
setInterval(updateProbe,500);updateProbe();
</script>)rawliteral";
  h += "</div></body></html>";
  return h;
}

static String uartSettingsPage() {
  String h = pageHead("UART Einstellungen");
  h += "<h1>UART Einstellungen</h1>";
  h += "<p class='small'>Diese Seite konfiguriert nur den gemeinsamen Hardware-UART. Konsole, Decoder und Probe-Runner sind getrennte Funktionen auf demselben Datenstrom und werden nicht mehr als exklusive Modi umgeschaltet.</p>";
  h += "<form method='post' action='/save_uart'><fieldset><legend>Basisbetrieb / Schnittstelle</legend>";
  h += "<div class='inlinecheck'><input type='checkbox' name='uart_enable' value='1'" + String(uartMonitor.enabled()?" checked":"") + "><label style='margin:0'>UART nach Speichern und beim Boot aktiv</label></div>";
  h += "<label>Hardware-UART</label><select name='uartno'><option value='1'" + String(uartMonitor.uartNumber()==1?" selected":"") + ">UART1</option><option value='2'" + String(uartMonitor.uartNumber()==2?" selected":"") + ">UART2</option></select>";
  h += "<label>RX GPIO</label><input type='number' min='0' max='39' name='rxpin' value='" + String(uartMonitor.rxPin()) + "'>";
  h += "<label>TX GPIO (-1 = nicht verwenden)</label><input type='number' min='-1' max='33' name='txpin' value='" + String(uartMonitor.txPin()) + "'>";
  h += "<div class='inlinecheck'><input type='checkbox' name='tx_enable' value='1'" + String(uartMonitor.txEnabled()?" checked":"") + "><label style='margin:0'>TX-Ausgang bewusst freigeben</label></div>";
  h += "<p class='warn'><b>TX-Sicherheit:</b> Wenn TX nicht freigegeben ist, startet HardwareSerial mit TX=-1. Der konfigurierte TX-GPIO wird dann nicht durch den UART-Peripherieblock getrieben. Beim Upgrade von v0.14 oder aelter wird TX einmalig sicherheitshalber gesperrt, bis diese Option aktiv gesetzt wird.</p>";
  h += "<label>Baudrate</label><input type='number' min='300' max='2000000' name='baud' value='" + String(uartMonitor.baud()) + "'>";
  h += "<label>Format</label><select name='frame'>";
  const char* frames[] = {"8N1","8E1","8O1","8N2"};
  for (const char* f : frames) h += "<option value='" + String(f) + "'" + String(uartMonitor.frame()==f?" selected":"") + ">" + String(f) + "</option>";
  h += "</select><p class='small'>UART0 bleibt fuer Debug reserviert. GPIO6..11 sind Flash-Pins; GPIO1/3 bleiben fuer UART0 frei; GPIO34..39 sind nur Eingang. BX3-Arbeitswert aus dem Leitfaden: 115200/8N1. TTL-UART-Pegel vor Anschluss pruefen; VCC nicht verbinden.</p></fieldset><button type='submit'>UART-Einstellungen speichern / UART neu starten</button></form>";
  h += "<fieldset><legend>Laufzeit</legend><p><b>Status:</b> " + htmlEscape(uartMonitor.status()) + "</p><p><b>TX-Belegung:</b> " + htmlEscape(uartMonitor.txOwnerText()) + "</p><form method='post' action='/uart/start?return=settings'><button type='submit'" + String((uartMonitor.isRunning()||!uartMonitor.enabled())?" disabled":"") + ">UART START</button></form><form method='post' action='/uart/stop?return=settings'><button type='submit'" + String(!uartMonitor.isRunning()?" disabled":"") + ">UART STOP</button></form></fieldset>";
  h += uartNav(); h += "<a class='btn' href='/'>Zurueck</a></div></body></html>";
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
  // IMPORTANT: ESPAsyncWebServer 3.x may match a shorter path before a more specific
  // sub-route. Register all /uart/* routes before the /uart compatibility redirect.
  server.on("/uart/status", HTTP_GET, [](AsyncWebServerRequest* r){
    AsyncWebServerResponse* resp = r->beginResponse(200, "application/json; charset=utf-8", uartMonitor.statusJson());
    resp->addHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    r->send(resp);
  });
  server.on("/uart/settings", HTTP_GET, [](AsyncWebServerRequest* r){
    AsyncWebServerResponse* resp = r->beginResponse(200, "text/html; charset=utf-8", uartSettingsPage());
    resp->addHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    r->send(resp);
  });
  server.on("/uart/decoder", HTTP_GET, [](AsyncWebServerRequest* r){
    AsyncWebServerResponse* resp = r->beginResponse(200, "text/html; charset=utf-8", uartDecoderPage());
    resp->addHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    r->send(resp);
  });
  // Compatibility with v0.14 and older bookmarks.
  server.on("/uart/monitor", HTTP_GET, [](AsyncWebServerRequest* r){ r->redirect("/uart/decoder"); });

  // Register specific console API routes before /uart/console.
  server.on("/uart/console/data", HTTP_GET, [](AsyncWebServerRequest* r){
    uint32_t since = 0;
    if (r->hasArg("since")) since = (uint32_t)strtoul(r->arg("since").c_str(), nullptr, 10);
    AsyncWebServerResponse* resp = r->beginResponse(200, "application/json; charset=utf-8", uartMonitor.consoleChunkJson(since));
    resp->addHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    r->send(resp);
  });
  server.on("/uart/console/send", HTTP_POST, [](AsyncWebServerRequest* r){
    if (!uartMonitor.consoleTxReady()) {
      r->send(409, "text/plain; charset=utf-8", "UART TX ist nicht bereit. TX-Freigabe, TX-GPIO, UART-Status und Probe-Runner pruefen.");
      return;
    }
    if (!r->hasArg("data")) {
      r->send(400, "text/plain; charset=utf-8", "Parameter data fehlt.");
      return;
    }
    const String format = r->hasArg("format") ? r->arg("format") : String("text");
    const String data = r->arg("data");
    size_t written = 0;
    size_t expected = 0;
    if (format == "hex") {
      if (data.length() > 2048U) {
        r->send(413, "text/plain; charset=utf-8", "HEX-Eingabe ist zu gross.");
        return;
      }
      uint8_t bytes[512];
      if (!parseHexBytes(data, bytes, sizeof(bytes), expected)) {
        r->send(400, "text/plain; charset=utf-8", "Ungueltige HEX-Eingabe. Erlaubt sind Hex-Ziffern und Trennzeichen Leerzeichen, Doppelpunkt, Komma oder Bindestrich.");
        return;
      }
      if (!expected) {
        r->send(400, "text/plain; charset=utf-8", "Keine HEX-Bytes angegeben.");
        return;
      }
      written = uartMonitor.consoleWrite(bytes, expected);
    } else {
      if (data.length() > 512U) {
        r->send(413, "text/plain; charset=utf-8", "Texteingabe ist groesser als 512 Bytes.");
        return;
      }
      expected = data.length();
      if (!expected) {
        r->send(400, "text/plain; charset=utf-8", "Keine Daten angegeben.");
        return;
      }
      written = uartMonitor.consoleWrite((const uint8_t*)data.c_str(), expected);
    }
    if (written != expected) {
      r->send(500, "text/plain; charset=utf-8", "UART konnte nicht alle Bytes senden.");
      return;
    }
    r->send(200, "text/plain; charset=utf-8", "OK");
  });
  server.on("/uart/console/control", HTTP_POST, [](AsyncWebServerRequest* r){
    if (!uartMonitor.consoleTxReady()) {
      r->send(409, "text/plain; charset=utf-8", "UART TX ist nicht bereit.");
      return;
    }
    if (!r->hasArg("code")) {
      r->send(400, "text/plain; charset=utf-8", "Parameter code fehlt.");
      return;
    }
    const int code = r->arg("code").toInt();
    if (code != 3 && code != 4 && code != 9 && code != 27) {
      r->send(400, "text/plain; charset=utf-8", "Nicht erlaubtes Steuerzeichen.");
      return;
    }
    const uint8_t value = (uint8_t)code;
    if (uartMonitor.consoleWrite(&value, 1U) != 1U) {
      r->send(500, "text/plain; charset=utf-8", "Steuerzeichen konnte nicht gesendet werden.");
      return;
    }
    r->send(200, "text/plain; charset=utf-8", "OK");
  });
  server.on("/uart/console/clear", HTTP_POST, [](AsyncWebServerRequest* r){
    uartMonitor.clearConsole();
    AsyncWebServerResponse* resp = r->beginResponse(200, "application/json; charset=utf-8", String("{\"sequence\":") + String(uartMonitor.consoleSequence()) + "}");
    resp->addHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    r->send(resp);
  });
  server.on("/uart/console", HTTP_GET, [](AsyncWebServerRequest* r){
    AsyncWebServerResponse* resp = r->beginResponse(200, "text/html; charset=utf-8", uartConsolePage());
    resp->addHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    r->send(resp);
  });

  // Probe actions must be registered before the /uart/probe page itself.
  server.on("/uart/probe/start_hit", HTTP_POST, [](AsyncWebServerRequest* r){
    if (!uartMonitor.startProbeSweep(true)) {
      r->send(400, "text/html; charset=utf-8", pageHead("Probe-Runner nicht bereit") + "<h1>Probe-Runner nicht gestartet</h1><p>" + htmlEscape(uartMonitor.probeStatus()) + "</p><a class='btn' href='/uart/probe'>Zurueck zum Probe-Runner</a></div></body></html>");
      return;
    }
    r->redirect("/uart/probe");
  });
  server.on("/uart/probe/start_all", HTTP_POST, [](AsyncWebServerRequest* r){
    if (!uartMonitor.startProbeSweep(false)) {
      r->send(400, "text/html; charset=utf-8", pageHead("Probe-Runner nicht bereit") + "<h1>Probe-Runner nicht gestartet</h1><p>" + htmlEscape(uartMonitor.probeStatus()) + "</p><a class='btn' href='/uart/probe'>Zurueck zum Probe-Runner</a></div></body></html>");
      return;
    }
    r->redirect("/uart/probe");
  });
  server.on("/uart/probe/stop", HTTP_POST, [](AsyncWebServerRequest* r){ uartMonitor.stopProbeSweep(); r->redirect("/uart/probe"); });
  server.on("/uart/probe", HTTP_GET, [](AsyncWebServerRequest* r){
    AsyncWebServerResponse* resp = r->beginResponse(200, "text/html; charset=utf-8", uartProbeRunnerPage());
    resp->addHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    r->send(resp);
  });

  server.on("/uart/start", HTTP_POST, [](AsyncWebServerRequest* r){
    const String ret = r->hasArg("return") ? r->arg("return") : String("settings");
    const char* target = ret == "console" ? "/uart/console" : ret == "decoder" ? "/uart/decoder" : ret == "probe" ? "/uart/probe" : "/uart/settings";
    if (!uartMonitor.start()) {
      r->send(400, "text/html; charset=utf-8", pageHead("UART Startfehler") + "<h1>UART konnte nicht gestartet werden</h1><p>" + htmlEscape(uartMonitor.status()) + "</p><a class='btn' href='/uart/settings'>UART Einstellungen</a></div></body></html>");
      return;
    }
    r->redirect(target);
  });
  server.on("/uart/stop", HTTP_POST, [](AsyncWebServerRequest* r){
    const String ret = r->hasArg("return") ? r->arg("return") : String("settings");
    const char* target = ret == "console" ? "/uart/console" : ret == "decoder" ? "/uart/decoder" : ret == "probe" ? "/uart/probe" : "/uart/settings";
    uartMonitor.stop();
    r->redirect(target);
  });

  server.on("/uart/send_f5_neg", HTTP_POST, [](AsyncWebServerRequest* r){
    if (!uartMonitor.sendField5ForOneSecond(-0.5f)) {
      r->send(400, "text/html; charset=utf-8", pageHead("UART TX nicht bereit") + "<h1>Senden nicht gestartet</h1><p>" + htmlEscape(uartMonitor.txStatus()) + "</p><a class='btn' href='/uart/decoder'>Zurueck zum Decoder</a></div></body></html>");
      return;
    }
    r->redirect("/uart/decoder");
  });
  server.on("/uart/send_f5_pos", HTTP_POST, [](AsyncWebServerRequest* r){
    if (!uartMonitor.sendField5ForOneSecond(+0.5f)) {
      r->send(400, "text/html; charset=utf-8", pageHead("UART TX nicht bereit") + "<h1>Senden nicht gestartet</h1><p>" + htmlEscape(uartMonitor.txStatus()) + "</p><a class='btn' href='/uart/decoder'>Zurueck zum Decoder</a></div></body></html>");
      return;
    }
    r->redirect("/uart/decoder");
  });

  server.on("/uart_clear", HTTP_POST, [](AsyncWebServerRequest* r){ uartMonitor.clearRaw(); r->redirect("/uart/decoder"); });
  // Backward-compatible UART entry point. Keep it after the specific /uart/* routes.
  server.on("/uart", HTTP_GET, [](AsyncWebServerRequest* r){ r->redirect("/uart/decoder"); });

  server.on("/save_uart", HTTP_POST, [](AsyncWebServerRequest* r){
    uartMonitor.setEnabled(r->hasArg("uart_enable"));
    uartMonitor.setTxEnabled(r->hasArg("tx_enable"));
    if (r->hasArg("uartno")) uartMonitor.setUartNumber((uint8_t)r->arg("uartno").toInt());
    if (r->hasArg("rxpin")) uartMonitor.setRxPin(r->arg("rxpin").toInt());
    if (r->hasArg("txpin")) uartMonitor.setTxPin(r->arg("txpin").toInt());
    if (r->hasArg("baud")) uartMonitor.setBaud((uint32_t)r->arg("baud").toInt());
    if (r->hasArg("frame")) uartMonitor.setFrame(r->arg("frame"));
    uartMonitor.save();
    const bool ok = uartMonitor.restart();
    if (!ok) r->send(400,"text/html; charset=utf-8",pageHead("UART Fehler")+"<h1>UART-Konfiguration ungueltig</h1><p>"+htmlEscape(uartMonitor.status())+"</p><a class='btn' href='/uart/settings'>Zurueck zu den Einstellungen</a></div></body></html>");
    else r->redirect("/uart/settings");
  });

  server.on("/save_uart_decoder", HTTP_POST, [](AsyncWebServerRequest* r){
    if (r->hasArg("deadband")) uartMonitor.setDeadband(r->arg("deadband").toFloat());
    for (uint8_t i=0;i<6;++i) { String k="label"+String(i+1); if(r->hasArg(k)) uartMonitor.setFieldAlias(i,r->arg(k)); }
    for (uint8_t i=0;i<5;++i) {
      String km="min"+String(i+1), kc="ctr"+String(i+1), kx="max"+String(i+1);
      if (r->hasArg(km) && r->hasArg(kc) && r->hasArg(kx)) {
        long mn=r->arg(km).toInt(), ct=r->arg(kc).toInt(), mx=r->arg(kx).toInt();
        if (mn>=0 && ct>=0 && mx>=0 && mn<ct && ct<mx && mx<=65535) uartMonitor.setCalibration(i,(uint16_t)mn,(uint16_t)ct,(uint16_t)mx);
      }
    }
    uartMonitor.save();
    r->redirect("/uart/decoder");
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
  uartMonitor.begin();
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
  uartMonitor.loop();
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
