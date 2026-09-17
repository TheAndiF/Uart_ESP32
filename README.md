# Uart_Esp32 - WROOM32 Infrastructure

Reduzierte Firmware für einen klassischen **ESP32-WROOM-32 / NodeMCU-32S**.

## Stand v0.6


**Buildfix v0.6:** In `markWifiConnected()` war die Zuweisung `wifiDhcpFallback = pendingWifiForceDhcp;` versehentlich zwischen einem `if` und dem zugehoerigen `else if` eingefuegt. Dadurch meldete GCC `else without a previous if`. Die Verzweigung ist jetzt korrekt geklammert und die DHCP-Fallback-Markierung wird erst nach der Quellenwahl gesetzt.

Der WLAN-Teil wurde fuer instabile Verbindungen und frische Geraete robuster aufgebaut. Die Firmware verwendet jetzt mehrere voneinander unabhaengige Fallback-Stufen statt wiederholt unmittelbar zwischen STA und AP umzuschalten. Dadurch sollen insbesondere Meldungen wie `mode(): Could not set mode!` und `softAP(): enable AP first!` vermieden werden.

Fuer WLAN gilt standardmaessig diese Reihenfolge:

1. gueltige Zugangsdaten aus `include/arduino_secrets.h`,
2. ein separat ueber das Webinterface in NVS gespeichertes WLAN,
3. bei konfigurierter statischer IP ein zweiter Versuch ueber DHCP,
4. Fallback-AP im Modus AP+STA, damit Provisionierung und spaetere Reconnects parallel moeglich bleiben,
5. falls der konfigurierte AP nicht startet: offener Emergency-AP `Uart_Esp32-Recovery-<ChipID>`.

Platzhalter wie `YOUR_WIFI_SSID` oder `CHANGE_ME` gelten nicht als gueltige Secrets. Ein NVS-WLAN bleibt als echte Alternative erhalten, auch wenn eine `arduino_secrets.h` vorhanden ist. Der Fallback-AP wird nach einer stabilen STA-Verbindung standardmaessig nach 15 Sekunden abgeschaltet. Faellt WLAN spaeter aus, wird er automatisch wieder bereitgestellt.

Normale Laufzeiteinstellungen verwenden weiterhin **NVS > arduino_secrets.h > interne Defaults**. Dadurch bleiben Einstellungen aus dem Webinterface wirksam. WLAN-Zugangsdaten sind die Ausnahme: hier ist `arduino_secrets.h` standardmaessig der erste Verbindungskandidat und NVS der zweite.

Die Startseite und `/network` zeigen nun zusaetzlich WLAN-Modus, aktive Zugangsdatenquelle, Reconnect-Zaehler, Fehlerzaehler und Resetgrund. Im seriellen Boot-Log stehen Firmwarestand, Build-Zeit und Resetursache.

Das Projekt enthaelt weiterhin nur die Infrastruktur-Funktionen aus der gewuenschten Auswahl:

- WLAN-Client mit robustem Reconnect und mehreren Fallbacks
- Fallback Access Point / Emergency-AP
- NTP + deutsche CET/CEST-Zeitzone mit automatischer Sommer-/Winterzeit
- Webinterface
- MQTT mit Statuswerten, Fernsteuerung und Heartbeat
- Deep Sleep mit Intervall, voller Stunde, festen Zeiten, Mixed-Modus, Einmal-Wakeup, Nachtmodus, Batterie- und MQTT-Abhaengigkeit
- Browser OTA
- ArduinoOTA / PlatformIO OTA
- HTTP Pull OTA
- NVS / Preferences
- interne ESP32-Temperatur
- Web-Reboot

Entfernt bleiben u. a. VL53L1X, UART-Entfernungssensor, INA226, Regentonnenkontakt, Feeder, USB/GPIO-Portsteuerung und die Distanz-Messautomatik. Die ADC-Batteriemessung ist nur als Datenquelle fuer den batterieabhaengigen Deep Sleep enthalten.

## Hardware / PlatformIO

`platformio.ini` verwendet:

```ini
board = nodemcu-32s
framework = arduino
```

Standardumgebung:

```ini
[env:uart_esp32_usb]
upload_protocol = esptool
```

Nach dem ersten USB-Flash kann optional die OTA-Umgebung benutzt werden:

```ini
[env:uart_esp32_ota]
upload_protocol = espota
```

`upload_port` und `--auth` in `platformio.ini` müssen dann zu deinem Gerät passen.

## Konfiguration / arduino_secrets.h

Die Datei `include/arduino_secrets.h` ist optional und wird absichtlich nicht mit Git versioniert. Als Vorlage liegt `include/arduino_secrets.example.h` im Projekt.

Beispiel:

```cpp
#pragma once
#define UART_WIFI_SSID       "MeinWLAN"
#define UART_WIFI_PASSWORD   "MeinPasswort"
#define UART_MQTT_ENABLED    true
#define UART_MQTT_HOST       "192.168.1.10"
#define UART_MQTT_USER       "mqtt"
#define UART_MQTT_PASSWORD   "geheim"
#define UART_OTA_PASSWORD    "geheim"
```

Die üblichen Arduino-Namen `SECRET_SSID` und `SECRET_PASS` werden ebenfalls als WLAN-Aliase akzeptiert. Alle Defines sind optional. Fehlt die Datei oder ein einzelner Wert, verwendet die Firmware sichere Defaults. Zugangsdaten-Dummies sind standardmäßig leer, damit nicht versehentlich eine Verbindung mit Platzhalterwerten versucht wird.

Für normale Einstellungen gilt **NVS > arduino_secrets.h > interne Defaults**. Für WLAN-Zugangsdaten gilt standardmäßig **arduino_secrets.h > NVS > AP**. Die Reihenfolge und einzelne Fallbacks können über die in `arduino_secrets.example.h` dokumentierten Makros angepasst werden.

## Erster Start / WLAN-Provisionierung

Sind gueltige WLAN-Zugangsdaten in `arduino_secrets.h` vorhanden, werden sie zuerst versucht. Schlaegt dieses WLAN fehl, probiert die Firmware ein ueber das Webinterface gespeichertes NVS-WLAN. Sind statische IP-Daten gesetzt und die Verbindung scheitert, folgt optional ein zweiter Durchlauf mit DHCP.

Sind keine funktionierenden Client-Zugangsdaten vorhanden, startet der Provisionierungs-AP:

- Standard-SSID: `Uart_Esp32-Setup`
- IP: `192.168.4.1`
- Passwort: keines, sofern nicht konfiguriert

Im Browser `http://192.168.4.1/` oeffnen und **WLAN / NTP** waehlen. Dort werden - sofern AP+STA zur Verfuegung steht - WLANs gescannt und angezeigt. Ausgewaehlte SSID und Passwort werden als NVS-Fallback gespeichert. Eine vorhandene Secret-SSID bleibt trotzdem der erste Boot-Kandidat.

Falls AP+STA vom WLAN-Treiber nicht aktiviert werden kann, versucht die Firmware einen reinen AP-Recovery-Modus und laesst diesen stabil aktiv, statt ihn durch weitere automatische Reconnect-Umschaltungen zu zerstoeren. Kann auch der konfigurierte AP nicht gestartet werden, wird als letzte Stufe ein offener Emergency-AP `Uart_Esp32-Recovery-<ChipID>` versucht.

Eine leere statische IP bedeutet DHCP. Das gespeicherte NVS-WLAN kann auf `/network` auch gezielt geloescht werden.

### WLAN-Fallback-Schalter

In `arduino_secrets.h` koennen optional folgende Werte gesetzt werden:

```cpp
#define UART_WIFI_PREFER_SECRETS         true
#define UART_WIFI_ALLOW_NVS_FALLBACK     true
#define UART_WIFI_ALLOW_DHCP_FALLBACK    true
#define UART_WIFI_ALLOW_EMERGENCY_AP     true
#define UART_AP_KEEP_AFTER_CONNECT       false
```

## Batterie / ADC

Standard:

- ADC: GPIO34
- R1: 100 kOhm, Batterie+ -> ADC
- R2: 33 kOhm, ADC -> GND
- Mittelung: 20 Messungen
- Kalibrierfaktor: 1.000

Es werden nur **ADC1-Pins GPIO32..39** zugelassen. Dadurch kollidiert die Messung nicht mit aktivem WLAN wie es bei ADC2-Pins des klassischen ESP32 passieren kann.

Die Berechnung lautet:

```text
U_Batterie = U_ADC * (R1 + R2) / R2 * Kalibrierfaktor
```

Vor dem Anschließen bitte sicherstellen, dass die maximale Spannung am ADC-Pin innerhalb des zulässigen ESP32-Bereichs bleibt.

## Web-Routen

| Route | Funktion |
|---|---|
| `/` | Hauptstatus |
| `/network` | WLAN-Scan/Auswahl, AP, Hostname, NTP |
| `/wifi_rescan` | WLAN-Scan neu starten |
| `/clear_wifi_nvs` | gespeichertes NVS-WLAN löschen, POST |
| `/mqtt` | MQTT-Einstellungen / Verbindungstest |
| `/battery` | ADC-Batteriemessung für Deep Sleep |
| `/deepsleep` | Deep-Sleep-Konfiguration |
| `/ota` | Browser OTA, ArduinoOTA, Pull OTA |
| `/reboot` | Neustart, POST |

## MQTT

Die Topic-Basis wird auf einem frischen Gerät automatisch erzeugt:

```text
Uart_Esp32_<Chip-ID>
```

Alle Fernsteuerbefehle liegen unter:

```text
<Basis>/cmd/#
```

### Allgemeine Befehle

| Topic | Payload | Funktion |
|---|---|---|
| `cmd/get` | beliebig | Status sofort veröffentlichen |
| `cmd/status/get` | beliebig | Status sofort veröffentlichen |
| `cmd/heartbeat_interval_s` | Sekunden | Heartbeat-Intervall setzen |
| `cmd/battery/measure` | beliebig | Batterie sofort messen |
| `cmd/reboot` | beliebig | ESP32 neu starten |
| `cmd/sleep/get` | beliebig | Deep-Sleep-Status veröffentlichen |

### Deep-Sleep-Befehle

Alle folgenden Topics beginnen mit `<Basis>/cmd/sleep/`:

| Untertopic | Beispiel | Funktion |
|---|---:|---|
| `enabled` | `1` | Deep Sleep ein/aus |
| `interval_min` | `15` | Basis-Schlafintervall |
| `awake_s` | `300` | Online-Zeit |
| `min_online_s` | `60` | Mindest-Onlinezeit |
| `ota_window_s` | `300` | OTA-Schutzfenster |
| `mqtt_ok_only` | `1` | nur nach MQTT-Verbindung schlafen |
| `mqtt_timeout_s` | `120` | maximale MQTT-Wartezeit |
| `night_enabled` | `1` | Nachtmodus |
| `night_start_h` | `22` | Nachtbeginn |
| `night_end_h` | `6` | Nachtende |
| `night_interval_min` | `60` | Schlafintervall nachts |
| `battery_adaptive` | `1` | batterieabhängige Intervalle |
| `low_voltage_threshold_v` | `3.50` | Low-Schwelle |
| `low_voltage_interval_min` | `60` | Low-Intervall |
| `critical_voltage_threshold_v` | `3.30` | Kritisch-Schwelle |
| `critical_voltage_interval_min` | `180` | Kritisch-Intervall |
| `wakeup_mode` | `mixed` | `interval`, `full_hour`, `fixed`, `mixed` |
| `wakeup_offset_min` | `5` | Minutenoffset für volle Stunde |
| `fixed_times` | `06:00,12:00,18:00` | feste Aufwachzeiten |
| `fallback_interval_min` | `15` | Fallback ohne gültige Uhrzeit |
| `wake_once` | `2026-09-18 08:00:00` | einmaliger Wakeup |
| `wake_once_in_min` | `30` | einmaliger Wakeup relativ |
| `wake_once_clear` | beliebig | einmaligen Wakeup löschen |
| `now` | `1` oder `sleep` | sofort schlafen |

### Veröffentlichte Statuswerte

Unter `<Basis>/status/` werden u. a. veröffentlicht:

- `online`
- `hostname`
- `ip`
- `rssi`
- `uptime_s`
- `heartbeat_interval_s`
- `esp32_temp_c`
- `time`
- `mqtt_status`
- `firmware`

Unter `<Basis>/battery/`:

- `enabled`
- `status`
- `valid`
- `voltage_v`
- `adc_voltage_v`
- `raw_mv`
- `time`

Unter `<Basis>/sleep/`:

- `enabled`
- `wakeup_mode`
- `remaining_online_s`
- `next_wakeup_s`
- `next_wakeup_epoch`
- `next_wakeup_text`
- `settings_json`
- beim Einschlafen zusätzlich `state` und `reason`

MQTT verwendet einen retained Last-Will auf `<Basis>/status/online = false`.

## OTA

### Browser OTA

Webseite `/ota`, Firmware-`.bin` auswählen und hochladen.

### ArduinoOTA / PlatformIO OTA

Im Webinterface aktivieren und Passwort setzen. Danach `uart_esp32_ota` in PlatformIO verwenden. `upload_port` und `--auth` müssen angepasst werden.

### HTTP Pull OTA

Im OTA-Webinterface kann eine `version.json` URL eingetragen werden. Erwartetes Format:

```json
{
  "version": "1.2.0",
  "url": "http://server/firmware.bin"
}
```

Die reduzierte Version vergleicht Versionsnummern numerisch segmentweise, statt nur auf Ungleichheit zu prüfen.

## NVS-Kompatibilität

Die wichtigen NVS-Keys der ursprünglichen Firmware wurden soweit sinnvoll beibehalten (`netconf`), insbesondere für:

- WLAN/AP/NTP
- MQTT
- OTA
- Deep Sleep
- Batterie-ADC

Dadurch bleiben vorhandene Einstellungen bei einem Firmwarewechsel auf demselben ESP32 in vielen Fällen nutzbar.

## Prüfung

Die Quelldateien wurden mit einem lokalen C++-Syntaxcheck gegen ESP32/Arduino-API-Stubs geprüft. Eine echte PlatformIO-Kompilierung war in der Erstellungsumgebung nicht möglich, weil PlatformIO dort nicht installiert war und kein Paketdownload verfügbar war. Vor dem Flashen daher einmal lokal ausführen:

```bash
pio run -e uart_esp32_usb
```

und anschließend z. B.:

```bash
pio run -e uart_esp32_usb -t upload
```
