#pragma once
#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <Update.h>
#include <ArduinoOTA.h>

class OtaManager {
public:
  OtaManager();

  void begin(Preferences& prefs);
  void registerRoutes(AsyncWebServer& server);

  void startArduinoOta(const String& hostname);
  void loop();

  bool isArduinoOtaEnabled() const;
  String getArduinoOtaPassword() const;

  bool isPullUpdateEnabled() const;
  bool isPullAutoCheckEnabled() const;
  String getVersionInfoUrl() const;
  String getCurrentVersion() const;

private:
  Preferences* _prefs;

  bool _arduinoOtaEnabled;
  String _arduinoOtaPassword;
  bool _arduinoOtaStarted;

  bool _pullUpdateEnabled;
  bool _pullAutoCheckEnabled;
  bool _pullUpdateRunning;
  String _versionInfoUrl;
  String _currentVersion;

  void loadSettings();
  void saveSettings();

  void checkServerUpdateNow();
  bool isNewerVersion(const String& remote, const String& local) const;
  String extractJsonValue(const String& json, const String& key) const;

  String buildOtaPageHtml() const;

  void handleOtaPage(AsyncWebServerRequest* request);
  void handleSaveOtaSettings(AsyncWebServerRequest* request);
  void handleCheckPullUpdate(AsyncWebServerRequest* request);
  void handleBrowserOtaFinished(AsyncWebServerRequest* request);
  void handleBrowserOtaUpload(AsyncWebServerRequest* request,
                              const String& filename,
                              size_t index,
                              uint8_t* data,
                              size_t len,
                              bool final);
};

#endif
