# Uart_Esp32 - Projektdokumentation - Firmware

**Projektname:** Uart_Esp32  
**Dokumentenart:** Projektdokumentation  
**Thema:** Firmware fuer ESP32-WROOM-32 / NodeMCU-32S - UART Monitor, getrennte Einstellungen und Decoder-Dauerbetrieb  
**Erstellungsdatum:** 2026-09-17  
**Autor / verantwortliche Person:** OpenAI ChatGPT (technische Erstellung)  
**Version:** v0.8  
**Status:** Entwurf / lokale Build- und Hardwarepruefung ausstehend  
**Aenderungsdatum:** 2026-09-17  
**Ablageort:** Projektordner `Uart_ESP32/docs`  
**Referenzen:** `Regeln_Projektdokumentation_PDF_DOCX_Pflicht_(5).pdf`; `ESP32_UART_Protokoll_Entschluesselung(1).pdf`; Projektstand v0.7

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
| v0.8 | 2026-09-17 | OpenAI ChatGPT | Entwurf | UART-Konfiguration und Live-Monitor getrennt; Raw/Decoder als persistenter Dauerbetrieb dokumentiert |

## Ziel des Standes v0.8

Der Projektstand v0.8 trennt die UART-Bedienung in eine reine Live-Monitorseite und eine explizite Einstellungsseite. Die eigentliche UART-Verarbeitung bleibt davon unabhaengig: Raw-/Sniffer-Modus und Protokoll-Decoder laufen nach Aktivierung dauerhaft in der ESP32-Hauptschleife weiter, auch wenn keine Browserseite geoeffnet ist. Der gewaehlte Modus wird in NVS gespeichert und nach einem normalen Neustart automatisch wieder gestartet.

Im Raw-/Sniffer-Modus werden empfangene UART-Bytes ohne Interpretation erfasst und als HEX und ASCII dargestellt. Im Protokoll-Decoder-Modus wird zusaetzlich das in der Referenzdokumentation beschriebene UART-Protokoll ausgewertet.

Die technische Feldkennung `Feld 1` bis `Feld 6` bleibt immer sichtbar. Zu jedem Feld kann ein frei editierbarer Alias gespeichert werden, z. B. `Feld 1 - Roll`. Dadurch koennen spaeter bestaetigte Bedeutungen eingetragen werden, ohne die neutrale Feldnummerierung zu verlieren.

## UART-Betriebsarten

| Modus | Verhalten |
|---|---|
| Aus | Zusaetzliche Hardware-UART ist deaktiviert. |
| Raw / Sniffer | Bytes werden empfangen und in einem 512-Byte-Ringpuffer gespeichert. Die letzten 256 Bytes werden live als HEX und ASCII angezeigt. |
| Protokoll-Decoder | Raw-Erfassung bleibt aktiv; zusaetzlich werden Pakete synchronisiert, geprueft und die bestaetigten Hauptpakete dekodiert. |

UART0 bleibt fuer den seriellen Debug-Monitor reserviert. Fuer die Zusatzfunktion sind Hardware-UART1 und UART2 waehlbar. RX und TX sind frei konfigurierbar; TX kann mit `-1` deaktiviert werden. Standard ist RX GPIO16, TX deaktiviert, 115200 Baud, 8N1.

**Dauerbetrieb:** Die UART-Verarbeitung wird in `loop()` kontinuierlich aufgerufen und besitzt keine Messanzahl oder Laufzeitbegrenzung. Der 512-Byte-Ringpuffer kann dabei nicht vollaufen: Sobald er gefuellt ist, werden nur die jeweils aeltesten Rohbytes ueberschrieben. Die Dekodierung selbst laeuft weiter. Das Oeffnen oder Schliessen der Weboberflaeche beeinflusst den Decoder nicht.

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

Ab v0.8 sind Konfiguration und Beobachtung bewusst getrennt. Dadurch bleibt die Live-Seite uebersichtlich, waehrend die umfangreichen Einstellungen auf einer eigenen Seite liegen.

### UART Monitor `/uart`

Die Monitorseite enthaelt nur Laufzeitinformationen:

- aktuellen UART-/Decoder-Status,
- fortlaufenden Bytezaehler,
- Feld 1 bis Feld 6 mit technischer Kennung, Alias, Rohwert und - fuer Feld 1 bis 5 - normiertem Wert,
- Paketzaehler fuer gesamt, gueltig, ungueltig und `0x10/0x15`,
- die letzten 256 Rohbytes als HEX und ASCII,
- Funktion zum Leeren des Raw-Ringpuffers und Bytezaehlers,
- direkten Link zur UART-Einstellungsseite.

Die Livewerte werden ueber `/uart/status` als JSON einmal pro Sekunde aktualisiert. Das ist nur die Darstellung; die Dekodierung laeuft unabhaengig davon permanent im Hintergrund.

### UART Einstellungen `/uart/settings`

Die separate Einstellungsseite enthaelt:

- Modus `Aus`, `Raw / Sniffer (Dauerbetrieb)` oder `Protokoll-Decoder (Dauerbetrieb)`,
- Auswahl Hardware-UART1 oder UART2,
- RX- und optionalen TX-GPIO,
- Baudrate und Format `8N1`, `8E1`, `8O1`, `8N2`,
- Alias-Textfelder fuer Feld 1 bis Feld 6,
- Min/Mitte/Max-Kalibrierung fuer Feld 1 bis Feld 5,
- einstellbare Totzone,
- Anzeige des aktuellen gespeicherten Modus und UART-Status.

Beim Speichern werden die Werte in NVS abgelegt und nur die UART-Schnittstelle neu initialisiert. Ein gespeicherter Raw- oder Decoder-Modus wird beim naechsten ESP32-Start automatisch wieder aktiviert.

### Web-Routen

| Route | Methode | Funktion |
|---|---|---|
| `/uart` | GET | Reine UART-Liveansicht / Decoder-Monitor |
| `/uart/settings` | GET | Explizite UART-Konfiguration, Feld-Aliase und Kalibrierung |
| `/uart/status` | GET | JSON-Livestatus fuer die Monitorseite |
| `/save_uart` | POST | Einstellungen, Aliase und Kalibrierung in NVS speichern; UART neu initialisieren |
| `/uart_clear` | POST | Raw-Ringpuffer und Bytezaehler leeren; Decoderbetrieb bleibt aktiv |

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

## Projektstruktur v0.8

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
    2026-09-17 - Uart_Esp32 - Projektdokumentation - Firmware - v0.8.*
    2026-09-17 - Uart_Esp32 - Commit-Nachricht - UART-Seitentrennung-und-Dauerbetrieb - v0.8.txt
```

Hinweis: Der Projektname bleibt `Uart_Esp32`. Der Root-Ordner `Uart_ESP32` wird fuer diese Auslieferung unveraendert aus dem vom Nutzer bereitgestellten Paket uebernommen, entsprechend der Regel zur Beibehaltung des Codepaket-Root-Ordners.

## Geaenderte / neue Dateien in v0.8

- `README.md`
- `src/ConfigDefaults.h`
- `src/main.cpp`
- `docs/2026-09-17 - Uart_Esp32 - Projektdokumentation - Firmware - v0.8.md` (neu)
- `docs/2026-09-17 - Uart_Esp32 - Projektdokumentation - Firmware - v0.8.docx` (neu)
- `docs/2026-09-17 - Uart_Esp32 - Projektdokumentation - Firmware - v0.8.pdf` (neu)
- `docs/2026-09-17 - Uart_Esp32 - Commit-Nachricht - UART-Seitentrennung-und-Dauerbetrieb - v0.8.txt` (neu)

## Pruefung und offene Verifikation

Die Aenderungen wurden statisch auf konsistente Einbindung, NVS-Key-Laengen, Parsergrenzen, Pinvalidierung und die Dokumentationsstruktur geprueft. Eine echte PlatformIO-Kompilierung mit dem ESP32-Arduino-Framework ist in der Erstellungsumgebung nicht verfuegbar. Verbindlicher lokaler Test:

```bash
pio run -e uart_esp32_usb -t clean
pio run -e uart_esp32_usb
pio run -e uart_esp32_usb -t upload
```

Danach sollte auf `/uart/settings` zunaechst `Raw / Sniffer (Dauerbetrieb)` eingestellt werden. Auf `/uart` kann anschliessend beobachtet werden, ob die Bytezahl kontinuierlich steigt. Fuer die erste Hardwaremessung TX auf `-1` belassen und nur RX sowie gemeinsame Masse verbinden. Erst nach bestaetigter 3,3-V-Pegellage den `Protokoll-Decoder (Dauerbetrieb)` mit 115200 8N1 aktivieren. Die Monitorseite darf geschlossen werden; der Decoder muss weiterhin laufen.

## Auslieferungsform

Bereitgestellt werden ein vollstaendiges ZIP mit unveraendertem Root-Ordner `Uart_ESP32`, ein ZIP mit nur geaenderten/neuen Dateien in derselben Struktur, eine Patch-Datei, eine englische Commit-Nachricht mit Datum und vollstaendiger Dateiliste sowie die Projektdokumentation als PDF und bearbeitbare DOCX mit identischem Stand v0.8.
