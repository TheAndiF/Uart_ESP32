# Uart_Esp32 v0.13 - Automatischer UART-Probe-Runner

## Ziel

Erweiterung des vorhandenen st10-Decoders um einen automatischen aktiven Test der 1000 UART-Testkandidaten.

## Ablauf

- Decoder-Modus und gueltiger TX-GPIO sind Voraussetzung.
- Vor Beginn: 2 s D2-Baseline.
- Testreihenfolge: P0, P1, P2, P3.
- Jeder Kandidat wird 5 s lang getestet.
- Derselbe Frame wird alle 250 ms gesendet, also typischerweise 20-mal.
- RX/Decoder laufen parallel weiter.
- Zwischen Kandidaten liegen 500 ms Pause.
- Wahlweise Stop beim ersten Treffer oder kompletter Durchlauf.

## Erkennung

Der Runner markiert als Auffaelligkeit:

- einen zuvor nicht beobachteten Command,
- geaenderte Meta-/Statusbytes,
- deutliche Aenderungen dekodierter 16-Bit-Werte (Schwelle 30 Rohwerte),
- deutliche Abweichungen der bekannten Command-Raten von der 2-s-Baseline,
- mindestens 500 ms Aussetzen gueltiger D2-Telemetrie bei zuvor aktiver Baseline.

Alle Ereignisse werden ueber UART0/Serial mit `[PROBE]` ausgegeben.

## Kandidatenkatalog

`TestCandidateCatalog.cpp/.h` erzeugt alle 1000 Katalogeintraege exakt aus den Familien A/B/C/D. Die Katalognummern und IDs bleiben erhalten. Die D-Familie behaelt ihre historischen SLOW/INIT-IDs; beim neuen automatischen Runner werden jedoch beide mit dem einheitlichen 250-ms/5-s-Schema getestet.

## Sicherheit

Aktive Tests nur am gesicherten Pruefstand ausfuehren. Unbekannte formal gueltige Befehle koennen reale Funktionen/Aktoren ausloesen. Gemeinsame Masse, Pegel und Bus-Topologie vor TX-Verbindung pruefen.
