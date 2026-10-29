# ESP32 UART Web Console

**A browser-based UART console and device-management toolkit for ESP32-WROOM-32 / NodeMCU-32S.**

Connect to a serial device from your browser, inspect incoming data, send commands, and transfer files. The firmware also provides Wi-Fi configuration, MQTT monitoring, OTA updates, and power-management features. It was developed with a **Marstek BX3** use case in mind, while several UART functions are useful for other 3.3 V TTL serial devices.

> **Project status:** Actively developed, hardware-oriented project. Some functions (notably BX3 protocol decoding, probes and shell-based file transfer) are device-specific. Check compatibility and safety notes before connecting hardware.

**[Deutsch](#deutsch--kurzüberblick)** · [Getting started](#getting-started) · [Features](#features) · [Technical documentation](docs/README_technical_history.md) · [Project documents](docs/)

## Features

| Feature | Description |
| --- | --- |
| Web UART console | Bidirectional text and HEX I/O, line endings, control keys, scrollback, and multiline commands |
| Saved commands | Reuse command blocks stored in ESP32 NVS |
| File transfers | Browser-to-device upload and device-to-browser download through a compatible BX3/Linux shell |
| Windows PowerShell client | Optional paired interactive console, transfer commands, status and heartbeat (`tools/UartEsp32.ps1`) |
| UART diagnostics | Raw data view, device-specific decoder and experimental probe runner |
| Connectivity | Wi-Fi setup and recovery AP, MQTT status and control |
| Maintenance | Browser OTA, ArduinoOTA / PlatformIO OTA and reboot controls |
| Power | Battery ADC monitoring and configurable deep sleep |

### Screenshots and hardware photos

**Real screenshots and wiring photos are welcome.** The project does not yet include a verified screenshot gallery. Until images are available, see the [technical documentation](docs/README_technical_history.md) for the interface routes and capabilities. Do not use an example photograph as a wiring reference.

## Getting started

### Requirements

- ESP32-WROOM-32 / NodeMCU-32S (the currently configured PlatformIO board is `nodemcu-32s`)
- [PlatformIO](https://platformio.org/) and a suitable USB connection for the first flash
- A compatible **3.3 V TTL UART** target and verified UART pinout/voltage levels
- A computer or phone on the same trusted network for the web interface

### Build and flash

```bash
git clone https://github.com/TheAndiF/Uart_ESP32.git
cd Uart_ESP32
pio run -e uart_esp32_usb
pio run -e uart_esp32_usb -t upload
```

These commands are the documented PlatformIO workflow, **not a claim that this revision was hardware-tested**. Select the correct USB port and verify your board before flashing.

Optionally copy `include/arduino_secrets.example.h` to `include/arduino_secrets.h` and configure credentials locally. Never commit real passwords. Most settings can also be configured via the web interface.

### First connection

1. Power the ESP32 and connect to its configured Wi-Fi network.
2. With no valid Wi-Fi credentials, the setup AP normally uses SSID `Uart_Esp32-Setup` and address `http://192.168.4.1/`. By default this AP has **no password** unless configured.
3. Open the interface and configure **WLAN / NTP**; after connection, open the ESP32's assigned IP address.
4. Verify the target UART voltage and pin mapping **before enabling TX**. Configure UART settings and open the web console.

For Wi-Fi fallback details, UART endpoint reference, MQTT topics, OTA, and configuration precedence, see the [complete technical guide](docs/README_technical_history.md).

## Safety and limitations

- **3.3 V TTL UART only:** ESP32 GPIO pins are not 5 V tolerant. RS-232 and RS-485 need appropriate transceivers. Verify the target voltage, connect ground appropriately, and do not connect BX3 VCC to ESP32 power.
- The web interface has **no built-in TLS/login layer**. Use it only on a trusted and isolated network; never expose it directly to the public internet.
- Some file-transfer operations require a supported BX3/Linux shell and utilities such as `base64`, `cksum` and `dd`; they are **not generic binary UART transfers**.
- Experimental probe or transmit features can affect the connected device. Keep TX disabled until the electrical interface and commands are understood.

## Documentation

- [Complete original README / development and technical history](docs/README_technical_history.md) — retained without shortening its contents
- [Project documentation](docs/) — versioned technical PDF, DOCX, Markdown and change notes
- [Validation notes](docs/validation/) — version-specific checks, not a substitute for hardware testing
- [PowerShell client](tools/UartEsp32.ps1)
- [PlatformIO configuration](platformio.ini)

## Deutsch – Kurzüberblick

**UART-Webkonsole und Geräteverwaltung auf Basis eines ESP32-WROOM-32 / NodeMCU-32S.** Die Firmware bietet eine bidirektionale UART-Konsole im Browser, gespeicherte Kommandos, BX3-/Linux-spezifische Dateiübertragung, eine optionale PowerShell-Anbindung, UART-Diagnose, WLAN, MQTT, OTA und Energiemanagement.

**Schnellstart:** Projekt mit PlatformIO in der Umgebung `uart_esp32_usb` bauen und flashen. Ohne gültige WLAN-Zugangsdaten startet normalerweise `Uart_Esp32-Setup` unter `http://192.168.4.1/`. Vor TX-Freigabe unbedingt die UART-Pegel und Pinbelegung prüfen.

Die **[vollständige deutschsprachige technische Dokumentation](docs/README_technical_history.md)** einschließlich der bisherigen Versionshistorie bleibt erhalten.

## Contributing and license

Feedback, reproducible bug reports and documentation improvements are welcome through [GitHub Issues](https://github.com/TheAndiF/Uart_ESP32/issues). Please include the ESP32 board, firmware revision, target hardware and relevant logs without credentials.

**License:** No repository license has been selected yet. Until the author adds one, do not assume open-source reuse permissions beyond applicable law. Contributions and licensing should be clarified before wider distribution.
