#include "OtaManager.h"

#include <HTTPClient.h>
#include <HTTPUpdate.h>

OtaManager::OtaManager()
: _prefs(nullptr),
  _arduinoOtaEnabled(false),
  _arduinoOtaPassword("ota123"),
  _arduinoOtaStarted(false),
  _pullUpdateEnabled(false),
  _pullAutoCheckEnabled(false),
  _pullUpdateRunning(false),
  _versionInfoUrl(""),
  _currentVersion("1.0.0")
{
}

void OtaManager::begin(Preferences& prefs)
{
  _prefs = &prefs;
  loadSettings();
}

void OtaManager::loadSettings()
{
  if (!_prefs) return;

  _prefs->begin("netconf", true);
  _arduinoOtaEnabled   = _prefs->getBool("ota_enabled", false);
  _arduinoOtaPassword  = _prefs->getString("ota_pw", "ota123");
  _pullUpdateEnabled   = _prefs->getBool("pull_enabled", false);
  _pullAutoCheckEnabled = _prefs->getBool("pull_auto", false);
  _versionInfoUrl      = _prefs->getString("pull_url", "");
  _currentVersion      = _prefs->getString("fw_version", "1.0.0");
  _prefs->end();
}

void OtaManager::saveSettings()
{
  if (!_prefs) return;

  _prefs->begin("netconf", false);
  _prefs->putBool("ota_enabled", _arduinoOtaEnabled);
  _prefs->putString("ota_pw", _arduinoOtaPassword);
  _prefs->putBool("pull_enabled", _pullUpdateEnabled);
  _prefs->putBool("pull_auto", _pullAutoCheckEnabled);
  _prefs->putString("pull_url", _versionInfoUrl);
  _prefs->putString("fw_version", _currentVersion);
  _prefs->end();
}

bool OtaManager::isArduinoOtaEnabled() const
{
  return _arduinoOtaEnabled;
}

String OtaManager::getArduinoOtaPassword() const
{
  return _arduinoOtaPassword;
}

bool OtaManager::isPullUpdateEnabled() const
{
  return _pullUpdateEnabled;
}

bool OtaManager::isPullAutoCheckEnabled() const
{
  return _pullAutoCheckEnabled;
}

String OtaManager::getVersionInfoUrl() const
{
  return _versionInfoUrl;
}

String OtaManager::getCurrentVersion() const
{
  return _currentVersion;
}

void OtaManager::startArduinoOta(const String& hostname)
{
  if (!_arduinoOtaEnabled) {
    Serial.println("[OTA] ArduinoOTA deaktiviert");
    _arduinoOtaStarted = false;
  } else if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[OTA] ArduinoOTA nicht gestartet: kein WLAN");
    _arduinoOtaStarted = false;
  } else {
    const char* otaHost = hostname.length() > 0 ? hostname.c_str() : "uart-esp32";

    ArduinoOTA.setHostname(otaHost);
    if (_arduinoOtaPassword.length() > 0) {
      ArduinoOTA.setPassword(_arduinoOtaPassword.c_str());
    }

    ArduinoOTA.onStart([]() {
      Serial.println("[OTA] Start");
    });

    ArduinoOTA.onEnd([]() {
      Serial.println("\n[OTA] Ende");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("[OTA] Fortschritt: %u%%\r", (progress * 100U) / total);
    });

    ArduinoOTA.onError([](ota_error_t error) {
      Serial.printf("[OTA] Fehler[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth fehlgeschlagen");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin fehlgeschlagen");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect fehlgeschlagen");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive fehlgeschlagen");
      else if (error == OTA_END_ERROR) Serial.println("End fehlgeschlagen");
      else Serial.println("Unbekannter Fehler");
    });

    ArduinoOTA.begin();
    _arduinoOtaStarted = true;

    Serial.print("[OTA] ArduinoOTA bereit auf Hostname: ");
    Serial.println(otaHost);
  }

  if (_pullUpdateEnabled && _pullAutoCheckEnabled && WiFi.status() == WL_CONNECTED) {
    checkServerUpdateNow();
  }
}

void OtaManager::loop()
{
  if (_arduinoOtaEnabled && _arduinoOtaStarted) {
    ArduinoOTA.handle();
  }
}

bool OtaManager::isNewerVersion(const String& remote, const String& local) const
{
  // Numerischer Vergleich von bis zu vier Versionssegmenten (z. B. 1.2.10 > 1.2.9).
  // Nicht-numerische Suffixe werden nach dem jeweiligen Zahlenteil ignoriert.
  int rpos = 0, lpos = 0;
  for (int part = 0; part < 4; ++part) {
    long rv = 0, lv = 0;
    while (rpos < remote.length() && !isDigit(remote[rpos])) rpos++;
    while (lpos < local.length() && !isDigit(local[lpos])) lpos++;
    while (rpos < remote.length() && isDigit(remote[rpos])) { rv = rv * 10 + (remote[rpos++] - '0'); }
    while (lpos < local.length() && isDigit(local[lpos])) { lv = lv * 10 + (local[lpos++] - '0'); }
    if (rv > lv) return true;
    if (rv < lv) return false;
  }
  return false;
}

String OtaManager::extractJsonValue(const String& json, const String& key) const
{
  String pattern = "\"" + key + "\"";
  int keyPos = json.indexOf(pattern);
  if (keyPos < 0) return "";

  int colonPos = json.indexOf(':', keyPos + pattern.length());
  if (colonPos < 0) return "";

  int firstQuote = json.indexOf('"', colonPos + 1);
  if (firstQuote < 0) return "";

  int secondQuote = json.indexOf('"', firstQuote + 1);
  if (secondQuote < 0) return "";

  return json.substring(firstQuote + 1, secondQuote);
}

void OtaManager::checkServerUpdateNow()
{
  if (_pullUpdateRunning) {
    Serial.println("[PULL-OTA] Bereits aktiv");
    return;
  }
  if (!_pullUpdateEnabled) {
    Serial.println("[PULL-OTA] Deaktiviert");
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[PULL-OTA] Kein WLAN");
    return;
  }
  if (_versionInfoUrl.length() == 0) {
    Serial.println("[PULL-OTA] Keine version.json URL gesetzt");
    return;
  }

  _pullUpdateRunning = true;
  Serial.print("[PULL-OTA] Lade Versionsinfo: ");
  Serial.println(_versionInfoUrl);

  HTTPClient http;
  if (!http.begin(_versionInfoUrl)) {
    Serial.println("[PULL-OTA] http.begin fehlgeschlagen");
    _pullUpdateRunning = false;
    return;
  }

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[PULL-OTA] HTTP Fehler: %d\n", httpCode);
    http.end();
    _pullUpdateRunning = false;
    return;
  }

  String payload = http.getString();
  http.end();

  String remoteVersion = extractJsonValue(payload, "version");
  String firmwareUrl   = extractJsonValue(payload, "url");

  if (remoteVersion.length() == 0 || firmwareUrl.length() == 0) {
    Serial.println("[PULL-OTA] version.json braucht 'version' und 'url'");
    _pullUpdateRunning = false;
    return;
  }

  Serial.print("[PULL-OTA] Lokal: ");
  Serial.println(_currentVersion);
  Serial.print("[PULL-OTA] Remote: ");
  Serial.println(remoteVersion);

  if (!isNewerVersion(remoteVersion, _currentVersion)) {
    Serial.println("[PULL-OTA] Kein Update erforderlich");
    _pullUpdateRunning = false;
    return;
  }

  Serial.print("[PULL-OTA] Starte Firmware-Update von: ");
  Serial.println(firmwareUrl);

  WiFiClient client;
  t_httpUpdate_return ret = httpUpdate.update(client, firmwareUrl);

  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("[PULL-OTA] Fehler (%d): %s\n",
                    httpUpdate.getLastError(),
                    httpUpdate.getLastErrorString().c_str());
      break;

    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("[PULL-OTA] Keine Updates");
      break;

    case HTTP_UPDATE_OK:
      Serial.println("[PULL-OTA] Update erfolgreich, Neustart folgt");
      _currentVersion = remoteVersion;
      saveSettings();
      break;
  }

  _pullUpdateRunning = false;
}

String OtaManager::buildOtaPageHtml() const
{
  String html =
    "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>OTA Update</title>"
    "<style>"
    "html,body{margin:0;padding:20px;background:#ffffff;font-family:Arial,sans-serif;text-align:center}"
    ".page{max-width:560px;margin:0 auto}"
    ".box{max-width:520px;margin:20px auto;padding:20px;border:1px solid #ccc;border-radius:8px;background:#fff}"
    ".button{display:block;width:100%;max-width:350px;padding:10px;margin:10px auto;"
    "border:1px solid #ccc;border-radius:4px;text-decoration:none;font-size:18px;color:#000;background:#fff;box-sizing:border-box}"
    ".button.primary{margin-top:20px}"
    ".button.nav{margin-top:18px}"
    "input,select{font-size:16px;padding:8px;margin:6px;width:90%;max-width:400px;box-sizing:border-box}"
    "small{display:block;margin-top:6px}"
    "</style></head><body>";

  html += "<div class='page'>";

  html += "<div class='box'>";
  html += "<h1>Browser OTA</h1>";
  html += "<form method='POST' action='/ota' enctype='multipart/form-data'>";
  html += "<input type='file' name='update'><br><br>";
  html += "<input class='button' type='submit' value='Browser-Update starten'>";
  html += "</form>";
  html += "</div>";

  html += "<form method='POST' action='/saveota'>";

  html += "<div class='box'>";
  html += "<h1>Arduino OTA</h1>";
  html += "Arduino OTA aktiv: <select name='ota_enabled'>";
  html += String("<option value='0'") + (_arduinoOtaEnabled ? "" : " selected") + ">AUS</option>";
  html += String("<option value='1'") + (_arduinoOtaEnabled ? " selected" : "") + ">EIN</option>";
  html += "</select><br>";
  html += "Arduino OTA Passwort: <input type='password' name='ota_pw' value='' placeholder='leer = unverändert' autocomplete='new-password'><br>";
  html += "<small>Dieses Passwort muss auch in PlatformIO bei --auth eingetragen werden.</small>";
  html += "</div>";

  html += "<div class='box'>";
  html += "<h1>Server Pull-Update</h1>";
  html += "Pull-Update aktiv: <select name='pull_enabled'>";
  html += String("<option value='0'") + (_pullUpdateEnabled ? "" : " selected") + ">AUS</option>";
  html += String("<option value='1'") + (_pullUpdateEnabled ? " selected" : "") + ">EIN</option>";
  html += "</select><br>";
  html += "Auto-Check beim Start: <select name='pull_auto'>";
  html += String("<option value='0'") + (_pullAutoCheckEnabled ? "" : " selected") + ">AUS</option>";
  html += String("<option value='1'") + (_pullAutoCheckEnabled ? " selected" : "") + ">EIN</option>";
  html += "</select><br>";
  html += "version.json URL: <input type='text' name='pull_url' value='" + _versionInfoUrl + "'><br>";
  html += "Aktuelle Firmware-Version: <input type='text' name='fw_version' value='" + _currentVersion + "'><br>";
  html += "<input class='button' type='submit' formaction='/checkupdate' formmethod='POST' value='Jetzt auf Server-Update prüfen'>";
  html += "</div>";

  html += "<input class='button primary' type='submit' value='OTA Einstellungen speichern'>";
  html += "</form>";

  html += "<a class='button nav' href='/'>Zurück zur Hauptseite</a>";
  html += "</div>";
  html += "</body></html>";

  return html;
}

void OtaManager::handleOtaPage(AsyncWebServerRequest* request)
{
  request->send(200, "text/html; charset=utf-8", buildOtaPageHtml());
}

void OtaManager::handleSaveOtaSettings(AsyncWebServerRequest* request)
{
  if (request->hasArg("ota_enabled")) {
    _arduinoOtaEnabled = request->arg("ota_enabled").toInt() == 1;
  }

  if (request->hasArg("ota_pw")) {
    String newPw = request->arg("ota_pw");
    if (newPw.length() > 0) {
      _arduinoOtaPassword = newPw;
    }
  }

  if (request->hasArg("pull_enabled")) {
    _pullUpdateEnabled = request->arg("pull_enabled").toInt() == 1;
  }

  if (request->hasArg("pull_auto")) {
    _pullAutoCheckEnabled = request->arg("pull_auto").toInt() == 1;
  }

  if (request->hasArg("pull_url")) {
    _versionInfoUrl = request->arg("pull_url");
  }

  if (request->hasArg("fw_version")) {
    _currentVersion = request->arg("fw_version");
  }

  saveSettings();

  String response =
    "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>OTA gespeichert</title>"
    "<style>html,body{margin:0;padding:0;height:100%;display:flex;flex-direction:column;"
    "align-items:center;justify-content:center;font-family:Arial,sans-serif}"
    ".button{display:block;width:100%;max-width:350px;padding:10px;margin:10px auto;"
    "border:1px solid #ccc;border-radius:4px;text-decoration:none;font-size:18px;color:#000}"
    "</style></head><body>"
    "<h1>OTA Einstellungen gespeichert</h1>"
    "<p>Arduino OTA: " + String(_arduinoOtaEnabled ? "EIN" : "AUS") + "</p>"
    "<p>Pull Update: " + String(_pullUpdateEnabled ? "EIN" : "AUS") + "</p>"
    "<p>Neustart wird ausgeführt ...</p>"
    "<a class='button' href='/'>Zurück zur Hauptseite</a>"
    "</body></html>";

  request->send(200, "text/html; charset=utf-8", response);
  delay(1500);
  ESP.restart();
}

void OtaManager::handleCheckPullUpdate(AsyncWebServerRequest* request)
{
  request->send(200, "text/html; charset=utf-8",
                "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Pull-Update</title></head>"
                "<body><h1>Prüfe auf Update ...</h1><a href='/ota'>Zurück</a></body></html>");
  delay(200);
  checkServerUpdateNow();
}

void OtaManager::handleBrowserOtaFinished(AsyncWebServerRequest* request)
{
  request->send(200, "text/plain", Update.hasError() ? "Update fehlgeschlagen" : "Update erfolgreich. Neustart...");
  delay(500);
  ESP.restart();
}

void OtaManager::handleBrowserOtaUpload(AsyncWebServerRequest* request,
                                        const String& filename,
                                        size_t index,
                                        uint8_t* data,
                                        size_t len,
                                        bool final)
{
  (void)request;
  (void)filename;

  if (index == 0) {
    Serial.setDebugOutput(true);

    uint32_t maxSketchSpace = ESP.getFreeSketchSpace();
    if (!Update.begin(maxSketchSpace)) {
      Update.printError(Serial);
    }
  }

  if (Update.write(data, len) != len) {
    Update.printError(Serial);
  }

  if (final) {
    if (Update.end(true)) {
      Serial.println("[OTA] Browser-OTA erfolgreich");
    } else {
      Update.printError(Serial);
    }
    Serial.setDebugOutput(false);
  }
}

void OtaManager::registerRoutes(AsyncWebServer& server)
{
  server.on("/ota", HTTP_GET, [this](AsyncWebServerRequest* request) {
    handleOtaPage(request);
  });

  server.on("/saveota", HTTP_POST, [this](AsyncWebServerRequest* request) {
    handleSaveOtaSettings(request);
  });

  server.on("/checkupdate", HTTP_POST, [this](AsyncWebServerRequest* request) {
    handleCheckPullUpdate(request);
  });

  server.on(
    "/ota",
    HTTP_POST,
    [this](AsyncWebServerRequest* request) {
      handleBrowserOtaFinished(request);
    },
    [this](AsyncWebServerRequest* request,
           const String& filename,
           size_t index,
           uint8_t* data,
           size_t len,
           bool final) {
      handleBrowserOtaUpload(request, filename, index, data, len, final);
    }
  );
}
