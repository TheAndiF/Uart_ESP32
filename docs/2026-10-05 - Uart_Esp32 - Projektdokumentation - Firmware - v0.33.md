# Uart_Esp32 - Projektdokumentation - Firmware - v0.33

**Projektname:** Uart_Esp32  
**Dokumentenart:** Projektdokumentation / Firmware  
**Erstellungsdatum:** 2026-10-05  
**Verantwortlich:** OpenAI / ChatGPT - technische Umsetzung und Dokumentation  
**Version:** v0.33  
**Status:** Arbeitsstand - Hardwarepruefung ausstehend  
**Aenderungsdatum:** 2026-10-05  
**Ablageort:** Projektordner `Uart_ESP32/docs`  
**Referenzen:** v0.32, `VALIDATION_v0.33.txt`, Commit-Nachricht v0.33

## 1. Ziel der Aenderung

Version v0.33 erweitert die in v0.32 eingefuehrte PowerShell-Unterstuetzung zu einer bewusst aktivierbaren, gekoppelten und interaktiven Windows-PowerShell-Bedienung. Der Benutzer aktiviert die Funktion in der Weboberflaeche. Der erste Windows-Client fuehrt danach einmalig einen Handshake aus. Nach erfolgreicher Kopplung kann derselbe Client die BX3-UART-Konsole direkt aus Windows PowerShell bedienen sowie Datei-Up- und Downloads starten.

Die in v0.32 entfernte Dateiendungs-Beschraenkung bleibt bestehen. Dateitransfers sind weiterhin nicht auf `.img` oder `.img.gz` beschraenkt. Die vorhandenen sicheren BX3-Pfadregeln bleiben erhalten.

## 2. Bedienablauf

1. Weboberflaeche des ESP32 oeffnen.
2. `UART Einstellungen` oeffnen.
3. `PowerShell-Steuerung aktivieren` einschalten und speichern.
4. Unter Windows im Projektordner ausfuehren: `./tools/UartEsp32.ps1 -Connect 192.168.4.1`.
5. Beim ersten Verbindungsaufbau fuehrt der Client automatisch den einmaligen Handshake durch und speichert das Token im lokalen Benutzerprofil.
6. Spaetere Verbindungen verwenden die gespeicherte Kopplung ohne neuen Handshake.
7. In der laufenden Sitzung werden normale Tastatureingaben direkt an UART weitergereicht.

Die Weboberflaeche zeigt an, ob bereits ein PowerShell-Client gekoppelt ist. Mit `PowerShell-Kopplung zuruecksetzen` wird das ESP32-seitige Token geloescht; der lokal gespeicherte alte Client-Schluessel ist danach ungueltig und eine neue Kopplung ist erforderlich.

## 3. Interaktive PowerShell-Konsole

Start:

```powershell
.\tools\UartEsp32.ps1 -Connect 192.168.4.1
```

Normale Tasten werden direkt an die UART-Konsole weitergereicht. Enter sendet CR. Ctrl+C, Ctrl+D, Tab und Esc werden als Steuerzeichen uebertragen. Damit kann die BX3-Shell wie in einem seriellen Terminal bedient werden.

Lokale PowerShell-Clientbefehle beginnen mit `/`:

- `/help` - lokale Befehle anzeigen.
- `/status` - UART-Status anzeigen.
- `/heartbeat on` bzw. `/heartbeat off` - vorhandenen 60-s-Konsolen-Heartbeat schalten.
- `/upload "C:\Build\ptybridge" "/tmp/bx3test/ptybridge"` - Datei Windows -> BX3.
- `/download "/tmp/mtd7.img" "C:\Backup\mtd7.img"` - Datei BX3 -> Windows.
- `/clear` - lokale PowerShell-Anzeige leeren.
- `/exit` - Sitzung beenden.

BX3-Befehle, die selbst mit `/` beginnen, werden mit doppeltem Slash eingegeben. `//tmp/testprogramm` wird beispielsweise als `/tmp/testprogramm` an die BX3-Shell gesendet.

## 4. Upload Windows -> BX3

Der Upload verwendet weiterhin den etablierten blockweisen Firmware-Transfer. Die Datei wird nicht komplett im ESP32-RAM gehalten. PowerShell liest die lokale Datei in 4096-Byte-Bloecken und uebergibt sie an die vorhandenen Upload-Endpunkte. Der ESP32 uebertraegt die Daten exklusiv ueber UART an die BX3, prueft die Bloecke und beendet den Transfer erst nach der bestehenden Groessen-/Prueflogik.

Interaktiv:

```text
/upload "C:\Build\ptybridge" "/tmp/bx3test/ptybridge"
```

Ohne Argumente fragt der Client lokale Datei und BX3-Zielpfad ab. Fuer Automatisierung bleibt zusaetzlich die direkte Aktion erhalten:

```powershell
.\tools\UartEsp32.ps1 -Esp 192.168.4.1 -Action upload -LocalPath .\ptybridge -TargetPath /tmp/bx3test/ptybridge
```

Dateiendungen werden nicht eingeschraenkt. Die sichere BX3-Zielpfadpruefung bleibt bestehen.

## 5. Download BX3 -> Windows

Der Download verwendet den bestehenden BX3->ESP32-Blocktransfer. PowerShell ruft validierte Bloecke vom ESP32 ab, schreibt sie direkt in die lokale Zieldatei und bestaetigt jeden Block gegenueber der Firmware.

Interaktiv:

```text
/download "/tmp/mtd7.img" "C:\Backup\mtd7.img"
```

Ohne lokale Zieldatei wird ein Dateiname vorgeschlagen. Fuer Automatisierung bleibt die direkte Aktion erhalten:

```powershell
.\tools\UartEsp32.ps1 -Esp 192.168.4.1 -Action download -Source /tmp/archive.tar.gz -OutFile .\archive.tar.gz
```

Die v0.32-Regel bleibt: sichere Quellen unter `/tmp/` und `/var/tmp/` koennen unabhaengig von der Dateiendung geladen werden; `/dev/mtdNro` bleibt fuer MTD-Abbilder zulaessig.

## 6. Handshake und Zugriffsschalter

Die PowerShell-Funktion ist standardmaessig deaktiviert. Aktiviert wird sie bewusst in `UART Einstellungen`. Ist noch kein Client gekoppelt, darf der erste v0.33-PowerShell-Client einen zufaelligen Kopplungstoken anfordern. Der ESP32 speichert diesen Token in NVS, der Windows-Client im lokalen Benutzerprofil unter `%LOCALAPPDATA%\UartEsp32`.

Jeder unterstuetzte PowerShell-API-Aufruf kennzeichnet sich als PowerShell-Client und sendet den Token mit. Ist die Funktion deaktiviert oder der Token falsch, weist die Firmware den Aufruf zurueck. Das Deaktivieren loescht die Kopplung nicht; dadurch kann dieselbe Kopplung nach erneutem Aktivieren weiterverwendet werden. `PowerShell-Kopplung zuruecksetzen` loescht sie bewusst.

Hinweis zur Sicherheit: Die bestehende Webschnittstelle arbeitet ueber HTTP und verwendet kein TLS. Kopplungstoken und Steuerdaten sollten deshalb nur in einem vertrauenswuerdigen lokalen Netzwerk verwendet werden. Der Schalter und die Kopplung ersetzen keine verschluesselte Netzwerkverbindung.

## 7. Geaenderte Dateien

- `README.md`
- `src/main.cpp`
- `tools/UartEsp32.ps1`
- `VALIDATION_v0.33.txt`
- `docs/2026-10-05 - Uart_Esp32 - Commit-Nachricht - Interaktive-PowerShell-Kopplung - v0.33.txt`
- `docs/2026-10-05 - Uart_Esp32 - Projektdokumentation - Firmware - v0.33.md`
- `docs/2026-10-05 - Uart_Esp32 - Projektdokumentation - Firmware - v0.33.docx`
- `docs/2026-10-05 - Uart_Esp32 - Projektdokumentation - Firmware - v0.33.pdf`

## 8. Validierung und offene Hardwarepruefung

Die Implementierung wurde statisch auf Routen, Persistenz, Authentifizierungsheader, Paketstruktur und Transferpfade geprueft. PlatformIO und Windows PowerShell stehen in der Ausfuehrungsumgebung nicht zur Verfuegung. Ein echter Firmware-Build sowie der Test mit ESP32 und BX3 konnten deshalb hier nicht ausgefuehrt werden.

Vor Freigabe auf Hardware sind insbesondere erster Handshake, Wiederverbindung ohne neuen Handshake, Deaktivierung, Pairing-Reset, direkte Konsolentasten, Upload und Download mit beliebigen sicheren Dateinamen zu pruefen. Die Detailcheckliste steht in `VALIDATION_v0.33.txt`.

## 9. Aenderungshistorie

| Version | Datum | Bearbeiter | Status | Aenderung |
|---|---|---|---|---|
| v0.32 | 2026-10-05 | OpenAI / ChatGPT | Arbeitsstand | Dateitypbeschraenkung beim Download entfernt; erster PowerShell-HTTP-Client. |
| v0.33 | 2026-10-05 | OpenAI / ChatGPT | Arbeitsstand | Web-Schalter, einmalige Kopplung, interaktive PowerShell-Konsole sowie integrierter Up-/Download. |
