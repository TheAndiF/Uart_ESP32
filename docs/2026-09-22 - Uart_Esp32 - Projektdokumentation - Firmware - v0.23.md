# Uart_Esp32 - Projektdokumentation - Firmware - v0.23

**Projektname:** Uart_Esp32  
**Dokumentenart:** Projektdokumentation  
**Thema:** UART Image-Transfer - stabiles Quellenfeld und Browser-Cache-Schutz  
**Version:** v0.23  
**Datum:** 2026-09-22  
**Status:** Entwurf / Implementierung vorbereitet, Hardwaretest offen  
**Verantwortlich:** OpenAI ChatGPT  
**Ablageort:** Uart_ESP32/docs  

## 1. Ziel der Änderung

In v0.22 blieb das Quellenfeld im Menü **UART Image-Transfer** in der Praxis weiterhin unzuverlässig bedienbar. Obwohl das Feld optisch editierbar war, konnte es je nach Browserzustand, gespeichertem Altwert oder Statusaktualisierung wieder auf `/dev/mtd7ro` zurückfallen. Für den vorgesehenen Snapshot-Workflow muss jedoch zuverlässig `/tmp/mtd7.img.gz` als Quelle einstellbar bleiben.

v0.23 trennt deshalb das Eingabefeld vollständig von der laufenden Statusanzeige. Der Benutzerwert im Feld ist der geplante nächste Transfer. Der Statusbereich zeigt nur die aktuell bzw. zuletzt von der Firmware verwendete Quelle.

## 2. Änderungshistorie

| Version | Datum | Bearbeiter | Status | Änderung |
|---|---|---|---|---|
| v0.20 | 2026-09-21 | OpenAI ChatGPT | Entwurf | Snapshot-Dateien aus `/tmp` und `/var/tmp` als Image-Transfer-Quelle erlaubt. |
| v0.21 | 2026-09-22 | OpenAI ChatGPT | Entwurf | Erste Korrektur gegen Überschreiben des Quellenfelds durch Status-Polling. |
| v0.22 | 2026-09-22 | OpenAI ChatGPT | Entwurf | UART-Konsole gegen Mehrfachsendungen abgesichert; Quellenfeld lokal gespeichert. |
| v0.23 | 2026-09-22 | OpenAI ChatGPT | Entwurf | Quellenfeld vollständig vom Status-Polling getrennt, versionierter Browser-Speicher, Snapshot-Standardquelle und Cache-Schutz ergänzt. |

## 3. Umgesetzte Firmware-Änderungen

### 3.1 Versionierung

Die Firmwarekennung wurde von `v0.22` auf `v0.23` angehoben. Zusätzlich wurde `UART_FW_VERSION` von `0.22.0` auf `0.23.0` gesetzt.

### 3.2 Stabiler Quellenwert

Das Quellenfeld verwendet jetzt einen neuen lokalen Browser-Schlüssel:

```text
uartImageSourceV23
```

Damit werden alte gespeicherte Werte aus früheren Versionen nicht mehr automatisch als Startwert übernommen. Standardquelle für den neuen Ablauf ist:

```text
/tmp/mtd7.img.gz
```

### 3.3 Status-Polling darf das Feld nicht mehr überschreiben

Die Image-Transfer-Seite aktualisiert weiterhin den Statusbereich alle 400 ms. Das Eingabefeld selbst wird dabei nicht mehr aus `status.source` beschrieben.

Damit sind diese beiden Werte getrennt:

| Bereich | Bedeutung |
|---|---|
| Quellen-Eingabefeld | Quelle für den nächsten Transfer |
| Statusanzeige Quelle | aktuell oder zuletzt verwendete Firmware-Quelle |

### 3.4 Schnellbuttons

Die Weboberfläche enthält nun bedienbare Schnellbuttons:

```text
Snapshot /tmp/mtd7.img.gz einsetzen
Live-MTD /dev/mtd7ro einsetzen
Feld leeren
```

Die Buttons setzen ausschließlich das sichtbare Eingabefeld und speichern den Wert im Browser. Der Transfer startet erst, wenn der Benutzer ausdrücklich `Image-Transfer starten` klickt.

### 3.5 Browser-Cache-Schutz

Der HTML-Kopf enthält zusätzliche No-Cache-Metadaten. Zusätzlich werden POST-Aufrufe der Image-Seite mit `cache: no-store` gesendet. Dadurch sinkt das Risiko, dass ein Browser nach einem Firmware-Update alten JavaScript-Code weiterverwendet.

## 4. Geänderte Dateien

| Datei | Änderung |
|---|---|
| `src/main.cpp` | Firmwareversion v0.23; Image-Transfer-Seite mit versioniertem Quellenfeld, Schnellbuttons, No-Cache-Meta und cachefreien POSTs. |
| `src/ConfigDefaults.h` | OTA-/Firmwareversion auf `0.23.0` gesetzt. |
| `docs/2026-09-22 - Uart_Esp32 - Projektdokumentation - Firmware - v0.23.*` | Dokumentation für diese Version. |
| `docs/2026-09-22 - Uart_Esp32 - Commit-Nachricht - Image-Quelle-Stabilisierung - v0.23.txt` | Englische Commit-Nachricht. |
| `VALIDATION_v0.23.txt` | Validierungsprotokoll. |

## 5. Bedienablauf nach v0.23

1. In der UART-Konsole den Snapshot erzeugen:

```sh
rm -f /tmp/mtd7.img /tmp/mtd7.img.gz /tmp/bx3blk && sync && df -h /tmp
```

```sh
dd if=/dev/mtd7ro bs=32768 2>/dev/null | gzip -c > /tmp/mtd7.img.gz && sync && ls -l /tmp/mtd7.img.gz && sha256sum /tmp/mtd7.img.gz
```

2. `UART Image-Transfer` öffnen.
3. Als Quelle `/tmp/mtd7.img.gz` verwenden oder den Snapshot-Schnellbutton drücken.
4. Startblock auf `0` lassen.
5. `Image-Transfer starten` klicken.
6. Nach dem Download die SHA256-Summe am PC gegen den in der Konsole angezeigten Wert vergleichen.

## 6. Validierung

| Prüfung | Ergebnis |
|---|---|
| JavaScript-Syntax der eingebetteten Skripte | Erfolgreich mit `node --check` geprüft. |
| Versionsstrings | `v0.23` und `0.23.0` gesetzt. |
| Patch-Anwendbarkeit | Gegen v0.22 erfolgreich getestet. |
| ZIP-Struktur | Root-Ordner `Uart_ESP32/` beibehalten. |
| Dokumentation | PDF und DOCX aus demselben Stand erstellt und visuell gerendert. |

## 7. Offene Punkte

Ein echter PlatformIO-Build, Flash-Test auf dem ESP32 und der reale BX3-Test mit `/tmp/mtd7.img.gz` bleiben offen. Nach dem Flashen sollte der Browser einmal hart neu geladen werden, zum Beispiel mit `Strg + F5`, damit kein alter JavaScript-Stand aktiv bleibt.

## 8. Commit-Nachricht

Siehe separate Datei:

```text
2026-09-22 - Uart_Esp32 - Commit-Nachricht - Image-Quelle-Stabilisierung - v0.23.txt
```
