#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

class MqttManager {
public:
  void begin(WiFiClient& wifiClient);
  void configure(const String& host, uint16_t port, const String& user,
                 const String& password, const String& topicBase,
                 const String& clientId, bool enabled);
  void setCallback(MQTT_CALLBACK_SIGNATURE);

  bool tryConnectOnce();
  void connectIfNeeded();
  void loop();
  void disconnect();

  bool publish(const String& topic, const String& payload, bool retained = false);
  bool isEnabled() const { return _enabled; }
  bool isConnected() const { return _client && _client->connected(); }
  String getLastStatus() const { return _lastStatus; }
  String getTopicBase() const { return _topicBase; }

private:
  PubSubClient* _client = nullptr;
  WiFiClient* _wifiClient = nullptr;
  String _host, _user, _password, _topicBase, _clientId;
  uint16_t _port = 1883;
  bool _enabled = false;
  String _lastStatus = "deaktiviert";
};
