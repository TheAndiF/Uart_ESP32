# Uart_Esp32 - Projektdokumentation - Firmware - v0.22

**Projektname:** Uart_Esp32  
**Dokumentenart:** Projektdokumentation  
**Thema:** UART-Konsole mit Sendeschutz und stabiler Image-Quelleneingabe  
**Erstellungsdatum:** 2026-09-22  
**Autor / verantwortliche Person:** OpenAI ChatGPT  
**Version:** v0.22  
**Status:** Entwurf - JavaScript-Syntax geprueft, Hardwaretest offen  
**Ablageort:** Projektordner Uart_ESP32/docs  

## 1. Zusammenfassung

Version v0.22 korrigiert die UART-Konsoleneingabe. In den vorherigen Tests konnte eine Eingabe wie `root` mehrfach gesendet werden, wodurch am BX3-Prompt Zeichenfolgen wie `rootrootroot` sichtbar wurden. Die Ursache lag in der Weboberflaeche: Button-Klicks, Enter-Tastenereignisse oder schnelle Wiederholungen konnten mehrere HTTP-Sendeanforderungen ausloesen, bevor die erste Anforderung abgeschlossen war.

Die Korrektur fuehrt eine browserseitige Sendesperre, eine kurze Cooldown-Zeit und eine Entkopplung des Eingabefelds vom laufenden Sendevorgang ein. Dadurch wird ein Login-Name, Passwort oder Shell-Befehl nur einmal pro Benutzeraktion uebertragen.

Zusaetzlich wurde die Quelleingabe des UART Image-Transfers robuster gemacht. Das Feld wird nicht mehr durch Status-Polling ueberschrieben und wird lokal im Browser gespeichert. Zwei Schnellbuttons setzen die haeufigen Quellen `/tmp/mtd7.img.gz` und `/dev/mtd7ro`.

## 2. Geaenderte Funktionen

| Bereich | Aenderung | Zweck |
|---|---|---|
| UART Konsole | Sendesperre waehrend laufendem HTTP-Request | verhindert parallele Mehrfachsendungen |
| UART Konsole | Cooldown fuer identische Eingaben | verhindert schnelle Doppelklicks oder doppelte Enter-Ausloesung |
| UART Konsole | Eingabefeld wird vor dem Senden geleert | verhindert erneutes Senden alter Eingaben |
| UART Konsole | Eingabe wird bei Sendefehler wiederhergestellt | vermeidet Datenverlust bei TX-Fehlern |
| UART Konsole | Enter-Repeat und IME-Komposition werden ignoriert | verhindert wiederholte Sendung durch gehaltene Enter-Taste |
| UART Image-Transfer | Quellenfeld wird lokal im Browser gespeichert | verhindert Zurueckspringen auf alte Quellenwerte |
| UART Image-Transfer | Schnellbuttons fuer `/tmp/mtd7.img.gz` und `/dev/mtd7ro` | erleichtert Snapshot- und Live-MTD-Transfers |
| Firmwarekennung | Version auf v0.22 / 0.22.0 angehoben | eindeutige Zuordnung der Auslieferung |

## 3. Neue Konsolenlogik

Beim Senden wird jetzt folgender Ablauf verwendet:

```text
Benutzer klickt Senden oder drueckt Enter
        ↓
Browser prueft: laeuft bereits ein Senderequest?
        ↓
Browser prueft: identische Eingabe innerhalb der Cooldown-Zeit?
        ↓
Eingabefeld wird lokal geleert
        ↓
HTTP-POST an /uart/console/send
        ↓
bei Erfolg: Eingabe bleibt geloescht
bei Fehler: Eingabe wird wiederhergestellt
```

Steuerzeichen wie Ctrl+C, Ctrl+D, TAB und ESC verwenden dieselbe Sperrlogik. Dadurch koennen sie ebenfalls nicht versehentlich mehrfach parallel gesendet werden.

## 4. Bedienhinweise

Fuer den BX3-Login sollte weiterhin nur eine Eingabeaktion pro Zeile verwendet werden: entweder den Button **Eingabe senden** oder die Enter-Taste im Eingabefeld. Die Firmware blockiert doppelte Ausloesungen, trotzdem ist ein einzelner klarer Bedienweg am sichersten.

Als Zeilenabschluss ist fuer den BX3-Login zunaechst **CR (0x0D)** empfehlenswert. CRLF bleibt verfuegbar, kann bei manchen seriellen Login-Prompts aber wie zwei Eingaben wirken.

## 5. Image-Transfer-Quelle

Das Image-Quellenfeld ist nun strikt von der Statusanzeige getrennt:

```text
Eingabefeld Quelle  = naechster gewuenschter Transfer
Status Quelle       = laufender oder letzter Transferstatus der Firmware
```

Die Statusabfrage darf das Eingabefeld nicht mehr ueberschreiben. Der zuletzt eingetragene Wert wird im Browser gespeichert.

Erlaubte Quellen bleiben:

```text
/dev/mtdNro
/tmp/*.img
/tmp/*.img.gz
/var/tmp/*.img
/var/tmp/*.img.gz
```

## 6. Geaenderte Dateien

| Datei | Art der Aenderung |
|---|---|
| `src/main.cpp` | Weboberflaeche der UART-Konsole und des Image-Transfers korrigiert; Firmwarekennung v0.22 |
| `src/ConfigDefaults.h` | `UART_FW_VERSION` auf `0.22.0` gesetzt |
| `VALIDATION_v0.22.txt` | Validierungsprotokoll ergaenzt |
| `docs/2026-09-22 - Uart_Esp32 - Commit-Nachricht - UART-Konsole-Sendeschutz - v0.22.txt` | Commit-Nachricht hinzugefuegt |
| `docs/2026-09-22 - Uart_Esp32 - Projektdokumentation - Firmware - v0.22.*` | Dokumentation als MD, DOCX und PDF |

## 7. Validierungsstand

Die eingebetteten JavaScript-Bloecke wurden aus `src/main.cpp` extrahiert und mit `node --check` syntaktisch geprueft. Die ZIP-Pakete wurden auf Integritaet geprueft. Die DOCX- und PDF-Dokumentation wurde gerendert und visuell kontrolliert.

Offen bleiben ein echter PlatformIO-Build, das Flashen auf einen ESP32 und ein realer Test am BX3. Vor dem Flashen sollte lokal ausgefuehrt werden:

```text
pio run -e uart_esp32_usb
```

## 8. Änderungshistorie

| Version | Datum | Bearbeiter | Status | Änderung |
|---|---|---|---|---|
| v0.20 | 2026-09-21 | OpenAI ChatGPT | Entwurf | Snapshot-Dateien aus `/tmp` und `/var/tmp` als Image-Quelle erlaubt |
| v0.21 | 2026-09-22 | OpenAI ChatGPT | Entwurf | Image-Quellenfeld gegen Status-Overwrite geschuetzt |
| v0.22 | 2026-09-22 | OpenAI ChatGPT | Entwurf | UART-Konsoleneingabe gegen Mehrfachsendungen abgesichert |
