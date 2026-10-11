# UART_ESP32 – Projektdokumentation – NVS-Befehle – v0.34

**Projekt:** UART_ESP32  
**Dokumentenart:** Projektdokumentation / Firmwareänderung  
**Thema:** Benannte UART-Befehle im NVS  
**Erstellungs- und Änderungsdatum:** 2026-10-10  
**Verantwortung:** Technische Erstellung: ChatGPT; fachliche Freigabe: Projektverantwortliche  
**Version:** v0.34 (Basis v0.33)  
**Status:** In Prüfung – Hardwaretest ausstehend  
**Ablageort:** Uart_ESP32/docs/2026-10-10 - Uart_ESP32 - Projektdokumentation - NVS-Befehle - v0.34.pdf und .docx  
**Referenz:** Regeln_Projektdokumentation_PDF_DOCX_Pflicht_.pdf; Firmware v0.33  

## Ziel
Die Webkonsole der UART_ESP32-Firmware erhält acht benannte, dauerhaft gespeicherte UART-Befehle. Name und Befehlsinhalt bleiben bei einem Neustart des ESP32 erhalten.

## Bedienung
Unter „UART Konsole“ im Bereich „Gespeicherte Befehle (NVS)“ einen Speicherplatz (0–7) auswählen, Namen und Befehl beziehungsweise mehrzeiligen Kommandoblock eingeben und im NVS speichern. Gespeicherte Befehle können manuell gesendet und Einträge gelöscht werden.

## Technische Umsetzung
ESP32 Preferences-Namespace „uartcmd“; je Speicherplatz „n0“ bis „n7“ für den Namen und „c0“ bis „c7“ für den Befehl. Maximal 64 UTF-8-Bytes pro Name und 1024 UTF-8-Bytes pro Befehl. Einträge sind nicht verschlüsselt – daher keine Passwörter oder andere Geheimnisse speichern.

## UART-Übertragung
Manueller Versand über die vorhandene Blocksendefunktion mit gewähltem Zeilenabschluss, Teilpaketen und bestehenden UART-TX-Sperren. Nach ESP32-Neustart erfolgt kein automatischer Versand.

## HTTP-Schnittstellen
GET /uart/console/presets: acht Speicherplätze abrufen. POST /uart/console/presets: Name und Befehl für einen Speicherplatz speichern. POST /uart/console/presets/delete: Speicherplatz löschen. Ungültige Speicherplätze und Daten werden abgewiesen.

## Prüfung und Grenzen
JavaScript-Syntax der eingebetteten Skripte wurde mit node --check geprüft. Vollständiger Firmware-Build sowie ESP32- und BX3-Hardwaretest stehen noch aus. Für die Abnahme sind insbesondere Speichern, Aktualisieren, Neustart, Löschen, UART-Sendesperre und mehrzeilige Befehle zu prüfen.

## Paketstruktur und Bereitstellung
Vollständiges Projekt-ZIP und separates Änderungs-ZIP verwenden beide den unveränderten Root-Ordner Uart_ESP32/. Neue oder geänderte Dateien liegen an den projektüblichen Pfaden. Die englische Commit-Nachricht wird zusätzlich als separate TXT-Datei bereitgestellt.

## Änderungshistorie
v0.34 – 2026-10-10 – In Prüfung – Benannte NVS-Befehle ergänzt
