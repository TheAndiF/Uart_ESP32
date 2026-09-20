# Uart_Esp32 - Projektdokumentation - Firmware

## BX3-Konsolenmodus und bestehende UART-Funktionen | Version v0.14

**Projektname:** Uart_Esp32  
**Dokumentenart:** Projektdokumentation  
**Thema:** Firmware für ESP32-WROOM-32 / NodeMCU-32S - BX3-Konsole, UART-Monitor, Decoder und Probe-Runner  
**Erstellungsdatum:** 2026-09-20  
**Autor / verantwortliche Person:** OpenAI ChatGPT (technische Bearbeitung)  
**Version:** v0.14  
**Status:** Entwurf - lokale C++17-Syntaxprüfung erfolgreich; PlatformIO-Build und Hardwaretest ausstehend  
**Änderungsdatum:** 2026-09-20  
**Ablageort:** Projektordner `Uart_ESP32/docs`  
**Referenzen:** `BX3_ESP32_Login_Leitfaden.pdf`; `Regeln_Projektdokumentation_PDF_DOCX_Pflicht_.pdf`; Projektstand v0.13

## Änderungshistorie

| Version | Datum | Bearbeiter | Status | Änderung |
|---|---|---|---|---|
| v0.12 | 2026-09-17 | OpenAI ChatGPT | Entwurf | Decoder an st10-Outer-/Inner-Frame, Commands und XOR angepasst. |
| v0.13 | 2026-09-20 | OpenAI ChatGPT | Entwurf | Automatischen Probe-Runner für 1000 Testkandidaten ergänzt. |
| v0.14 | 2026-09-20 | OpenAI ChatGPT | Entwurf | Bidirektionalen BX3-Konsolenmodus mit Webterminal, inkrementellem RX-Puffer und TX-Eingabe ergänzt. |

## 1. Ziel des Stands v0.14

Ziel dieses Stands ist ein zusätzlicher UART-Betriebsmodus, der die im BX3-Konsolenleitfaden beschriebene Linux-Konsole nutzbar macht. Der ESP32 übernimmt dabei keine Anmeldung und enthält keine unbekannten Zugangsdaten. Er stellt ausschließlich den seriellen Transport zwischen Bedienoberfläche und BX3 bereit.

Der Referenzleitfaden belegt für die BX3-Konsole lesbaren Linux-Boottext, den Text `Welcome to BX3` und den Prompt `bx3 login:`. Als Arbeitsparameter sind 115200 Baud und 8N1 angegeben. Gleichzeitig ist dort festgehalten, dass der physische BX3-Console-RX-Punkt noch sicher bestimmt werden muss und dass vor aktivem TX die Pegel zu prüfen sind.

Der bestehende Projektstand v0.13 bleibt erhalten. Raw/Sniffer, st10-Protokoll-Decoder, Feld-5-TX-Replay und automatischer Probe-Runner werden nicht entfernt. Der Konsolenmodus wird als vierte Betriebsart ergänzt.

## 2. Ausgangsbasis und Hardwarebezug

Das aktuelle Projekt ist für einen klassischen ESP32-WROOM-32 / NodeMCU-32S konfiguriert (`board = nodemcu-32s`). Der BX3-Leitfaden beschreibt einen ESP32-S3, die serielle Grundidee ist jedoch auf den vorhandenen ESP32 übertragbar: UART1 oder UART2 wird für die BX3-Leitung verwendet, während UART0 für den lokalen USB-/Debug-Anschluss reserviert bleibt.

Für den Konsolenzugriff gelten die elektrischen Regeln aus dem Referenzleitfaden:

- BX3 Console-TX wird mit einem ESP32-RX verbunden.
- BX3 Console-RX wird erst nach sicherer Identifikation mit einem ESP32-TX verbunden.
- In der TX-Leitung wird während der Erprobung ein Serienwiderstand von etwa 470 Ohm bis 1 kOhm empfohlen.
- Zwischen BX3 und ESP32 wird eine gemeinsame Masse benötigt.
- BX3-VCC wird nicht mit 3,3 V oder 5 V des ESP32 verbunden.
- Der tatsächliche High-Pegel der UART-Leitung wird vor aktivem TX gemessen.
- Für einen reinen Lesetest kann der TX-GPIO im Projekt weiterhin auf `-1` stehen.

Die Firmware sperrt weiterhin GPIO6..11 wegen des Flashs und GPIO1/3 wegen UART0. GPIO34..39 dürfen nur als RX verwendet werden.

## 3. Firmwarearchitektur des neuen Modus

Der Enum `UartManager::Mode` wurde um `Console = 3` erweitert. Die Einstellung wird wie die anderen UART-Modi in NVS gespeichert und nach einem Neustart wieder geladen.

Vereinfachter Datenfluss:

```text
Browser /uart/console                       Lokales USB-Terminal
        |                                             |
        | HTTP Eingabe / Live-Polling                 | UART0 Eingabe
        v                                             v
+-----------------------------------------------------------+
| ESP32-WROOM-32                                            |
| UartManager - Mode::Console                               |
| - Hardware UART1 oder UART2                               |
| - 2048-Byte Konsolen-Ringpuffer                           |
| - Web-TX und optionaler USB/UART0-Passthrough             |
+-----------------------------------------------------------+
        ^                                             |
        | RX                                          | TX
        |                                             v
                BX3 Console TX / RX
```

### 3.1 Empfangsrichtung BX3 -> ESP32

Jedes empfangene UART-Byte wird weiterhin in den allgemeinen Raw-Ringpuffer geschrieben und im Konsolenmodus zusätzlich in einen separaten 2048-Byte-Konsolenpuffer übernommen. Eine fortlaufende Sequenznummer kennzeichnet den Datenstand.

Die Weboberfläche fragt über `/uart/console/data?since=<Sequenz>` nur die seit dem letzten Poll neu hinzugekommenen Bytes ab. Dadurch wird nicht bei jedem Update der komplette Puffer übertragen. Ist der Browser länger inaktiv und ältere Daten wurden bereits überschrieben, meldet die API den Zustand `truncated` und liefert den noch vorhandenen Teil des Ringpuffers.

Im Konsolenmodus werden die empfangenen BX3-Bytes zusätzlich auf `Serial` gespiegelt. Damit ist auch ein lokaler USB/UART0-Zugriff möglich. Die bestehende Firmware schreibt jedoch weiterhin Diagnosemeldungen auf `Serial`; die Webkonsole ist deshalb die sauberere Darstellung der reinen BX3-Ausgabe.

### 3.2 Senderichtung ESP32 -> BX3

TX ist nur freigegeben, wenn alle folgenden Bedingungen erfüllt sind:

1. Der UART läuft.
2. Der aktive Modus ist `BX3 Konsole`.
3. Ein TX-GPIO größer oder gleich 0 ist konfiguriert.
4. Der TX-GPIO ist gemäß der bestehenden Pinprüfung zulässig.

Die Weboberfläche sendet Text über `/uart/console/send`. Es wird keine versteckte automatische CR/LF-Umsetzung durchgeführt. Der Benutzer wählt ausdrücklich einen der Abschlüsse `CR`, `LF`, `CRLF` oder `keiner`.

Zusätzlich stehen die Steuerzeichen Ctrl+C, Ctrl+D und TAB über `/uart/console/control` zur Verfügung. Die lokale USB/UART0-Eingabe wird im Konsolenmodus byteweise unverändert zum BX3-UART weitergereicht.

## 4. Weboberfläche `/uart/console`

Die Startseite enthält ab v0.14 einen eigenen Button **BX3 Konsole**. Die Konsolenseite zeigt:

- aktuell gewählten Modus,
- Laufzustand START/STOP,
- Hardware-UART,
- RX- und TX-GPIO,
- Baudrate und UART-Format,
- empfangene RX-Bytes,
- gesendete TX-Bytes,
- TX-Bereitschaft,
- laufende Terminalausgabe,
- Eingabefeld für Login oder Shell-Befehle,
- optional verdeckte Eingabe für Passwörter,
- auswählbaren Zeilenabschluss,
- Tasten für Enter, Ctrl+C, Ctrl+D und TAB,
- Funktion zum Leeren des Konsolenpuffers.

Das Eingabefeld wird nach erfolgreichem Senden geleert. Eingaben werden nicht in NVS geschrieben und nicht als Konsolen-Historie gespeichert. Ob das Zielsystem Zeichen selbst zurückechoed, hängt vom BX3/Linux-Login ab. Passwörter werden bei Linux typischerweise nicht sichtbar zurückgegeben.

### 4.1 Neue Webrouten

| Route | Methode | Funktion |
|---|---|---|
| `/uart/console` | GET | Webterminal für die BX3-Konsole. |
| `/uart/console/data` | GET | Inkrementelle RX-Daten als JSON anhand einer Sequenznummer. |
| `/uart/console/send` | POST | Sendet bis zu 512 Bytes Konsolentext an den Ziel-UART. |
| `/uart/console/control` | POST | Sendet erlaubte Steuerzeichen, aktuell Ctrl+C, Ctrl+D, TAB und ESC. |
| `/uart/console/clear` | POST | Leert den serverseitigen Konsolen-Ringpuffer. |

Die spezifischen Konsolenrouten werden vor der kürzeren Route `/uart` registriert. Damit wird das bereits aus früheren Projektständen bekannte Routingproblem von ESPAsyncWebServer 3.x vermieden.

## 5. UART-Einstellungen für den ersten BX3-Test

Für den ersten Test ist folgende Reihenfolge vorgesehen:

1. In `/uart/settings` den Modus **BX3 Konsole** wählen.
2. Hardware-UART 1 oder 2 wählen.
3. Nur den bekannten BX3 Console-TX mit dem gewählten ESP32-RX und GND verbinden.
4. TX zunächst auf `-1` setzen.
5. Baudrate `115200` und Format `8N1` einstellen.
6. Speichern und `/uart/console` öffnen.
7. Prüfen, ob beim BX3-Start lesbarer Boottext, `Welcome to BX3` und `bx3 login:` erscheinen.
8. Erst danach den physischen BX3 Console-RX sicher bestimmen und den Pegel messen.
9. ESP32-TX über den vorgesehenen Schutzwiderstand anschließen und den TX-GPIO konfigurieren.
10. Auf der Konsolenseite zunächst nur Enter mit `CR` senden und die Reaktion prüfen.
11. Anschließend ausschließlich autorisierte, bekannte Zugangsdaten verwenden.

Der Firmwaremodus ersetzt keine Zugangsdaten und enthält keine Brute-Force-Funktion.

## 6. Zusammenspiel mit bestehenden UART-Funktionen

### Raw / Sniffer

Der Raw-Modus bleibt unverändert. Er erfasst empfangene Bytes und zeigt sie als HEX und ASCII an. Er sendet nichts aktiv an das Zielsystem.

### Protokoll-Decoder

Der st10-Decoder bleibt unverändert für die bekannte Telemetrie. Er synchronisiert auf `FF FB`, prüft Inner-Frame, Längenbeziehung und XOR und zählt die bekannten Commands `0x0021`, `0x0023`, `0x0031` und `0x0033`.

### Feld-5-TX-Replay

Der experimentelle Feld-5-TX-Replay bleibt ausschließlich an den Decoder-Modus gebunden. Im Konsolenmodus ist diese Funktion nicht aktiv.

### Probe-Runner v0.13

Der automatische 1000-Kandidaten-Sweep bleibt ebenfalls ausschließlich im Decoder-Modus nutzbar. Startet der Benutzer den Konsolenmodus, kann der Probe-Runner nicht parallel aktiv sein. Damit werden Konsoleneingaben nicht mit den experimentellen Kandidatentelegrammen vermischt.

## 7. Sicherheit und Zugriffsschutz

Die Weboberfläche des Projekts arbeitet aktuell mit normalem HTTP und hat keine eigene Benutzeranmeldung. Das ist für einen seriellen Konsolenzugang besonders relevant, weil über die Konsole gegebenenfalls Anmeldedaten und Shell-Befehle übertragen werden.

Daraus folgen für v0.14 diese Betriebsregeln:

- Webkonsole nur in einem vertrauenswürdigen, kontrollierten Netz verwenden.
- Keine Portweiterleitung der Konsolenrouten ins öffentliche Internet einrichten.
- Zugangsdaten nicht im Firmwarequelltext speichern.
- Passwort-Eingaben können im Browser verdeckt werden; die Transportstrecke HTTP ist dadurch jedoch nicht verschlüsselt.
- Nach dem Test kann TX wieder auf `-1` gesetzt oder der UART gestoppt werden.
- Für produktive Fernzugriffe wäre eine spätere Authentifizierungs-/TLS-Lösung als eigener Projektstand sinnvoll.

## 8. Fehlerdiagnose

| Symptom | Wahrscheinliche Ursache | Prüfung / Maßnahme |
|---|---|---|
| Nur Zeichensalat | Baudrate, Format, Masse oder Messpunkt falsch | 115200/8N1, GND und bekannten Console-TX prüfen. |
| Boottext sichtbar, Eingabe ohne Wirkung | BX3 Console-RX nicht verbunden oder falscher TX-Punkt | TX wieder trennen, RX-Kandidat passiv bestimmen, Pegel messen, dann über Schutzwiderstand testen. |
| `TX bereit: nein` in der Webkonsole | TX-GPIO `-1`, UART gestoppt oder anderer Modus aktiv | Einstellungen und START-Zustand prüfen. |
| Nach Benutzername keine sichtbare Passworteingabe | Normales Linux-Verhalten | Passwort eingeben und Enter senden; kein Echo ist erwartbar. |
| Browser zeigt eine Pufferwarnung | Mehr als 2048 Bytes seit dem letzten erfolgreichen Poll angefallen | Ausgabe ist weiterhin nutzbar; ältere Ringpufferdaten wurden überschrieben. |
| USB-Terminal enthält zusätzliche Zeilen | Firmwarediagnosen werden ebenfalls über UART0 ausgegeben | Für eine saubere Zielansicht die Webkonsole verwenden. |
| Webkonsole kann nicht senden | HTTP-Anfrage erreicht ESP32, aber Konsolen-TX ist nicht bereit | Modus `BX3 Konsole`, gültigen TX-GPIO und laufenden UART prüfen. |

## 9. Geänderte Dateien v0.14

- `src/UartManager.h`
- `src/UartManager.cpp`
- `src/main.cpp`
- `src/ConfigDefaults.h`
- `README.md`
- `VALIDATION_v0.14.txt`
- `docs/2026-09-20 - Uart_Esp32 - Projektdokumentation - Firmware - v0.14.md`
- `docs/2026-09-20 - Uart_Esp32 - Projektdokumentation - Firmware - v0.14.docx`
- `docs/2026-09-20 - Uart_Esp32 - Projektdokumentation - Firmware - v0.14.pdf`
- `docs/2026-09-20 - Uart_Esp32 - Commit-Nachricht - BX3-Konsole - v0.14.txt`

## 10. Validierung und offene Punkte

Die geänderten Kernquellen wurden lokal mit `g++ -std=c++17 -fsyntax-only` gegen Arduino-/ESP32-API-Stubs geprüft. Dabei wurden sowohl `UartManager.cpp` zusammen mit `TestCandidateCatalog.cpp` als auch `main.cpp` ohne Syntaxfehler geprüft.

Zusätzlich wurde die neue Konsolen-Ringpuffer-/JSON-Funktion mit einem kleinen lokalen Testprogramm geprüft. CR/LF, Anführungszeichen, Backslash und ESC wurden als gültiges JSON serialisiert und anschließend wieder korrekt geparst. Sequenzabfragen und das Leeren des Ringpuffers wurden ebenfalls getestet.

Ein vollständiger PlatformIO-Build der Version v0.14 war in der Ausführungsumgebung nicht möglich, weil PlatformIO und die ESP32-Toolchain nicht installiert sind. Vor dem Flashen muss deshalb lokal mindestens folgender Build ausgeführt werden:

```text
pio run -e uart_esp32_usb
```

Offen bleibt außerdem der Hardwaretest am BX3. Insbesondere müssen der tatsächliche Console-RX-Punkt des BX3, der Logikpegel und die Reaktion auf einen einzelnen Enter-Tastendruck verifiziert werden. Die im Referenzleitfaden fehlenden Zugangsdaten werden durch diese Firmware nicht ersetzt.

## 11. Kurzcheckliste vor dem ersten Login-Test

- [ ] BX3-Zugriff ist autorisiert.
- [ ] Gemeinsame Masse ist eindeutig verbunden.
- [ ] BX3 Console-TX ist am gewählten ESP32-RX lesbar.
- [ ] High-Pegel wurde gemessen und ist ESP32-kompatibel.
- [ ] UART steht auf 115200 / 8N1 oder auf nachgemessenen korrekten Werten.
- [ ] Für den ersten Lesetest steht TX auf `-1`.
- [ ] `Welcome to BX3` und `bx3 login:` erscheinen in `/uart/console`.
- [ ] BX3 Console-RX ist vor aktivem TX sicher identifiziert.
- [ ] ESP32-TX ist beim Erproben über 470 Ohm bis 1 kOhm angeschlossen.
- [ ] Zuerst wird nur Enter/CR getestet.
- [ ] Zugangsdaten stammen aus einer legitimen Quelle.
- [ ] Webkonsole wird nur in einem vertrauenswürdigen Netz verwendet.
