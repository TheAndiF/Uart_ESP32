# Uart_Esp32 - WROOM32 Infrastructure

Reduzierte Firmware für einen klassischen **ESP32-WROOM-32 / NodeMCU-32S**.

## Stand v0.21

**Neu v0.21:** Die UART-Image-Transfer-Seite ueberschreibt das Quellenfeld nicht mehr bei jeder 400-ms-Statusabfrage. Der Browser markiert das Feld als lokal geaendert, sobald es fokussiert oder bearbeitet wird. Statusdaten aktualisieren weiterhin die getrennte Anzeige "Quelle", aber nicht mehr das Eingabefeld, solange der Benutzer dort z. B. `/tmp/mtd7.img.gz` eintraegt. Erst beim Start oder Neustart wird der aktuell eingetragene Wert uebernommen und getrimmt. Dadurch springt die Anzeige nicht mehr auf den letzten Firmware-Statuswert wie `/dev/mtd7ro` zurueck.

Die v0.20-Funktion fuer Snapshot-Dateien bleibt unveraendert erhalten; WLAN, UART-Konsole, Decoder, Probe-Runner, OTA, MQTT, Batterie und Deep-Sleep wurden nicht funktional geaendert. Zusaetzlich wurden versehentlich mitgelieferte Patch-Reject-Artefakte (`*.rej`) aus dem Projektpaket entfernt.

## Stand v0.20


**Neu v0.20:** Der UART Image-Transfer akzeptiert jetzt neben read-only MTD-Quellen auch vorbereitete Snapshot-Dateien aus `/tmp` und `/var/tmp`. Erlaubt sind bewusst nur Dateien mit den Endungen `.img` und `.img.gz`, z. B. `/tmp/mtd7.img` oder `/tmp/mtd7.img.gz`. Dadurch kann zuerst in der BX3-Konsole ein stabiler Snapshot erzeugt werden und danach genau diese unveraenderliche Datei ueber die Image-Transfer-Seite heruntergeladen werden. Das ist wichtig, wenn sich `/dev/mtd7ro` im laufenden System veraendert und deshalb die Gesamt-SHA256-Pruefung eines Live-MTD-Transfers fehlschlaegt.

Bei Snapshot-Dateien wird die Groesse ueber `wc -c` ermittelt und anschließend derselbe blockweise Transferpfad wie bei MTD-Quellen verwendet: `bx3img_block N`, `dd`, `cksum`, `base64`, CRC32 pro Block und Gesamt-SHA256 der Quelldatei. Der Browserdownload bekommt bei Datei-Quellen automatisch den Dateinamen der Quelle, z. B. `mtd7.img.gz`.

**Neu v0.19:** Der UART Image-Transfer wurde fuer weniger CPU-/Shell-Overhead und robusteren Durchsatz optimiert. Pro 32-KiB-Block wird jetzt **POSIX `cksum` CRC32** statt SHA-256 verwendet; die **Gesamt-SHA256-Pruefung des vollstaendigen Images bleibt erhalten**. Der ESP32 vergleicht dazu seinen eigenen POSIX-CRC32 mit dem vom BX3 gelieferten `cksum`-Wert. Voraussetzung auf dem BX3 ist damit zusaetzlich das Kommando `cksum`; fehlt es, bricht der Transfer kontrolliert ab und gibt die Konsole wieder frei.

Der Hardware-UART-RX-Puffer wird vor `HardwareSerial.begin()` auf **16 KiB** vergroessert. Waehrend eines aktiven Image-Transfers verarbeitet die Hauptschleife pro Durchlauf bis zu **8192 RX-Bytes** statt 512. Dadurch koennen Base64-Bursts schneller geleert werden und der Webserver hat mehr Pufferreserve. Auf dem BX3 wird einmalig die Shell-Funktion `bx3img_block` installiert. Danach sendet der ESP32 pro Block nur noch `bx3img_block N`; `cksum` liefert dabei CRC32 und Laenge in einem Aufruf und ersetzt die bisherige Kombination aus `wc` + Block-SHA256.

Der Browserblock wird nicht mehr als HTTP-Chunked-Response ausgegeben, sondern mit bekannter **Content-Length** direkt aus dem bereits validierten ESP32-Blockpuffer gestreamt. Dadurch entfaellt HTTP-Chunked-Framing fuer die 32-KiB-Bloecke. Die Synchronisierung, idempotenten ACKs und die Konsolenfreigabe nach einer Unterbrechung aus v0.18 bleiben unveraendert erhalten.


**Neu v0.18:** Der UART Image-Transfer synchronisiert Browser und ESP32 jetzt explizit ueber getrennte Zustandswerte fuer **Empfangsblock**, **browserbereiten Block** und **letztes ACK**. Ein zu frueher HTTP-Abruf liefert `202 Accepted` statt eines sichtbaren Fehlers. ACKs sind blocknummerngebunden und idempotent, damit ein verlorener HTTP-ACK keine doppelten Browserbloecke oder einen unbeabsichtigten Blocksprung verursacht. Fehler werden zusaetzlich nach Timeout, SHA, Base64, Marker, Groesse/Puffer und Sonstige aufgeschluesselt.

Nach Abbruch oder nicht wiederherstellbarem Fehler wird die UART-Konsole automatisch wieder freigegeben. Ein Image-Neustart ist danach **bewusst gesperrt**, bis der Benutzer die UART-Konsole oeffnet, den BX3-Shell-Zugriff wiederherstellt und dies dort mit **Konsolenzugriff hergestellt - Image-Neustart freigeben** bestaetigt. Erst danach wird auf der Image-Seite **Transfer komplett neu starten (Block 0)** aktiv. Der Neustart beginnt absichtlich bei Block 0, weil beim Wechsel zur Konsole die bereits im Browser gesammelten Teile eines Vollimages nicht verlaesslich erhalten bleiben.

**Neu v0.17:** Die UART-Konsole ist jetzt kopierfreundlich: `user-select:text`, Anzeige-Pause bei weiterlaufendem UART-Empfang, Schutz einer aktiven Textmarkierung vor Live-Neuzeichnen sowie Schaltflaechen fuer markierten Text und die gesamte Ansicht. Zusaetzlich gibt es das Hauptmenue **UART Image-Transfer**. Es liest ein read-only MTD-Device (z. B. `/dev/mtd7ro`) blockweise mit 32 KiB ueber die BX3-Shell (`dd`, `base64`, `sha256sum`), prueft Groesse und SHA-256 pro Block, wiederholt fehlerhafte Bloecke bis zu fuenfmal und vergleicht bei einem Volltransfer ab Block 0 den Gesamt-SHA256. Der Browser sammelt nur bereits validierte Bloecke und stellt am Ende die Image-Datei zum Download bereit; der ESP32 haelt nie das gesamte Image im RAM. Waehrend des Transfers ist UART TX/RX exklusiv fuer den Image-Parser reserviert.


**Neu v0.16:** Die universelle **UART Konsole** besitzt jetzt ein Häkchenfeld **Autoscroll bei neuen UART-Daten**. Autoscroll ist beim ersten Aufruf standardmaessig aktiviert. Ist das Häkchen gesetzt, springt das Terminal nach jeder Aktualisierung automatisch ans Ende. Wird es deaktiviert, bleibt die aktuelle Scrollposition stehen, waehrend neue UART-Daten weiterhin empfangen und in den Browserpuffer uebernommen werden. Die Einstellung ist reine Browserdarstellung und wird per `localStorage` unter `uartConsoleAutoscroll` gespeichert; sie veraendert weder UART- noch NVS-Einstellungen auf dem ESP32. Beim erneuten Aktivieren wird sofort zum Ende des Terminals gescrollt.


**Neu v0.15:** Die UART-Architektur wurde von exklusiven Betriebsarten auf einen gemeinsamen UART-Basisbetrieb umgestellt. Der physische RX-Datenstrom wird jetzt gleichzeitig in den Rohdatenpuffer, den universellen UART-Konsolenpuffer und den st10-Decoder eingespeist. Dadurch bleiben Konsolendaten sichtbar, waehrend der Decoder aktiv ist. Die Weboberflaeche besitzt vier getrennte Hauptbereiche: **UART Einstellungen**, **UART Konsole**, **UART Decoder** und **UART Probe-Runner**.

TX ist jetzt unabhaengig vom RX-Betrieb freigebbar. Ist TX gesperrt, startet `HardwareSerial` mit `TX=-1`, sodass der konfigurierte TX-GPIO nicht vom UART-Peripherieblock getrieben wird. Beim Upgrade von v0.14 oder aelter wird TX sicherheitshalber einmalig gesperrt und muss unter UART Einstellungen bewusst aktiviert werden. Probe-Runner und Feld-5-Replay reservieren TX; waehrenddessen kann die Webkonsole nicht dazwischen senden.

Die UART Konsole wurde auf einen 8192-Byte-Ringpuffer erweitert und kann RX als **Text**, **HEX** oder **HEX + ASCII** darstellen. Fuer TX stehen Text/ASCII sowie frei eingebbare HEX-Bytes zur Verfuegung; CR, LF, CRLF, Ctrl+C, Ctrl+D, TAB und ESC werden weiterhin unterstuetzt. Der Probe-Runner hat eine eigene Seite. Ausserdem wurde ein Fehler im Kandidatenfortschritt behoben, durch den der Runner zuvor nach jedem Test zwei Katalogpositionen weiterzaehlen konnte. WLAN, MQTT, OTA, Batterie, Deep Sleep und die sonstigen Infrastrukturfunktionen bleiben unveraendert erhalten.


### Historische Änderungshinweise

Die folgenden Abschnitte dokumentieren frühere Zwischenstände. Wo sie der v0.21-Architektur widersprechen, gilt die Beschreibung von v0.21 weiter oben.

**Neu v0.14:** Zusaetzlich zu Raw/Sniffer und Protokoll-Decoder gibt es den Modus **BX3 Konsole**. Er setzt die im beigefuegten BX3/ESP32-Leitfaden beschriebene bidirektionale UART-Verbindung fuer Boottext, Login und Shell-Eingaben um. Die Weboberflaeche `/uart/console` zeigt einen laufenden Terminalpuffer, kann Text mit bewusst waehlbarem Zeilenabschluss (CR, LF, CRLF oder keiner) senden und stellt Enter, Ctrl+C, Ctrl+D und TAB als eigene Aktionen bereit. Eine Passwort-Eingabe kann im Browser verdeckt werden; eingegebene Zeichen werden nicht persistent gespeichert. Der Modus verwendet die normalen UART-Einstellungen und ist damit auf 115200/8N1 einstellbar, wie es der Leitfaden als Arbeitswert fuer die BX3-Konsole nennt. RX-Daten werden ausserdem auf den lokalen seriellen USB/UART0-Monitor gespiegelt; dort koennen Firmware-Diagnosemeldungen dazwischen erscheinen. Bytes, die lokal ueber USB/UART0 eingegeben werden, werden im Konsolenmodus unveraendert zum Ziel-UART weitergereicht.

Die Webkonsole besitzt bewusst keine eigene Authentifizierung oder TLS-Schicht. Sie sollte deshalb nur in einem vertrauenswuerdigen Netz verwendet werden. Der physische BX3-RX-Pin und die Pegel muessen weiterhin vor aktivem TX sicher bestimmt werden; BX3-VCC darf nicht mit der ESP32-Versorgung verbunden werden.


**Neu v0.13:** Der UART-Decoder besitzt jetzt einen automatischen Probe-Runner fuer den vollstaendigen Katalog mit 1000 Testkandidaten. Die Frames werden zur Laufzeit exakt aus den Familien A/B/C/D erzeugt, sodass keine grosse statische Hexliste im RAM noetig ist. Vor dem Sweep wird D2 zwei Sekunden als Baseline beobachtet. Danach wird jeder Kandidat 5 Sekunden lang alle 250 ms gesendet (ca. 20 Aussendungen), waehrend RX und Decoder weiterlaufen. Der serielle Monitor protokolliert Kandidaten-ID, Katalognummer, Prioritaet, Hexframe, Fortschritt und erkannte Reaktionen. Geprueft werden neue Commands, deutliche Wert-/Meta-/Statusaenderungen, Ratenabweichungen und ein Aussetzen der D2-Ausgabe. Im Webmonitor kann der Sweep entweder bei der ersten Auffaelligkeit stoppen oder alle Kandidaten durchlaufen. Die Testreihenfolge folgt P0 -> P1 -> P2 -> P3. Die historischen SLOW/INIT-Bezeichnungen der D-Familie bleiben zur Rueckverfolgbarkeit erhalten; fuer den automatischen Runner gilt jedoch einheitlich die neue 250-ms/5-s-Vorgabe.

**Neu v0.12:** Der Decoder wurde auf die st10-Spezifikation umgestellt. Er validiert jetzt den aeusseren Frame `FF FB Route OuterLength`, den inneren Frame `FF FD/FE InnerLength Command DATA XOR`, die Laengenbeziehung `OuterLength = InnerLength + 4` sowie die XOR-Pruefsumme ab `InnerLength` bis zum letzten Datenbyte. Bekannte Commands `0x0021`, `0x0023`, `0x0031` und `0x0033` werden getrennt gezaehlt und dekodiert. Die bisherigen Felder 1..6 stammen nun explizit aus Command `0x0021`; Feld 1..5 bleiben kalibrier-/normierbar. Der vorhandene Feld-5-TX-Replay bleibt erhalten, ist aber als experimentell markiert, weil st10 fuer D4/RX des Zielgeraets noch kein gueltiges Eingangsprotokoll belegt.




**Erweiterung v0.8:** Die UART-Funktion ist jetzt in zwei getrennte Webbereiche aufgeteilt. `/uart` ist eine reine Live-Monitor-/Decoder-Seite; `/uart/settings` enthaelt ausschliesslich Betriebsart, Hardware-UART, GPIOs, Baudrate, Format, Feld-Aliase, Totzone und Kalibrierung. Raw- und Decoder-Modus laufen als Dauerbetrieb unabhaengig davon weiter, ob die Browserseite geoeffnet ist. Der gewaehlte Modus bleibt in NVS gespeichert und wird nach einem Neustart automatisch wieder gestartet. Der 512-Byte-Ringpuffer ueberschreibt im Dauerbetrieb nur die jeweils aeltesten Rohbytes.



**Neu v0.10:** Auf der UART-Monitorseite gibt es jetzt explizite **START**- und **STOP**-Schaltflaechen. STOP beendet den aktuellen Hardware-UART-Empfang und Decoder sofort, ohne den gespeicherten Modus, Pins, Baudrate, Aliase oder Kalibrierwerte zu veraendern. START initialisiert die Schnittstelle mit der gespeicherten Konfiguration erneut. Ein manueller STOP ist nur ein Laufzeitzustand und wird nicht in NVS gespeichert; nach einem ESP32-Neustart startet ein gespeicherter Raw-/Decoder-Modus daher wieder automatisch.


**Neu v0.11:** Der UART-Monitor besitzt jetzt einen gezielten TX-Replay-Test fuer **Feld 5**. Voraussetzung ist ein laufender Protokoll-Decoder, ein konfigurierter TX-GPIO und mindestens ein frisch empfangenes gueltiges `0x0021`-Paket. Beim Ausloesen wird dieses letzte Hauptpaket eingefroren, nur Feld 5 mit der vorhandenen Kalibrierung auf `-0,5` oder `+0,5` zurueckgerechnet, die XOR-Pruefsumme neu gebildet und die Paketkopie fuer ca. **1 Sekunde mit 50 Hz** gesendet. Die Sendeausgabe ist nicht blockierend; RX und Decoder laufen parallel weiter.

Der Sendebereich ist absichtlich auf diese zwei Testwerte begrenzt. Ohne gueltige Paketvorlage oder ohne TX-Pin bleiben die Buttons deaktiviert. Nach UART-START bzw. einer Konfigurationsaenderung muss zuerst wieder ein frisches gueltiges Hauptpaket empfangen werden, bevor TX freigegeben wird.
**Korrektur v0.9:** Die UART-Unterseiten werden jetzt eindeutig geroutet. Bei ESPAsyncWebServer 3.x konnte die zuerst registrierte Route `/uart` auch Anfragen an `/uart/settings` und `/uart/status` abfangen. Dadurch zeigte `/uart/settings` faelschlich die Monitorseite und die Live-API lieferte HTML statt JSON; Byte- und Paketzaehler blieben deshalb auf 0. Ab v0.9 werden `/uart/status`, `/uart/settings` und `/uart/monitor` zuerst registriert. `/uart` dient nur noch als kompatibler Redirect auf `/uart/monitor`. Die Monitorseite prueft den JSON-Content-Type und zeigt den Zustand der Status-API sichtbar an.
**Erweiterung v0.7:** Zwei UART-Betriebsarten wurden ergaenzt: ein Raw-/Sniffer-Modus fuer unverarbeitete serielle Daten sowie ein Protokoll-Decoder gemaess der Messunterlage `ESP32_UART_Protokoll_Entschluesselung`. UART-Nummer, RX/TX-Pins, Baudrate und Format sind ueber `/uart` einstellbar und werden in NVS gespeichert. Fuer Feld 1 bis Feld 6 gibt es frei editierbare Aliasnamen, waehrend die feste technische Kennung `Feld N` immer sichtbar bleibt.

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


## UART Einstellungen, UART Konsole, UART Decoder und UART Probe-Runner

Seit v0.15 existiert nur noch **ein gemeinsamer Hardware-UART-Basisbetrieb**. Konsole, Decoder und Probe-Runner sind keine gegenseitig ausschliessenden Modi mehr. Solange der UART laeuft, wird jedes empfangene Byte parallel an folgende Verbraucher verteilt:

```text
UART RX
  |
  +--> Rohdaten-Ringpuffer
  +--> UART Konsole
  +--> st10 UART Decoder
  +--> Probe-Runner-Auswertung
```

Die Startseite bietet dafuer vier getrennte Menues:

- **UART Einstellungen** - Hardware-UART, RX/TX-GPIO, Baudrate, Frameformat, UART-Autostart und separate TX-Freigabe.
- **UART Konsole** - universelles Live-Terminal mit Text-, HEX- und HEX+ASCII-Anzeige sowie manueller TX-Eingabe.
- **UART Decoder** - st10-Decoder, Feldwerte, Raw-Daten, Aliase/Kalibrierung und experimenteller Feld-5-Replay.
- **UART Probe-Runner** - automatischer Test der 1000 Kandidaten mit eigener Laufzeit- und Reaktionsanzeige.

WLAN, NTP, MQTT, Batterie, Deep Sleep und OTA bleiben davon unabhaengig und unveraendert erhalten.

### UART Einstellungen und TX-Freigabe

In NVS gespeichert werden UART-Autostart, Hardware-UART 1 oder 2, RX-GPIO, konfigurierter TX-GPIO, separate TX-Freigabe, Baudrate und UART-Format (`8N1`, `8E1`, `8O1`, `8N2`). UART0 bleibt fuer den seriellen Debug-Monitor reserviert. GPIO6..11 werden wegen des Flashs gesperrt; GPIO1/3 bleiben fuer UART0 frei. GPIO34..39 koennen nur als RX verwendet werden.

**TX-Sicherheitslogik:** Ein eingetragener TX-GPIO bedeutet noch nicht, dass der ESP32 sendet. Erst die Option **TX-Ausgang bewusst freigeben** aktiviert die Senderichtung. Solange sie aus ist, wird der Hardware-UART mit `TX=-1` gestartet. Beim ersten Start nach einem Upgrade von v0.14 oder aelter wird TX sicherheitshalber deaktiviert, auch wenn zuvor ein TX-GPIO hinterlegt war.

### Universelle UART Konsole

Die Seite `/uart/console` liest denselben RX-Datenstrom wie der Decoder. Deshalb muss nicht mehr auf einen eigenen Konsolenmodus umgeschaltet werden. Der Konsolenringpuffer fasst 8192 Bytes. Die Browserseite fragt neue Bytes inkrementell anhand einer Sequenznummer ab.

Anzeige:

- Text
- HEX
- HEX + ASCII

TX-Eingabe:

- Text/ASCII
- frei eingebbare HEX-Bytes, z. B. `48 65 6C 6C 6F 0D`
- waehbarer Zeilenabschluss `CR`, `LF`, `CRLF` oder keiner
- Ctrl+C, Ctrl+D, TAB und ESC
- optional verdeckte Texteingabe fuer Passwoerter

Die Konsole darf nur senden, wenn UART laeuft, TX freigegeben ist, ein gueltiger TX-GPIO gesetzt ist und kein anderer aktiver Sender TX reserviert. Probe-Runner und Feld-5-Replay sperren manuelle Konsolen-TX-Ausgaben fuer ihre Laufzeit.

**Autoscroll v0.16:** In der Konsole kann **Autoscroll bei neuen UART-Daten** per Häkchen ein- oder ausgeschaltet werden. Bei aktiviertem Autoscroll folgt die Ansicht automatisch dem neuesten Datenende; bei deaktiviertem Autoscroll bleibt die manuell gewählte Scrollposition erhalten. Der Zustand wird nur im jeweiligen Browser per `localStorage` gespeichert und nicht in der ESP32-NVS abgelegt.

Der BX3-Leitfaden nennt fuer die dort beobachtete Linux-Konsole **115200 Baud / 8N1** als Arbeitswert. Der physische BX3-Console-RX-Pin und die elektrischen Pegel muessen weiterhin vor aktivem TX sicher bestimmt werden. BX3-VCC darf nicht mit der ESP32-Versorgung verbunden werden.

**HTTP-Hinweis:** Das Webinterface besitzt keine eigene TLS-/Login-Schicht. Login-Daten oder Shell-Zugriff nur in einem vertrauenswuerdigen Netz verwenden.

### UART Decoder

Der Decoder verarbeitet RX permanent parallel zur Konsole. Er synchronisiert auf `FF FB`, prueft Outer-/Inner-Laengen und die XOR-Pruefsumme und dekodiert die bekannten Commands `0x0021`, `0x0023`, `0x0031` und `0x0033`. Feld 1 bis 5 des Commands `0x0021` koennen weiterhin kalibriert und auf `-1.0 ... +1.0` normiert werden; Feld 6 bleibt als Rohwert sichtbar. Die frei editierbaren Aliase und Kalibrierwerte werden weiterhin in NVS gespeichert.

Der vorhandene Feld-5-Replay bleibt als experimentelle TX-Funktion erhalten. Er benoetigt TX-Freigabe und ein frisches gueltiges `0x0021`-Paket. Waehren eines Probe-Runs ist diese Funktion blockiert.


### UART Probe-Runner

Der automatische Runner hat ab v0.15 eine eigene Seite `/uart/probe`. Er zeichnet zuerst 2 Sekunden Baseline auf und testet anschliessend jeden Kandidaten 5 Sekunden lang mit einem Sendeintervall von 250 ms. Die Reihenfolge folgt P0 -> P1 -> P2 -> P3. Der Decoder wertet parallel neue Commands, Wert-/Meta-/Statusaenderungen, Ratenabweichungen und Empfangspausen aus.

Der Runner benoetigt einen laufenden UART, einen gueltigen TX-GPIO und die separate TX-Freigabe. Solange der Runner aktiv ist, besitzt er die TX-Reservierung. Die Konsole bleibt fuer RX sichtbar, kann aber nicht senden.

In v0.15 wurde ausserdem der Kandidatenfortschritt korrigiert: Nach einem abgeschlossenen Kandidaten wird der Testindex genau einmal erhoeht.

### UART-Webrouten

| Route | Funktion |
|---|---|
| `/uart/status` | Gemeinsamer UART-/Decoder-/Probe-Livestatus als JSON |
| `/uart/settings` | Hardware-UART und TX-Freigabe |
| `/uart/console` | Universelle UART-Webkonsole |
| `/uart/console/data` | Inkrementelle RX-Daten mit Text- und HEX-Darstellung |
| `/uart/console/send` | Text- oder HEX-Daten senden, POST |
| `/uart/console/control` | Ctrl+C/Ctrl+D/TAB/ESC senden, POST |
| `/uart/console/clear` | Konsolenringpuffer leeren, POST |
| `/uart/decoder` | st10-Decoder, Raw-Daten und Decoder-Einstellungen |
| `/save_uart_decoder` | Aliase/Kalibrierung speichern, POST |
| `/uart/probe` | UART Probe-Runner |
| `/uart/probe/start_hit` | Sweep starten und beim ersten Treffer stoppen, POST |
| `/uart/probe/start_all` | Alle 1000 Kandidaten testen, POST |
| `/uart/probe/stop` | Probe-Runner stoppen, POST |
| `/uart/monitor` | Kompatibler Redirect auf `/uart/decoder` |
| `/uart` | Kompatibler Redirect auf `/uart/decoder` |
| `/save_uart` | Hardware-UART/TX-Freigabe speichern und UART neu starten, POST |
| `/uart_clear` | Rohdatenpuffer / RX-Bytezaehler leeren, POST |

### UART-MQTT-Status

Unter `<Basis>/uart/` werden weiterhin Status, Byte-/Paketzaehler und dekodierte Felder veroeffentlicht. Neu sind `enabled`, `tx_enabled` und `tx_owner`. Das bisherige Topic `mode` bleibt aus Kompatibilitaetsgruenden bestehen und meldet ab v0.15 `UART Basisbetrieb` bzw. `Aus`.

### Elektrischer Hinweis

Die universelle Konsole ist softwareseitig fuer TTL-UART ausgelegt. RS-232, RS-485 oder abweichende Spannungspegel benoetigen passende Transceiver/Pegelwandler. Fuer erste Messungen TX gesperrt lassen, gemeinsame Masse herstellen und den High-Pegel der Ziel-UART messen. ESP32-GPIOs sind nicht 5-V-tolerant.

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
| `/uart/status` | UART Live-Status als JSON |
| `/uart/settings` | Hardware-UART, Autostart und TX-Freigabe |
| `/uart/decoder` | UART Decoder, Raw-Daten und Decoder-Einstellungen |
| `/uart/monitor` | kompatibler Redirect auf `/uart/decoder` |
| `/uart/console` | Universelle bidirektionale UART-Webkonsole |
| `/uart/console/data` | inkrementelle Terminaldaten als JSON |
| `/uart/console/send` | Konsoleneingabe senden, POST |
| `/uart/console/control` | Steuerzeichen senden, POST |
| `/uart/console/clear` | Konsolenpuffer leeren, POST |
| `/uart/probe` | UART Probe-Runner |
| `/uart` | Redirect auf `/uart/decoder` |
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
