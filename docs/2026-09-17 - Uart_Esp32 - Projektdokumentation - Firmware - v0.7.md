# Uart_Esp32 - Projektdokumentation - Firmware

**Projektname:** Uart_Esp32  
**Dokumentenart:** Projektdokumentation  
**Thema:** Firmware fuer ESP32-WROOM-32 / NodeMCU-32S - UART Raw/Sniffer und Protokoll-Decoder  
**Erstellungsdatum:** 2026-09-17  
**Autor / verantwortliche Person:** OpenAI ChatGPT (technische Erstellung)  
**Version:** v0.7  
**Status:** Entwurf / lokale Build- und Hardwarepruefung ausstehend  
**Aenderungsdatum:** 2026-09-17  
**Ablageort:** Projektordner `Uart_ESP32/docs`  
**Referenzen:** `Regeln_Projektdokumentation_PDF_DOCX_Pflicht_(5).pdf`; `ESP32_UART_Protokoll_Entschluesselung(1).pdf`; Projektstand v0.6

## Aenderungshistorie

| Version | Datum | Bearbeiter | Status | Aenderung |
|---|---|---|---|---|
| v0.1 | 2026-09-17 | OpenAI ChatGPT | Entwurf | Reduzierter WROOM32-Projektstand erstellt |
| v0.2 | 2026-09-17 | OpenAI ChatGPT | Entwurf | Projekt in Uart_Esp32 umbenannt und Dokumentationsregeln umgesetzt |
| v0.3 | 2026-09-17 | OpenAI ChatGPT | Entwurf | Buildfehler durch typensichere Integer-Vergleiche behoben |
| v0.4 | 2026-09-17 | OpenAI ChatGPT | Entwurf | Arduino-Secrets, NVS-Fallbacks und WLAN-Provisionierung ergaenzt |
| v0.5 | 2026-09-17 | OpenAI ChatGPT | Entwurf | WLAN-State-/Fallback-Logik, Recovery-AP, DHCP-Fallback und Resetdiagnose ergaenzt |
| v0.6 | 2026-09-17 | OpenAI ChatGPT | Entwurf | Compilerfehler in `markWifiConnected()` behoben |
| v0.7 | 2026-09-17 | OpenAI ChatGPT | Entwurf | UART Raw/Sniffer, Protokoll-Decoder, Feld-Aliase, Kalibrierung und UART-Webseite ergaenzt |

## Ziel des Standes v0.7

Der Projektstand v0.7 erweitert die bestehende Firmware um zwei getrennte serielle Betriebsarten. Im Raw-/Sniffer-Modus werden empfangene UART-Bytes ohne Interpretation erfasst und im Webinterface als HEX und ASCII dargestellt. Im Protokoll-Decoder-Modus wird zusaetzlich das in der Referenzdokumentation beschriebene UART-Protokoll ausgewertet.

Die technische Feldkennung `Feld 1` bis `Feld 6` bleibt immer sichtbar. Zu jedem Feld kann ein frei editierbarer Alias gespeichert werden, z. B. `Feld 1 - Roll`. Dadurch koennen spaeter bestaetigte Bedeutungen eingetragen werden, ohne die neutrale Feldnummerierung zu verlieren.

## UART-Betriebsarten

| Modus | Verhalten |
|---|---|
| Aus | Zusaetzliche Hardware-UART ist deaktiviert. |
| Raw / Sniffer | Bytes werden empfangen und in einem 512-Byte-Ringpuffer gespeichert. Die letzten 256 Bytes werden live als HEX und ASCII angezeigt. |
| Protokoll-Decoder | Raw-Erfassung bleibt aktiv; zusaetzlich werden Pakete synchronisiert, geprueft und die bestaetigten Hauptpakete dekodiert. |

UART0 bleibt fuer den seriellen Debug-Monitor reserviert. Fuer die Zusatzfunktion sind Hardware-UART1 und UART2 waehlbar. RX und TX sind frei konfigurierbar; TX kann mit `-1` deaktiviert werden. Standard ist RX GPIO16, TX deaktiviert, 115200 Baud, 8N1.

Nicht zugelassen sind GPIO6..11 wegen des externen Flashs sowie GPIO1/3, damit der Debug-UART nicht kollidiert. GPIO34..39 sind nur als RX geeignet, da sie eingangs-only sind.

## Decoder gemaess Messunterlage

Der Decoder verwendet die in `ESP32_UART_Protokoll_Entschluesselung(1).pdf` dokumentierte Struktur:

1. Suche nach `FF`.
2. Folgendes Byte muss `FB` sein.
3. Typ und Laengenbyte lesen.
4. Gesamtlaenge als `4 + Laenge` bestimmen.
5. Vollstaendiges Paket sammeln.
6. XOR-Pruefsumme ueber Byte 6 bis zum Byte vor der Pruefsumme bilden.
7. Nur gueltige Pakete weiter auswerten.

Fuer das bestaetigte Hauptpaket `Typ 0x10`, `Laenge 0x15` ergibt sich eine Gesamtlaenge von 25 Bytes. Die sechs 16-Bit-Felder liegen Little Endian ab Byte 12:

**Verifikationshinweis:** Die Implementierung folgt der im PDF textlich angegebenen XOR-Regel (Byte 6 bis zum Byte vor der Pruefsumme). Die im PDF abgedruckte einzelne Beispiel-Bytefolge ergibt bei einer unabhaengigen Nachrechnung nicht denselben Endwert wie das dort gezeigte letzte Byte. Deshalb zeigt die Firmware gueltige/ungueltige Paketzaehler separat an; der Hardwaretest muss bestaetigen, ob die XOR-Grenzen fuer die realen Mitschnitte exakt stimmen. Der Raw-Modus bleibt hiervon unberuehrt.

| Technisches Feld | Byteposition | Behandlung |
|---|---:|---|
| Feld 1 | 12 / 13 | uint16 LE, normiert |
| Feld 2 | 14 / 15 | uint16 LE, normiert |
| Feld 3 | 16 / 17 | uint16 LE, normiert |
| Feld 4 | 18 / 19 | uint16 LE, normiert |
| Feld 5 | 20 / 21 | uint16 LE, normiert |
| Feld 6 | 22 / 23 | uint16 LE, nur Rohwert |
| Checksumme | 24 | XOR |

## Normierung und Kalibrierung

Feld 1 bis Feld 5 werden stueckweise linear auf `-1.0 ... +1.0` normiert. Minimum, Mitte und Maximum sind fuer jedes Feld separat einstellbar und werden in NVS gespeichert. Als Startwerte werden die Messwerte aus der Referenz verwendet:

| Feld | Minimum | Mitte | Maximum |
|---|---:|---:|---:|
| Feld 1 | 1000 | 1503 | 2000 |
| Feld 2 | 1000 | 1486 | 2000 |
| Feld 3 | 1000 | 1493 | 2000 |
| Feld 4 | 1006 | 1511 | 2000 |
| Feld 5 | 1000 | 1498 | 2000 |

Eine einstellbare Totzone wird nach der Normierung angewendet; Standard ist `0.03`. Feld 6 wird bewusst nicht normiert, da die Messunterlage seine Bedeutung noch nicht abschliessend klaert.

## Feld-Aliase

Fuer jedes Feld steht ein Textfeld mit maximal 32 Zeichen bereit. Die Darstellung bleibt immer zweistufig:

- feste technische Kennung: `Feld 1`, `Feld 2`, ... `Feld 6`
- optionaler Benutzer-Alias: z. B. `Roll`, `Pitch`, `Schieber`, `noch unbekannt`

Der Alias ersetzt die technische Kennung nicht. Aliase werden in NVS gespeichert und im Webinterface sowie ueber MQTT ausgegeben.

## Webinterface

Die neue Hauptseite `/uart` enthaelt:

- Moduswahl Aus / Raw / Decoder,
- Auswahl Hardware-UART1 oder UART2,
- RX- und optionalen TX-GPIO,
- Baudrate und Format `8N1`, `8E1`, `8O1`, `8N2`,
- Alias-Textfelder fuer Feld 1 bis Feld 6,
- Min/Mitte/Max-Kalibrierung fuer Feld 1 bis Feld 5,
- einstellbare Totzone,
- Liveanzeige der Rohwerte und normierten Werte,
- Paketzaehler und Checksumme-Statistik,
- HEX- und ASCII-Rohdatenanzeige,
- Funktion zum Leeren des Raw-Ringpuffers.

Die Livewerte werden ueber `/uart/status` als JSON einmal pro Sekunde aktualisiert. Dadurch wird die Konfigurationsseite nicht komplett neu geladen und Eingaben in Formularfeldern bleiben erhalten.

### Neue Web-Routen

| Route | Methode | Funktion |
|---|---|---|
| `/uart` | GET | UART-Konfiguration und Liveanzeige |
| `/uart/status` | GET | JSON-Livestatus fuer Weboberflaeche |
| `/save_uart` | POST | Einstellungen, Aliase und Kalibrierung in NVS speichern; UART neu initialisieren |
| `/uart_clear` | POST | Raw-Ringpuffer und Bytezaehler leeren |

## MQTT-Erweiterung

Der normale Heartbeat veroeffentlicht zusaetzlich den UART-Modus und den empfangenen Bytezaehler. Unter `<Basis>/uart/` werden ausgegeben:

- `mode`
- `status`
- `bytes`
- `packets`
- `valid_packets`
- `invalid_packets`
- `field1/raw` bis `field6/raw`, sobald ein Hauptpaket dekodiert wurde
- `field1/norm` bis `field5/norm`
- `field1/alias` bis `field6/alias` retained

Der Raw-HEX-Datenstrom wird bewusst nicht vollstaendig ueber MQTT gespiegelt, um Broker und Funkverbindung nicht unnoetig zu belasten.

## Persistenz / NVS

UART-Einstellungen werden im separaten Namespace `uartconf` gespeichert. Gespeichert werden Betriebsart, Hardware-UART, RX/TX, Baudrate, Frame, Totzone, sechs Aliase und die Kalibrierwerte fuer Feld 1 bis Feld 5. Fehlende Keys werden ohne NVS-Fehlermeldungen mit sicheren Defaults belegt.

## Elektrische Voraussetzungen

Der ESP32 arbeitet mit 3,3-V-Logik und seine GPIOs sind nicht 5-V-tolerant. Vor dem Anschluss gilt:

- gemeinsame Masse zwischen ESP32 und Zielgeraet herstellen,
- zuerst nur RX anschliessen und passiv sniffen,
- High-Pegel der UART-Leitung mit Multimeter/Oszilloskop pruefen,
- bei 5-V-UART Pegelwandler oder geeigneten Spannungsteiler verwenden,
- TX erst nach geklaerter Richtung und Bus-Topologie verbinden.

## Projektstruktur v0.7

```text
Uart_ESP32/
  .gitignore
  platformio.ini
  README.md
  include/
    arduino_secrets.example.h
    arduino_secrets.h          # lokal, nicht in Git / nicht im Auslieferungs-ZIP
  src/
    ConfigDefaults.h
    BatteryMonitor.cpp/.h
    DeepSleepManager.cpp/.h
    MqttManager.cpp/.h
    OtaManager.cpp/.h
    UartManager.cpp/.h         # neu v0.7
    main.cpp
  docs/
    2026-09-17 - Uart_Esp32 - Projektdokumentation - Firmware - v0.7.*
    2026-09-17 - Uart_Esp32 - Commit-Nachricht - UART-Monitor-und-Decoder - v0.7.txt
```

Hinweis: Der Projektname bleibt `Uart_Esp32`. Der Root-Ordner `Uart_ESP32` wird fuer diese Auslieferung unveraendert aus dem vom Nutzer bereitgestellten Paket uebernommen, entsprechend der Regel zur Beibehaltung des Codepaket-Root-Ordners.

## Geaenderte / neue Dateien in v0.7

- `README.md`
- `include/arduino_secrets.example.h`
- `src/ConfigDefaults.h`
- `src/main.cpp`
- `src/UartManager.h` (neu)
- `src/UartManager.cpp` (neu)
- `docs/2026-09-17 - Uart_Esp32 - Projektdokumentation - Firmware - v0.7.md` (neu)
- `docs/2026-09-17 - Uart_Esp32 - Projektdokumentation - Firmware - v0.7.docx` (neu)
- `docs/2026-09-17 - Uart_Esp32 - Projektdokumentation - Firmware - v0.7.pdf` (neu)
- `docs/2026-09-17 - Uart_Esp32 - Commit-Nachricht - UART-Monitor-und-Decoder - v0.7.txt` (neu)

## Pruefung und offene Verifikation

Die Aenderungen wurden statisch auf konsistente Einbindung, NVS-Key-Laengen, Parsergrenzen, Pinvalidierung und die Dokumentationsstruktur geprueft. Eine echte PlatformIO-Kompilierung mit dem ESP32-Arduino-Framework ist in der Erstellungsumgebung nicht verfuegbar. Verbindlicher lokaler Test:

```bash
pio run -e uart_esp32_usb -t clean
pio run -e uart_esp32_usb
pio run -e uart_esp32_usb -t upload
```

Danach sollte `/uart` zunaechst im Raw-Modus getestet werden. Fuer die erste Hardwaremessung TX auf `-1` belassen und nur RX sowie gemeinsame Masse verbinden. Erst nach bestaetigter 3,3-V-Pegellage den Decoder-Modus mit 115200 8N1 aktivieren.

## Auslieferungsform

Bereitgestellt werden ein vollstaendiges ZIP mit unveraendertem Root-Ordner `Uart_ESP32`, ein ZIP mit nur geaenderten/neuen Dateien in derselben Struktur, eine Patch-Datei, eine englische Commit-Nachricht mit Datum und vollstaendiger Dateiliste sowie die Projektdokumentation als PDF und bearbeitbare DOCX mit identischem Stand v0.7.
