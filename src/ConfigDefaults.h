#pragma once

// Optional local secrets file. Keep it out of Git.
#if __has_include("arduino_secrets.h")
  #include "arduino_secrets.h"
  #define UART_ESP32_HAS_SECRETS 1
#else
  #define UART_ESP32_HAS_SECRETS 0
#endif

// Supported aliases for common Arduino-style secret names.
#if !defined(UART_WIFI_SSID) && defined(SECRET_SSID)
  #define UART_WIFI_SSID SECRET_SSID
#endif
#if !defined(UART_WIFI_PASSWORD) && defined(SECRET_PASS)
  #define UART_WIFI_PASSWORD SECRET_PASS
#endif

// Remember which values were explicitly supplied by arduino_secrets.h before
// safe defaults are defined below. These flags let runtime code distinguish
// a real secret value from a built-in dummy/default value.
#ifdef UART_WIFI_SSID
  #define UART_SECRET_HAS_WIFI_SSID 1
#else
  #define UART_SECRET_HAS_WIFI_SSID 0
#endif
#ifdef UART_WIFI_PASSWORD
  #define UART_SECRET_HAS_WIFI_PASSWORD 1
#else
  #define UART_SECRET_HAS_WIFI_PASSWORD 0
#endif
#ifdef UART_STATIC_IP
  #define UART_SECRET_HAS_STATIC_IP 1
#else
  #define UART_SECRET_HAS_STATIC_IP 0
#endif
#ifdef UART_GATEWAY_IP
  #define UART_SECRET_HAS_GATEWAY_IP 1
#else
  #define UART_SECRET_HAS_GATEWAY_IP 0
#endif
#ifdef UART_SUBNET_MASK
  #define UART_SECRET_HAS_SUBNET_MASK 1
#else
  #define UART_SECRET_HAS_SUBNET_MASK 0
#endif
#ifdef UART_AP_SSID
  #define UART_SECRET_HAS_AP_SSID 1
#else
  #define UART_SECRET_HAS_AP_SSID 0
#endif
#ifdef UART_AP_PASSWORD
  #define UART_SECRET_HAS_AP_PASSWORD 1
#else
  #define UART_SECRET_HAS_AP_PASSWORD 0
#endif
#ifdef UART_HOSTNAME
  #define UART_SECRET_HAS_HOSTNAME 1
#else
  #define UART_SECRET_HAS_HOSTNAME 0
#endif
#ifdef UART_NTP_SERVER
  #define UART_SECRET_HAS_NTP_SERVER 1
#else
  #define UART_SECRET_HAS_NTP_SERVER 0
#endif
#ifdef UART_MQTT_HOST
  #define UART_SECRET_HAS_MQTT_HOST 1
#else
  #define UART_SECRET_HAS_MQTT_HOST 0
#endif
#ifdef UART_MQTT_USER
  #define UART_SECRET_HAS_MQTT_USER 1
#else
  #define UART_SECRET_HAS_MQTT_USER 0
#endif
#ifdef UART_MQTT_PASSWORD
  #define UART_SECRET_HAS_MQTT_PASSWORD 1
#else
  #define UART_SECRET_HAS_MQTT_PASSWORD 0
#endif
#ifdef UART_MQTT_TOPIC_BASE
  #define UART_SECRET_HAS_MQTT_TOPIC_BASE 1
#else
  #define UART_SECRET_HAS_MQTT_TOPIC_BASE 0
#endif
#ifdef UART_MQTT_CLIENT_ID
  #define UART_SECRET_HAS_MQTT_CLIENT_ID 1
#else
  #define UART_SECRET_HAS_MQTT_CLIENT_ID 0
#endif
#ifdef UART_OTA_PASSWORD
  #define UART_SECRET_HAS_OTA_PASSWORD 1
#else
  #define UART_SECRET_HAS_OTA_PASSWORD 0
#endif
#ifdef UART_PULL_OTA_URL
  #define UART_SECRET_HAS_PULL_OTA_URL 1
#else
  #define UART_SECRET_HAS_PULL_OTA_URL 0
#endif

// Safe/dummy defaults when arduino_secrets.h or individual defines are missing.
#ifndef UART_WIFI_SSID
  #define UART_WIFI_SSID ""
#endif
#ifndef UART_WIFI_PASSWORD
  #define UART_WIFI_PASSWORD ""
#endif
#ifndef UART_STATIC_IP
  #define UART_STATIC_IP ""
#endif
#ifndef UART_GATEWAY_IP
  #define UART_GATEWAY_IP "192.168.1.1"
#endif
#ifndef UART_SUBNET_MASK
  #define UART_SUBNET_MASK "255.255.255.0"
#endif
#ifndef UART_AP_SSID
  #define UART_AP_SSID "Uart_Esp32-Setup"
#endif
#ifndef UART_AP_PASSWORD
  #define UART_AP_PASSWORD ""
#endif
#ifndef UART_HOSTNAME
  #define UART_HOSTNAME "uart-esp32"
#endif
#ifndef UART_NTP_SERVER
  #define UART_NTP_SERVER "pool.ntp.org"
#endif

// WLAN recovery/fallback policy.
#ifndef UART_WIFI_PREFER_SECRETS
  #define UART_WIFI_PREFER_SECRETS true
#endif
#ifndef UART_WIFI_ALLOW_NVS_FALLBACK
  #define UART_WIFI_ALLOW_NVS_FALLBACK true
#endif
#ifndef UART_WIFI_ALLOW_DHCP_FALLBACK
  #define UART_WIFI_ALLOW_DHCP_FALLBACK true
#endif
#ifndef UART_WIFI_ALLOW_EMERGENCY_AP
  #define UART_WIFI_ALLOW_EMERGENCY_AP true
#endif
#ifndef UART_WIFI_CONNECT_TIMEOUT_MS
  #define UART_WIFI_CONNECT_TIMEOUT_MS 12000UL
#endif
#ifndef UART_WIFI_RECONNECT_INTERVAL_MS
  #define UART_WIFI_RECONNECT_INTERVAL_MS 30000UL
#endif
#ifndef UART_WIFI_AP_START_AFTER_MS
  #define UART_WIFI_AP_START_AFTER_MS 5000UL
#endif
#ifndef UART_AP_KEEP_AFTER_CONNECT
  #define UART_AP_KEEP_AFTER_CONNECT false
#endif
#ifndef UART_AP_SHUTDOWN_DELAY_MS
  #define UART_AP_SHUTDOWN_DELAY_MS 15000UL
#endif
#ifndef UART_EMERGENCY_AP_PREFIX
  #define UART_EMERGENCY_AP_PREFIX "Uart_Esp32-Recovery"
#endif

#ifndef UART_MQTT_ENABLED
  #define UART_MQTT_ENABLED false
#endif
#ifndef UART_MQTT_HOST
  #define UART_MQTT_HOST ""
#endif
#ifndef UART_MQTT_PORT
  #define UART_MQTT_PORT 1883
#endif
#ifndef UART_MQTT_USER
  #define UART_MQTT_USER ""
#endif
#ifndef UART_MQTT_PASSWORD
  #define UART_MQTT_PASSWORD ""
#endif
#ifndef UART_MQTT_TOPIC_BASE
  #define UART_MQTT_TOPIC_BASE ""
#endif
#ifndef UART_MQTT_CLIENT_ID
  #define UART_MQTT_CLIENT_ID ""
#endif
#ifndef UART_MQTT_HEARTBEAT_MS
  #define UART_MQTT_HEARTBEAT_MS 60000UL
#endif

#ifndef UART_OTA_ENABLED
  #define UART_OTA_ENABLED false
#endif
#ifndef UART_OTA_PASSWORD
  #define UART_OTA_PASSWORD "ota123"
#endif
#ifndef UART_PULL_OTA_ENABLED
  #define UART_PULL_OTA_ENABLED false
#endif
#ifndef UART_PULL_OTA_AUTO_CHECK
  #define UART_PULL_OTA_AUTO_CHECK false
#endif
#ifndef UART_PULL_OTA_URL
  #define UART_PULL_OTA_URL ""
#endif
#ifndef UART_FW_VERSION
  #define UART_FW_VERSION "0.20.0"
#endif

#ifndef UART_BATTERY_ENABLED
  #define UART_BATTERY_ENABLED true
#endif
#ifndef UART_BATTERY_ADC_PIN
  #define UART_BATTERY_ADC_PIN 34
#endif
#ifndef UART_BATTERY_R1
  #define UART_BATTERY_R1 100000.0f
#endif
#ifndef UART_BATTERY_R2
  #define UART_BATTERY_R2 33000.0f
#endif
#ifndef UART_BATTERY_CAL
  #define UART_BATTERY_CAL 1.0f
#endif
#ifndef UART_BATTERY_SAMPLES
  #define UART_BATTERY_SAMPLES 20
#endif

#ifndef UART_SLEEP_ENABLED
  #define UART_SLEEP_ENABLED false
#endif
#ifndef UART_SLEEP_INTERVAL_MIN
  #define UART_SLEEP_INTERVAL_MIN 15U
#endif
#ifndef UART_SLEEP_AWAKE_S
  #define UART_SLEEP_AWAKE_S 300U
#endif
#ifndef UART_SLEEP_MIN_ONLINE_S
  #define UART_SLEEP_MIN_ONLINE_S 60U
#endif
#ifndef UART_SLEEP_OTA_WINDOW_S
  #define UART_SLEEP_OTA_WINDOW_S 300U
#endif
#ifndef UART_SLEEP_MQTT_OK_ONLY
  #define UART_SLEEP_MQTT_OK_ONLY true
#endif
#ifndef UART_SLEEP_MQTT_TIMEOUT_S
  #define UART_SLEEP_MQTT_TIMEOUT_S 120U
#endif
#ifndef UART_SLEEP_NIGHT_ENABLED
  #define UART_SLEEP_NIGHT_ENABLED false
#endif
#ifndef UART_SLEEP_NIGHT_START_H
  #define UART_SLEEP_NIGHT_START_H 22U
#endif
#ifndef UART_SLEEP_NIGHT_END_H
  #define UART_SLEEP_NIGHT_END_H 6U
#endif
#ifndef UART_SLEEP_NIGHT_INTERVAL_MIN
  #define UART_SLEEP_NIGHT_INTERVAL_MIN 60U
#endif
#ifndef UART_SLEEP_WAKE_MODE
  #define UART_SLEEP_WAKE_MODE "interval"
#endif
#ifndef UART_SLEEP_WAKE_OFFSET_MIN
  #define UART_SLEEP_WAKE_OFFSET_MIN 0U
#endif
#ifndef UART_SLEEP_FIXED_TIMES
  #define UART_SLEEP_FIXED_TIMES "06:00,12:00,18:00"
#endif
#ifndef UART_SLEEP_FALLBACK_MIN
  #define UART_SLEEP_FALLBACK_MIN 15U
#endif
#ifndef UART_SLEEP_BATTERY_ADAPTIVE
  #define UART_SLEEP_BATTERY_ADAPTIVE false
#endif
#ifndef UART_SLEEP_LOW_V
  #define UART_SLEEP_LOW_V 3.50f
#endif
#ifndef UART_SLEEP_LOW_INTERVAL_MIN
  #define UART_SLEEP_LOW_INTERVAL_MIN 60U
#endif
#ifndef UART_SLEEP_CRITICAL_V
  #define UART_SLEEP_CRITICAL_V 3.30f
#endif
#ifndef UART_SLEEP_CRITICAL_INTERVAL_MIN
  #define UART_SLEEP_CRITICAL_INTERVAL_MIN 180U
#endif

// A copied example file may still contain placeholders. Treat those as
// unavailable so that NVS/AP fallbacks can take over instead of trying
// obviously invalid credentials.
static inline bool uartSecretTextUsable(const char* value) {
  if (!value || !value[0]) return false;
  String text(value);
  text.trim();
  if (!text.length()) return false;
  if (text.startsWith("YOUR_")) return false;
  if (text == "CHANGE_ME") return false;
  return true;
}
