# Uart_Esp32 - Projektdokumentation - Firmware - v0.21

**Projektname:** Uart_Esp32  
**Dokumentenart:** Projektdokumentation  
**Thema:** UART Image-Transfer - Quellenfeld bleibt bei Statusaktualisierung editierbar  
**Erstellungsdatum:** 2026-09-22  
**Autor / verantwortliche Person:** OpenAI ChatGPT (technische Bearbeitung)  
**Version:** v0.21  
**Status:** Entwurf - statische Pruefung und Dokumentationsrendering erfolgt; Hardwaretest ausstehend  
**Ablageort:** Projektordner `Uart_ESP32/docs`  
**Referenzen:** Projektstand v0.20, Projektdokumentationsregeln, BX3/ESP32-Konsolenleitfaden

## 1. Anlass

Beim Test der v0.20-Weboberflaeche wurde beobachtet, dass die Anzeige auf der Seite **UART Image-Transfer** wiederholt auf den vorherigen Quellenwert zurueckspringt. Insbesondere beim Eintragen einer Snapshot-Datei wie `/tmp/mtd7.img.gz` konnte das Eingabefeld durch die laufende Statusabfrage wieder auf `/dev/mtd7ro` gesetzt werden.

Die Ursache lag im Browser-JavaScript der Image-Transfer-Seite. Die Funktion `pollImage()` aktualisierte alle 400 ms nicht nur die Statusanzeige, sondern auch das Eingabefeld `img_source`. Damit wurde ein aktuell bearbeiteter Wert ueberschrieben.

## 2. Ziel

Der Benutzer soll im Quellenfeld einen Pfad wie `/tmp/mtd7.img.gz` eintragen koennen, ohne dass die Statusaktualisierung diesen Wert ersetzt. Die Statusanzeige soll weiterhin sichtbar machen, welche Quelle der aktuell laufende oder zuletzt gemeldete Transfer verwendet.

## 3. Umgesetzte Aenderungen

### 3.1 Quellenfeld gegen Status-Overwrite geschuetzt

In `src/main.cpp` wurde die Image-Transfer-Seite um den Browserzustand `imgSourceDirty` erweitert. Das Feld gilt als lokal bearbeitet, sobald es fokussiert oder veraendert wird. Die Statusabfrage schreibt den vom ESP32 gemeldeten Wert nur noch in das Eingabefeld zurueck, wenn das Feld nicht aktiv bearbeitet wurde und nicht den Fokus besitzt.

Dadurch bleibt beispielsweise die Eingabe `/tmp/mtd7.img.gz` stehen, waehrend `pollImage()` weiter Statusdaten abfragt.

### 3.2 Start und Neustart uebernehmen den aktuellen Feldwert

Beim Start oder Neustart wird der aktuelle Feldwert getrimmt und danach als Transferquelle gesendet. Erst zu diesem Zeitpunkt wird der lokale Bearbeitungszustand zurueckgesetzt. Damit ist eindeutig, wann ein editierter Wert wirklich in den Transfer uebernommen wurde.

### 3.3 Versionierung aktualisiert

Die Firmwareanzeige wurde auf `v0.21` gesetzt. Die Default-Firmwareversion in `src/ConfigDefaults.h` wurde auf `0.21.0` gesetzt.

### 3.4 Patch-Reject-Artefakte entfernt

Im v0.20-Ausgangspaket lagen noch mehrere `*.rej`-Dateien aus frueheren Patch-Anwendungen. Diese Dateien gehoeren nicht zum lauffaehigen Firmwareprojekt und wurden aus dem v0.21-Projektpaket entfernt.

## 4. Nicht geaendert

Die v0.20-Funktion fuer Snapshot-Dateien bleibt erhalten. Erlaubte Quellen sind weiterhin eng begrenzt:

- `/dev/mtdNro`
- `/tmp/*.img`
- `/tmp/*.img.gz`
- `/var/tmp/*.img`
- `/var/tmp/*.img.gz`

Nicht funktional geaendert wurden:

- UART-Konsole
- UART Decoder
- UART Probe-Runner
- Image-Transfer-Blockprotokoll
- CRC32-Blockpruefung
- Gesamt-SHA256-Pruefung
- WLAN, MQTT, OTA, Batterie und Deep Sleep

## 5. Bedienhinweis

Fuer einen stabilen Download sollte zuerst in der BX3-Konsole eine Snapshot-Datei erzeugt werden, zum Beispiel:

```sh
rm -f /tmp/mtd7.img /tmp/mtd7.img.gz /tmp/bx3blk; dd if=/dev/mtd7ro bs=32768 2>/dev/null | gzip -c > /tmp/mtd7.img.gz; sync; ls -l /tmp/mtd7.img.gz; sha256sum /tmp/mtd7.img.gz
```

Danach auf der Seite **UART Image-Transfer** als Quelle eintragen:

```text
/tmp/mtd7.img.gz
```

Der Wert bleibt nun im Eingabefeld stehen und wird nicht mehr durch die Statusabfrage auf `/dev/mtd7ro` zurueckgesetzt.

## 6. Geaenderte Dateien

| Datei | Aenderung |
|---|---|
| `src/main.cpp` | Firmwareanzeige auf v0.21 gesetzt; Image-Transfer-JavaScript schuetzt das Quellenfeld vor Status-Overwrite; Beschreibungstext aktualisiert |
| `src/ConfigDefaults.h` | Default-Firmwareversion auf 0.21.0 gesetzt |
| `README.md` | Stand v0.21 dokumentiert |
| `docs/2026-09-22 - Uart_Esp32 - Projektdokumentation - Firmware - v0.21.*` | Dokumentation fuer v0.21 erstellt |
| `docs/2026-09-22 - Uart_Esp32 - Commit-Nachricht - Image-Quelle-Feldschutz - v0.21.txt` | Commit-Nachricht erstellt |
| `VALIDATION_v0.21.txt` | Validierungsprotokoll erstellt |
| `*.rej` | Patch-Reject-Artefakte aus dem Projektpaket entfernt |

## 7. Validierung

Durchgefuehrt wurden:

- JavaScript-Syntaxpruefung aller eingebetteten Webskripte mit `node --check`.
- Kontrolle der geaenderten Stellen in `src/main.cpp`.
- Versionskontrolle fuer `v0.21` und `0.21.0`.
- Patch-Erzeugung gegen v0.20.
- ZIP-Strukturpruefung fuer vollstaendiges Paket und Paket mit geaenderten Dateien.
- DOCX-Erstellung, PDF-Erstellung und visuelle Renderkontrolle.

Nicht durchgefuehrt wurden:

- Kein echter PlatformIO-Build, da PlatformIO in der Arbeitsumgebung nicht installiert ist.
- Kein Flashen auf einen ESP32.
- Kein realer BX3-Test des Eingabefelds im Browser.

## 8. Aenderungshistorie

| Version | Datum | Status | Aenderung |
|---|---|---|---|
| v0.20 | 2026-09-21 | Entwurf | Snapshot-Dateien aus `/tmp` und `/var/tmp` als Image-Transfer-Quelle erlaubt |
| v0.21 | 2026-09-22 | Entwurf | Quellenfeld der Image-Transfer-Seite gegen automatische Status-Ruecksetzung geschuetzt |
