#pragma once

// Copy this file to include/arduino_secrets.h and fill in your local values.
// The real arduino_secrets.h is intentionally ignored by Git.

// Common Arduino aliases SECRET_SSID / SECRET_PASS are also accepted.
#define UART_WIFI_SSID       "YOUR_WIFI_SSID"
#define UART_WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"

// WLAN fallback policy. Secrets are tried first by default. If they fail,
// a WLAN saved via the web UI in NVS is tried next.
#define UART_WIFI_PREFER_SECRETS         true
#define UART_WIFI_ALLOW_NVS_FALLBACK     true
#define UART_WIFI_ALLOW_DHCP_FALLBACK    true
#define UART_WIFI_ALLOW_EMERGENCY_AP     true
#define UART_AP_KEEP_AFTER_CONNECT       false

// Optional network defaults
#define UART_STATIC_IP       ""                 // empty = DHCP
#define UART_GATEWAY_IP      "192.168.1.1"
#define UART_SUBNET_MASK     "255.255.255.0"
#define UART_AP_SSID         "Uart_Esp32-Setup"
#define UART_AP_PASSWORD     ""                 // empty = open AP; otherwise >= 8 chars
#define UART_HOSTNAME        "uart-esp32"
#define UART_NTP_SERVER      "pool.ntp.org"

// MQTT defaults
#define UART_MQTT_ENABLED       false
#define UART_MQTT_HOST          "192.168.1.10"
#define UART_MQTT_PORT          1883
#define UART_MQTT_USER          "YOUR_MQTT_USER"
#define UART_MQTT_PASSWORD      "YOUR_MQTT_PASSWORD"
#define UART_MQTT_TOPIC_BASE    ""
#define UART_MQTT_CLIENT_ID     ""
#define UART_MQTT_HEARTBEAT_MS  60000UL

// OTA defaults
#define UART_OTA_ENABLED          false
#define UART_OTA_PASSWORD         "CHANGE_ME"
#define UART_PULL_OTA_ENABLED     false
#define UART_PULL_OTA_AUTO_CHECK  false
#define UART_PULL_OTA_URL         ""
#define UART_FW_VERSION           "0.6.0"

// Optional battery defaults
#define UART_BATTERY_ENABLED  true
#define UART_BATTERY_ADC_PIN  34
#define UART_BATTERY_R1       100000.0f
#define UART_BATTERY_R2       33000.0f
#define UART_BATTERY_CAL      1.0f
#define UART_BATTERY_SAMPLES  20

// Optional Deep-Sleep defaults
#define UART_SLEEP_ENABLED                 false
#define UART_SLEEP_INTERVAL_MIN            15U
#define UART_SLEEP_AWAKE_S                 300U
#define UART_SLEEP_MIN_ONLINE_S            60U
#define UART_SLEEP_OTA_WINDOW_S            300U
#define UART_SLEEP_MQTT_OK_ONLY            true
#define UART_SLEEP_MQTT_TIMEOUT_S          120U
#define UART_SLEEP_NIGHT_ENABLED           false
#define UART_SLEEP_NIGHT_START_H           22U
#define UART_SLEEP_NIGHT_END_H             6U
#define UART_SLEEP_NIGHT_INTERVAL_MIN      60U
#define UART_SLEEP_WAKE_MODE               "interval"
#define UART_SLEEP_WAKE_OFFSET_MIN         0U
#define UART_SLEEP_FIXED_TIMES             "06:00,12:00,18:00"
#define UART_SLEEP_FALLBACK_MIN            15U
#define UART_SLEEP_BATTERY_ADAPTIVE        false
#define UART_SLEEP_LOW_V                   3.50f
#define UART_SLEEP_LOW_INTERVAL_MIN        60U
#define UART_SLEEP_CRITICAL_V              3.30f
#define UART_SLEEP_CRITICAL_INTERVAL_MIN   180U
