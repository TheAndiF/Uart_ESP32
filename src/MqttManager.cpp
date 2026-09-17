#include "MqttManager.h"

void MqttManager::begin(WiFiClient& wifiClient) {
  _wifiClient = &wifiClient;
  _client = new PubSubClient(wifiClient);
  _client->setBufferSize(2048);
}

void MqttManager::configure(const String& host, uint16_t port,
                            const String& user, const String& password,
                            const String& topicBase, const String& clientId,
                            bool enabled) {
  _host = host; _port = port; _user = user; _password = password;
  _topicBase = topicBase; _clientId = clientId; _enabled = enabled;
  _host.trim(); _topicBase.trim(); _clientId.trim();
  if (_client) _client->setServer(_host.c_str(), _port);
  if (!_enabled) _lastStatus = "deaktiviert";
  else if (_host.length() == 0 || _topicBase.length() == 0) _lastStatus = "Einstellungen unvollstaendig";
  else _lastStatus = "nicht verbunden";
}

void MqttManager::setCallback(MQTT_CALLBACK_SIGNATURE) {
  if (_client) _client->setCallback(callback);
}

bool MqttManager::tryConnectOnce() {
  if (!_enabled) { _lastStatus = "deaktiviert"; return false; }
  if (!_client) { _lastStatus = "Client nicht initialisiert"; return false; }
  if (WiFi.status() != WL_CONNECTED) { _lastStatus = "kein WLAN"; return false; }
  if (_host.length() == 0 || _topicBase.length() == 0) { _lastStatus = "Einstellungen unvollstaendig"; return false; }
  if (_client->connected()) { _lastStatus = "verbunden"; return true; }

  String id = _clientId;
  if (id.length() == 0) id = "Uart_Esp32_" + String((uint32_t)(ESP.getEfuseMac() & 0xFFFFFFFFULL), HEX);
  String willTopic = _topicBase + "/status/online";

  bool ok;
  if (_user.length()) {
    ok = _client->connect(id.c_str(), _user.c_str(), _password.c_str(),
                          willTopic.c_str(), 0, true, "false");
  } else {
    ok = _client->connect(id.c_str(), nullptr, nullptr,
                          willTopic.c_str(), 0, true, "false");
  }

  if (!ok) {
    _lastStatus = "Verbindung fehlgeschlagen (state=" + String(_client->state()) + ")";
    return false;
  }

  _lastStatus = "verbunden";
  _client->subscribe((_topicBase + "/cmd/#").c_str());
  _client->publish(willTopic.c_str(), "true", true);
  return true;
}

void MqttManager::connectIfNeeded() {
  if (!_enabled) return;
  if (_client && _client->connected()) { _lastStatus = "verbunden"; return; }
  tryConnectOnce();
}

void MqttManager::loop() {
  if (_enabled && _client) _client->loop();
}

void MqttManager::disconnect() {
  if (_client && _client->connected()) {
    String t = _topicBase + "/status/online";
    _client->publish(t.c_str(), "false", true);
    _client->disconnect();
  }
  _lastStatus = _enabled ? "nicht verbunden" : "deaktiviert";
}

bool MqttManager::publish(const String& topic, const String& payload, bool retained) {
  if (!_enabled || !_client) return false;
  if (!_client->connected()) connectIfNeeded();
  if (!_client->connected()) return false;
  bool ok = _client->publish(topic.c_str(), payload.c_str(), retained);
  if (!ok) _lastStatus = "Publish fehlgeschlagen";
  return ok;
}
