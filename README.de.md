# ESP32 UART-Webkonsole

**Browserbasierte UART-Konsole und Geräteverwaltung für ESP32-WROOM-32 / NodeMCU-32S.**

Mit dem ESP32 lassen sich serielle Daten im Browser anzeigen, Befehle senden und – bei kompatiblen Zielgeräten – Dateien übertragen. Hinzu kommen WLAN-Konfiguration, MQTT-Status, OTA-Updates und Energiemanagement. Entwickelt wurde die Firmware insbesondere für einen **Marstek-BX3-Anwendungsfall**; viele UART-Grundfunktionen eignen sich auch für andere Geräte mit 3,3-V-TTL-UART.

### Erstinstallation per USB direkt im Browser

Die GitHub-Pages-Installationsseite unter `web-flasher/` ermöglicht das Flashen eines klassischen ESP32 (`nodemcu-32s`) über USB und einen Web-Serial-fähigen Desktop-Browser. Der Workflow `.github/workflows/web-flasher.yml` baut dafür bei Änderungen an `main` die Firmware und veröffentlicht die Seite samt vollständigem Flash-Image.

**Einrichtung:** Unter *Repository → Settings → Pages → Build and deployment* als Quelle **GitHub Actions** auswählen. Nach erfolgreichem Workflow ist der Installer unter `https://theandif.github.io/Uart_ESP32/` erreichbar. ESP32 per USB-Datenkabel anschließen, Chrome oder Edge öffnen und den Installationsknopf verwenden. Dieser Weg erfordert kein lokales PlatformIO.

**Wichtig:** Die USB-Erstinstallation schreibt ein vollständiges Image mit Bootloader und Partitionstabelle. Bestehende Geräteeinstellungen vorher sichern; bei vollständiger Neuinstallation oder geänderter Partitionierung können sie verloren gehen. Für spätere Aktualisierungen bei unveränderter Partitionierung bevorzugt das bereits vorhandene Browser-OTA unter `http://<ESP-IP>/ota` verwenden. Dieser Installer ist nicht für ESP32-C3/S3 gedacht.

> **Projektstatus:** Aktives, hardwarenahes Entwicklungsprojekt. Einige Funktionen (insbesondere BX3-Protokolldecoder, Probe-Runner und Shell-basierte Dateiübertragung) sind gerätespezifisch. Vor dem Anschluss bitte Kompatibilität und Sicherheitshinweise prüfen.

**[English README](README.md)** · [Schnellstart](#schnellstart) · [Funktionen](#funktionen) · [Technischer Leitfaden](docs/README_technical_history.md) · [Projektdokumente](docs/)

## Funktionen

| Funktion | Beschreibung |
| --- | --- |
| UART-Webkonsole | Bidirektionale Text- und HEX-Ein-/Ausgabe, Zeilenenden, Steuerzeichen, Verlauf und mehrzeilige Befehle |
| Gespeicherte Befehle | Wiederverwendbare Kommandoblöcke im ESP32-NVS |
| Dateiübertragung | Upload vom Browser zum Zielgerät und Download in Gegenrichtung über eine kompatible BX3-/Linux-Shell |
| Windows-PowerShell-Client | Optional gekoppelte interaktive Konsole, Dateiübertragung, Status und Heartbeat (`tools/UartEsp32.ps1`) |
| UART-Diagnose | Rohdatenansicht, gerätespezifischer Decoder und experimenteller Probe-Runner |
| Netzwerk | WLAN-Einrichtung und Recovery-AP, MQTT-Status und -Steuerung |
| Wartung | Browser-OTA, ArduinoOTA / PlatformIO-OTA und Neustartfunktionen |
| Energieverwaltung | Batterie-ADC-Überwachung und einstellbarer Deep Sleep |

### Screenshots und Hardwarefotos

Eine mit echten Aufnahmen geprüfte Bildergalerie ist derzeit noch nicht vorhanden. Bis zur Ergänzung sind die Bedienoberflächen und Webrouten im [technischen Leitfaden](docs/README_technical_history.md) beschrieben. Beispielbilder sind keine geeigneten Anschlusspläne.

## Schnellstart

### Voraussetzungen

- ESP32-WROOM-32 / NodeMCU-32S (in PlatformIO ist aktuell `nodemcu-32s` eingestellt)
- [PlatformIO](https://platformio.org/) und USB-Verbindung zum erstmaligen Flashen
- Kompatibles Zielgerät mit **3,3-V-TTL-UART** und überprüfter Pinbelegung und Signalspannung
- Computer oder Smartphone im selben vertrauenswürdigen Netzwerk

### Kompilieren und flashen

```bash
git clone https://github.com/TheAndiF/Uart_ESP32.git
cd Uart_ESP32
pio run -e uart_esp32_usb
pio run -e uart_esp32_usb -t upload
```

Dies ist der dokumentierte PlatformIO-Arbeitsablauf, **keine Bestätigung eines Hardwaretests dieser Fassung**. Vor dem Flashen die richtige Platine und den USB-Port überprüfen.

Optional `include/arduino_secrets.example.h` nach `include/arduino_secrets.h` kopieren und lokale Zugangsdaten eintragen. Echte Passwörter niemals ins Git-Repository übernehmen. Viele Einstellungen können über die Weboberfläche vorgenommen werden.

### Erste Verbindung

1. ESP32 einschalten und mit dem konfigurierten WLAN verbinden.
2. Ohne gültige WLAN-Zugangsdaten startet normalerweise der Einrichtungs-AP `Uart_Esp32-Setup` unter `http://192.168.4.1/`. Dieser AP hat standardmäßig **kein Passwort**, sofern keines konfiguriert wurde.
3. Im Browser **WLAN / NTP** aufrufen und die Verbindung konfigurieren. Danach die zugewiesene IP-Adresse des ESP32 öffnen.
4. Vor der TX-Freigabe unbedingt UART-Spannungspegel und Pinbelegung prüfen. Anschließend UART-Einstellungen vornehmen und die Webkonsole öffnen.

Details zu WLAN-Fallback, UART-Schnittstellen, MQTT-Topics, OTA und Konfigurationsprioritäten stehen im [vollständigen technischen Leitfaden](docs/README_technical_history.md).

## Sicherheit und Einschränkungen

- **Nur 3,3-V-TTL-UART:** ESP32-GPIOs vertragen keine 5 V. Für RS-232 und RS-485 werden passende Transceiver benötigt. Zielspannung prüfen, Masse fachgerecht verbinden und BX3-VCC nicht mit der ESP32-Versorgung verbinden.
- Die Weboberfläche hat **keine eigene TLS-/Login-Schicht**. Nur in einem vertrauenswürdigen, abgeschotteten Netzwerk verwenden; keinesfalls direkt ins Internet stellen.
- Bestimmte Dateitransfers benötigen eine passende BX3-/Linux-Shell und Werkzeuge wie `base64`, `cksum` und `dd`. Es handelt sich **nicht um einen universellen binären UART-Dateitransfer**.
- Experimentelle Probe- und Sendefunktionen können das angeschlossene Gerät beeinflussen. TX zunächst deaktiviert lassen, bis die elektrische Verbindung und die Kommandos sicher geklärt sind.

## Dokumentation

- [Bisherige vollständige README mit Entwicklungs- und Technikdetails](docs/README_technical_history.md) — inhaltlich unverändert erhalten
- [Projektdokumentation](docs/) — versionierte PDF-, DOCX- und Markdown-Dokumente sowie Änderungsnotizen
- [Validierungsnotizen](docs/validation/) — versionsbezogene Prüfungen, kein Ersatz für Hardwaretests
- [PowerShell-Client](tools/UartEsp32.ps1)
- [PlatformIO-Konfiguration](platformio.ini)

## Mitwirken und Lizenz

Rückmeldungen, nachvollziehbare Fehlerberichte und Dokumentationsverbesserungen sind über [GitHub Issues](https://github.com/TheAndiF/Uart_ESP32/issues) willkommen. Bitte ESP32-Platine, Firmwarestand, Zielhardware und relevante Logs ohne Zugangsdaten angeben.

**Lizenz:** Für das Repository wurde bislang keine Lizenzdatei festgelegt. Ohne ausdrückliche Lizenz darf keine allgemeine Open-Source-Nutzungserlaubnis angenommen werden. Vor einer breiteren Weiterverwendung sollte der Autor die Lizenz bestimmen.
