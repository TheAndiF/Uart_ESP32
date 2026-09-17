#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <time.h>

#include "MqttManager.h"
#include "DeepSleepManager.h"
#include "OtaManager.h"
#include "BatteryMonitor.h"

static const char* FW_NAME = "Uart_Esp32";
static const char* FW_VERSION_DEFAULT = "1.0.0";
static const char* TZ_CET_CEST = "CET-1CEST,M3.5.0,M10.5.0/3";

AsyncWebServer server(80);
WiFiClient wifiClient;
MqttManager mqtt;
DeepSleepManager deepSleep;
OtaManager ota;
BatteryMonitor battery;
Preferences otaPrefs;

String wifiSsid, wifiPassword, staticIp, gatewayIp, subnetMask;
String apSsid, apPassword, hostname, ntpServer;
bool mqttEnabled = false;
String mqttHost, mqttUser, mqttPassword, mqttTopicBase, mqttClientId;
uint16_t mqttPort = 1883;
uint32_t heartbeatMs = 60000UL;

bool apActive = false;
bool otaStarted = false;
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

static void loadSettings() {
  Preferences p;
  p.begin("netconf", true);
  wifiSsid = p.getString("ssid", "");
  wifiPassword = p.getString("wifipw", "");
  staticIp = p.getString("static_ip", "");
  gatewayIp = p.getString("gateway", "192.168.1.1");
  subnetMask = p.getString("subnet", "255.255.255.0");
  apSsid = p.getString("ap_ssid", "Uart_Esp32-Setup");
  apPassword = p.getString("ap_pw", "");
  hostname = p.getString("hostname", "uart-esp32");
  ntpServer = p.getString("ntp_server", "pool.ntp.org");

  mqttEnabled = p.getBool("mqtt_enabled", false);
  mqttHost = p.getString("mqtt_host", "");
  mqttPort = (uint16_t)p.getUInt("mqtt_port", 1883);
  mqttUser = p.getString("mqtt_user", "");
  mqttPassword = p.getString("mqtt_pw", "");
  mqttTopicBase = p.getString("mqtt_topic", "");
  mqttClientId = p.getString("mqtt_client_id", "");
  heartbeatMs = p.getULong("mqtt_hb_ms", 60000UL);
  p.end();

  if (hostname.length() == 0) hostname = "uart-esp32";
  if (apSsid.length() == 0) apSsid = "Uart_Esp32-Setup";
  if (mqttTopicBase.length() == 0) mqttTopicBase = defaultIdentity();
  if (mqttClientId.length() == 0) mqttClientId = mqttTopicBase;
  if (mqttPort == 0) mqttPort = 1883;
  if (heartbeatMs < 1000) heartbeatMs = 1000;
}

static void saveNetworkSettings() {
  Preferences p; p.begin("netconf", false);
  p.putString("ssid", wifiSsid); p.putString("wifipw", wifiPassword);
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

static void startFallbackAp() {
  if (apActive) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  if (apPassword.length() >= 8) WiFi.softAP(apSsid.c_str(), apPassword.c_str());
  else WiFi.softAP(apSsid.c_str());
  apActive = true;
  Serial.printf("[WIFI] Fallback AP: %s, IP %s\n", apSsid.c_str(), WiFi.softAPIP().toString().c_str());
}

static void connectWifi(bool waitForConnection) {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(hostname.c_str());

  IPAddress ip, gw, mask;
  if (validIp(staticIp, ip) && validIp(gatewayIp, gw) && validIp(subnetMask, mask)) {
    WiFi.config(ip, gw, mask);
  }

  if (wifiSsid.length() == 0) { startFallbackAp(); return; }
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  lastWifiTry = millis();

  if (waitForConnection) {
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000UL) { delay(250); Serial.print('.'); }
    Serial.println();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WIFI] Verbunden: %s\n", WiFi.localIP().toString().c_str());
  } else startFallbackAp();
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
  h += "<h1>" + String(FW_NAME) + "</h1>";
  h += "<fieldset><legend>System</legend><p><b>Chip:</b> ESP32-WROOM-32 / NodeMCU-32S</p>";
  h += "<p><b>Zeit:</b> " + nowText() + "</p><p><b>Uptime:</b> " + String(millis()/1000UL) + " s</p>";
  h += "<p><b>ESP32 Temperatur:</b> " + String(espTemperature(),2) + " &deg;C</p></fieldset>";
  h += "<fieldset><legend>Netzwerk</legend><p><b>WLAN:</b> " + String(WiFi.status()==WL_CONNECTED ? "verbunden" : "nicht verbunden") + "</p>";
  if (WiFi.status()==WL_CONNECTED) h += "<p><b>IP:</b> " + WiFi.localIP().toString() + " &nbsp; <b>RSSI:</b> " + String(WiFi.RSSI()) + " dBm</p>";
  if (apActive) h += "<p><b>Fallback-AP:</b> " + htmlEscape(apSsid) + " / " + WiFi.softAPIP().toString() + "</p>";
  h += "</fieldset>";
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
  String h = pageHead("WLAN / NTP"); h += "<h1>WLAN / NTP</h1><form method='post' action='/save_network'><fieldset><legend>WLAN Client</legend>";
  h += "<label>SSID</label><input name='ssid' value='"+htmlEscape(wifiSsid)+"'><label>Passwort</label><input type='password' name='wifipw' placeholder='leer = unveraendert'>";
  h += "<label>Statische IP (leer = DHCP)</label><input name='static_ip' value='"+htmlEscape(staticIp)+"'><label>Gateway</label><input name='gateway' value='"+htmlEscape(gatewayIp)+"'><label>Subnetz</label><input name='subnet' value='"+htmlEscape(subnetMask)+"'></fieldset>";
  h += "<fieldset><legend>Fallback Access Point</legend><label>AP SSID</label><input name='ap_ssid' value='"+htmlEscape(apSsid)+"'><label>AP Passwort (leer = offen, sonst min. 8 Zeichen)</label><input type='password' name='ap_pw' placeholder='leer = unveraendert'></fieldset>";
  h += "<fieldset><legend>System</legend><label>Hostname</label><input name='hostname' value='"+htmlEscape(hostname)+"'><label>NTP Server</label><input name='ntp_server' value='"+htmlEscape(ntpServer)+"'><p class='small'>Zeitzone: Deutschland (CET/CEST), Sommer-/Winterzeit automatisch.</p></fieldset><button type='submit'>Speichern und neu starten</button></form><a class='btn' href='/'>Zurueck</a></div></body></html>";
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
  server.on("/save_network", HTTP_POST, [](AsyncWebServerRequest* r){
    if (r->hasArg("ssid")) wifiSsid=r->arg("ssid");
    if (r->hasArg("wifipw") && r->arg("wifipw").length()) wifiPassword=r->arg("wifipw");
    if (r->hasArg("static_ip")) staticIp=r->arg("static_ip"); if (r->hasArg("gateway")) gatewayIp=r->arg("gateway"); if (r->hasArg("subnet")) subnetMask=r->arg("subnet");
    if (r->hasArg("ap_ssid")) apSsid=r->arg("ap_ssid"); if (r->hasArg("ap_pw") && r->arg("ap_pw").length()) apPassword=r->arg("ap_pw");
    if (r->hasArg("hostname")) hostname=r->arg("hostname"); if (r->hasArg("ntp_server")) ntpServer=r->arg("ntp_server");
    saveNetworkSettings(); r->send(200,"text/html; charset=utf-8",pageHead("Gespeichert")+"<h1>Gespeichert</h1><p>Neustart ...</p></div></body></html>"); delay(500); ESP.restart();
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
  Serial.printf("\n%s\n", FW_NAME);
  loadSettings();
  battery.load(); battery.begin(); battery.measure("boot");
  deepSleep.begin(); deepSleep.setBeforeSleepCallback(beforeSleep);
  ota.begin(otaPrefs);

  connectWifi(true);
  configureTime();

  mqtt.begin(wifiClient); mqtt.setCallback(mqttCallback);
  mqtt.configure(mqttHost,mqttPort,mqttUser,mqttPassword,mqttTopicBase,mqttClientId,mqttEnabled);

  registerRoutes(); server.begin();

  if (WiFi.status()==WL_CONNECTED) { ota.startArduinoOta(hostname); otaStarted=true; }
  if (mqttEnabled) { mqtt.tryConnectOnce(); if(mqtt.isConnected()) publishHeartbeat(); }
}

void loop() {
  ota.loop(); mqtt.loop();

  if (WiFi.status() != WL_CONNECTED && millis()-lastWifiTry >= 30000UL) {
    lastWifiTry=millis();
    if (wifiSsid.length() != 0) { WiFi.begin(wifiSsid.c_str(),wifiPassword.c_str()); startFallbackAp(); }
  }
  if (WiFi.status()==WL_CONNECTED && !otaStarted) { configureTime(); ota.startArduinoOta(hostname); otaStarted=true; }

  if (mqttEnabled && millis()-lastMqttTry >= 5000UL) {
    lastMqttTry=millis(); bool was=mqtt.isConnected(); mqtt.connectIfNeeded(); if(!was && mqtt.isConnected()) publishHeartbeat();
  }
  if (millis()-lastBattery >= 60000UL) { lastBattery=millis(); battery.measure("periodic"); }
  if (mqttEnabled && (millis()-lastHeartbeat >= heartbeatMs)) publishHeartbeat();

  deepSleep.loop(mqtt.isConnected(), battery.valid ? battery.voltage : NAN);
  delay(2);
}
