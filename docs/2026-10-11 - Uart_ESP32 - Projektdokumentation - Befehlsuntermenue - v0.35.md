# Uart_ESP32 - Projektdokumentation - Befehlsuntermenue - v0.35

Projekt: Uart_ESP32 | Version: v0.35 | Datum: 2026-10-11 | Status: In Prüfung | Verantwortlich: Projektentwicklung / ChatGPT | Ablageort: Uart_ESP32/docs/

## Ziel und Umsetzung
Die acht bereits vorhandenen NVS-Befehlsspeicher befinden sich ab v0.35 auf einer eigenständigen Seite /uart/presets. Der Menüpunkt „Gespeicherte Befehle“ befindet sich direkt unter „UART Konsole“. Die Live-Konsole zeigt keine Befehlsverwaltung mehr.

## NVS-Speicherablauf und Datenbeständigkeit
Die bestehenden NVS-Schlüssel im Namespace uartcmd bleiben unverändert: n0 bis n7 für Namen, c0 bis c7 für Befehle. Dadurch bleiben die unter v0.34 gespeicherten Werte lesbar. Maximal 64 UTF-8-Bytes je Name und 1024 UTF-8-Bytes je Befehlsblock. Beim Schreiben werden die Werte aus NVS zurückgelesen und verglichen. Bei Fehlern versucht der Server, den vorherigen Zustand wiederherzustellen. Da Name und Kommando weiterhin in zwei NVS-Schlüsseln liegen, ist eine vollständig atomare Transaktion nicht garantiert.

## Mehrzeilige Befehle
Der Browser überträgt den Text unverändert per URLSearchParams; der ESP32 speichert den Befehlsinhalt unverändert im NVS (einschließlich CR, LF oder CRLF). Nach dem Speichern liest der Browser den vollständigen Datensatz zurück und vergleicht ihn bytegenau nach JavaScript-Stringwerten. Erst beim Senden werden Zeilenenden einheitlich auf CR, LF oder CRLF umgesetzt; die Option „Keiner“ verwendet LF zwischen Zeilen und fügt am Blockende keinen Abschluss hinzu. Längere Texte werden an UTF-8-Zeichengrenzen in höchstens 480-Byte-Teile aufgeteilt, mit ca. 90 ms Pause übertragen und über den bestehenden, gegen parallele UART-Verwendung abgesicherten Sendeendpunkt gesendet.

## Bedienung
Speicherplatz wählen, Namen und Befehlsblock eingeben, „Im NVS speichern“ anklicken. Erfolgreiches Speichern wird erst nach dem erneuten Laden angezeigt. „Gespeicherten Befehl senden“ verschickt ausschließlich die gespeicherte Version, nicht ungespeicherte Änderungen. Ein NVS-Löschvorgang erfordert eine Bestätigung. Keine automatische Ausführung beim ESP32-Neustart.

## Prüfung und Grenzen
JavaScript-Syntax der sechs eingebetteten Browser-Skripte per node --check geprüft. Test der CR-Umsetzung, abschließender Zeilenumbrüche sowie UTF-8-gerechter 480-Byte-Aufteilung bestanden. Strukturkontrolle der ZIP-Pakete erfolgt bei Auslieferung. PlatformIO-Firmwarebuild und reale NVS-/UART-Hardwaretests stehen aus. Eine vollständige atomare NVS-Transaktion über beide Schlüssel ist nicht implementiert.

## Änderungshistorie
v0.34 · 2026-10-10: Benannte NVS-Befehle innerhalb der Konsole.
v0.35 · 2026-10-11: Eigenes Menü, NVS-Verifikation und eigenständige Zeilenabschluss-Auswahl.