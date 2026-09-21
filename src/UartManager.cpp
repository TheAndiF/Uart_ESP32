#include "UartManager.h"
#include <math.h>
#include <mbedtls/base64.h>

static bool prefHas(Preferences& p, const char* key) { return p.isKey(key); }

void UartManager::load() {
  Preferences p;
  p.begin("uartconf", true);

  // v0.15 migration: older firmware stored one exclusive mode (Off/Raw/
  // Decode/Console). Any previously active mode becomes one enabled common
  // UART transport. TX remains deliberately locked after migration until the
  // user explicitly enables it in UART Einstellungen.
  const uint8_t legacyMode = prefHas(p, "mode") ? p.getUChar("mode", 0) : 0;
  _enabled = prefHas(p, "enabled") ? p.getBool("enabled", legacyMode != 0) : (legacyMode != 0);
  _txEnabled = prefHas(p, "tx_enabled") ? p.getBool("tx_enabled", false) : false;

  _uartNumber = prefHas(p, "uartno") ? p.getUChar("uartno", 2) : 2;
  if (_uartNumber != 1 && _uartNumber != 2) _uartNumber = 2;
  _rxPin = prefHas(p, "rxpin") ? p.getInt("rxpin", 16) : 16;
  _txPin = prefHas(p, "txpin") ? p.getInt("txpin", -1) : -1;
  _baud = prefHas(p, "baud") ? p.getUInt("baud", 115200U) : 115200U;
  _frame = prefHas(p, "frame") ? p.getString("frame", "8N1") : String("8N1");
  setFrame(_frame);
  _deadband = prefHas(p, "deadband") ? p.getFloat("deadband", 0.03f) : 0.03f;
  if (!isfinite(_deadband) || _deadband < 0.0f || _deadband > 0.5f) _deadband = 0.03f;

  for (uint8_t i = 0; i < 6; ++i) {
    String key = "lbl" + String(i + 1);
    _aliases[i] = p.isKey(key.c_str()) ? p.getString(key.c_str(), "") : String();
  }
  for (uint8_t i = 0; i < 5; ++i) {
    String kMin = "min" + String(i + 1);
    String kCtr = "ctr" + String(i + 1);
    String kMax = "max" + String(i + 1);
    if (p.isKey(kMin.c_str())) _cal[i].minV = p.getUShort(kMin.c_str(), _cal[i].minV);
    if (p.isKey(kCtr.c_str())) _cal[i].centerV = p.getUShort(kCtr.c_str(), _cal[i].centerV);
    if (p.isKey(kMax.c_str())) _cal[i].maxV = p.getUShort(kMax.c_str(), _cal[i].maxV);
    if (!(_cal[i].minV < _cal[i].centerV && _cal[i].centerV < _cal[i].maxV)) {
      static const Calibration defaults[5] = {
        {1000,1503,2000}, {1000,1486,2000}, {1000,1493,2000}, {1006,1511,2000}, {1000,1498,2000}
      };
      _cal[i] = defaults[i];
    }
  }
  p.end();
}

void UartManager::save() {
  Preferences p;
  p.begin("uartconf", false);
  p.putBool("enabled", _enabled);
  p.putBool("tx_enabled", _txEnabled);
  // Keep a small compatibility marker for older firmware revisions. Raw mode
  // is the least surprising fallback because v0.15 no longer has modes.
  p.putUChar("mode", _enabled ? 1U : 0U);
  p.putUChar("uartno", _uartNumber);
  p.putInt("rxpin", _rxPin);
  p.putInt("txpin", _txPin);
  p.putUInt("baud", _baud);
  p.putString("frame", _frame);
  p.putFloat("deadband", _deadband);
  for (uint8_t i = 0; i < 6; ++i) {
    String key = "lbl" + String(i + 1);
    p.putString(key.c_str(), _aliases[i]);
  }
  for (uint8_t i = 0; i < 5; ++i) {
    String kMin = "min" + String(i + 1);
    String kCtr = "ctr" + String(i + 1);
    String kMax = "max" + String(i + 1);
    p.putUShort(kMin.c_str(), _cal[i].minV);
    p.putUShort(kCtr.c_str(), _cal[i].centerV);
    p.putUShort(kMax.c_str(), _cal[i].maxV);
  }
  p.end();
}

void UartManager::begin() {
  load();
  restart();
}

bool UartManager::start() {
  if (_running) return true;
  if (!_enabled) {
    _status = "deaktiviert - UART Basisbetrieb ist ausgeschaltet";
    return false;
  }
  if (!validPins()) {
    _status = "ungueltige GPIO-Konfiguration";
    return false;
  }
  if (_baud < 300U || _baud > 2000000U) {
    _status = "ungueltige Baudrate";
    return false;
  }

  resetParser();
  _hasLastMainPacketCopy = false;
  _txActive = false;
  _txPacketCount = 0;
  _txStatus = !_txEnabled ? String("TX gesperrt") :
              (_txPin >= 0 ? String("TX freigegeben - warte auf gueltiges 0x0021-Paket") : String("TX freigegeben, aber kein TX GPIO konfiguriert"));

  _serial = (_uartNumber == 1) ? &_uart1 : &_uart2;
  // Important safety property: when TX is locked, HardwareSerial is started
  // with TX=-1 so the configured GPIO is not driven by the UART peripheral.
  const int activeTxPin = _txEnabled ? _txPin : -1;
  _serial->begin(_baud, serialConfig(), _rxPin, activeTxPin);
  _running = true;
  _status = "UART aktiv auf UART" + String(_uartNumber) + ", RX GPIO" + String(_rxPin);
  if (_txEnabled && _txPin >= 0) _status += ", TX GPIO" + String(_txPin) + " freigegeben";
  else if (_txPin >= 0) _status += ", TX GPIO" + String(_txPin) + " gesperrt";
  else _status += ", RX-only";
  _status += ", " + String(_baud) + " " + _frame;
  Serial.printf("[UART] %s\n", _status.c_str());
  return true;
}

void UartManager::stop() {
  if (imageActive()) abortImageTransfer("UART gestoppt");
  _txActive = false;
  if (_probeState != ProbeState::Idle) stopProbeSweep();
  if (_serial) {
    _serial->end();
    delay(5);
  }
  _serial = nullptr;
  _running = false;
  resetParser();
  if (_txStatus.startsWith("sendet")) _txStatus = "gestoppt";
  _status = _enabled ? String("UART gestoppt") : String("deaktiviert");
  Serial.printf("[UART] %s\n", _status.c_str());
}

bool UartManager::restart() {
  stop();
  if (!_enabled) {
    _status = "deaktiviert";
    return true;
  }
  return start();
}

void UartManager::loop() {
  if (!_running || !_serial) return;
  int budget = 512;
  while (_serial->available() > 0 && budget-- > 0) {
    const int value = _serial->read();
    if (value < 0) break;
    const uint8_t b = (uint8_t)value;

    // Normal operation fans RX out to console/raw/decoder. During image transfer
    // RX is owned by the image parser so base64 payload cannot pollute decoder
    // state or the interactive console.
    if (imageActive()) {
      processImageByte(b);
    } else {
      pushRaw(b);
      pushConsole(b);
      processDecoderByte(b);
    }
    ++_totalBytes;
  }

  if (imageActive() && _imageState != ImageState::BlockReady &&
      (unsigned long)(millis() - _imageLastRxMillis) > 15000UL) {
    retryImageBlock("UART Timeout");
  }
  if (!imageActive()) {
    serviceTxReplay();
    serviceProbe();
  }
}

String UartManager::modeText() const {
  return _enabled ? String("UART Basisbetrieb") : String("Aus");
}

String UartManager::txOwnerText() const {
  if (!_txEnabled) return "gesperrt";
  if (!_running || !_serial) return "UART gestoppt";
  if (_txPin < 0 || !validTxPin(_txPin)) return "kein gueltiger TX GPIO";
  if (imageActive()) return "UART Image-Transfer";
  if (_probeState != ProbeState::Idle) return "UART Probe-Runner";
  if (_txActive) return "Decoder Feld-5-Replay";
  return "frei";
}

bool UartManager::validRxPin(int pin) const {
  if (pin < 0 || pin > 39) return false;
  if (pin >= 6 && pin <= 11) return false; // flash pins
  if (pin == 1 || pin == 3) return false;  // console UART0 stays reserved
  return true;
}

bool UartManager::validTxPin(int pin) const {
  if (pin == -1) return true;
  if (!validRxPin(pin)) return false;
  if (pin >= 34 && pin <= 39) return false; // input-only
  return true;
}

bool UartManager::validPins() const {
  return validRxPin(_rxPin) && validTxPin(_txPin) && (_txPin < 0 || _txPin != _rxPin);
}

void UartManager::setFrame(const String& value) {
  String v = value;
  v.toUpperCase();
  if (v == "8E1" || v == "8O1" || v == "8N2") _frame = v;
  else _frame = "8N1";
}

uint32_t UartManager::serialConfig() const {
  if (_frame == "8E1") return SERIAL_8E1;
  if (_frame == "8O1") return SERIAL_8O1;
  if (_frame == "8N2") return SERIAL_8N2;
  return SERIAL_8N1;
}

void UartManager::clearRaw() {
  _rawHead = 0;
  _rawCount = 0;
  _totalBytes = 0;
}

void UartManager::pushRaw(uint8_t value) {
  _raw[_rawHead] = value;
  _rawHead = (_rawHead + 1U) % RAW_CAPACITY;
  if (_rawCount < RAW_CAPACITY) ++_rawCount;
}

void UartManager::pushConsole(uint8_t value) {
  _console[_consoleHead] = value;
  _consoleHead = (_consoleHead + 1U) % CONSOLE_CAPACITY;
  if (_consoleCount < CONSOLE_CAPACITY) ++_consoleCount;
  ++_consoleSequence;
}

bool UartManager::baseTxReady() const {
  return _running && _serial && _txEnabled && _txPin >= 0 && validTxPin(_txPin);
}

bool UartManager::consoleTxReady() const {
  return baseTxReady() && !imageActive() && _probeState == ProbeState::Idle && !_txActive;
}

size_t UartManager::consoleWrite(const uint8_t* data, size_t length) {
  if (!consoleTxReady() || !data || !length) return 0;
  const size_t written = _serial->write(data, length);
  _consoleTxBytes += (uint32_t)written;
  return written;
}

void UartManager::clearConsole() {
  _consoleHead = 0;
  _consoleCount = 0;
}

String UartManager::consoleChunkJson(uint32_t sinceSequence) const {
  const uint32_t current = _consoleSequence;
  const uint32_t oldest = current - (uint32_t)_consoleCount;
  bool truncated = false;

  if (sinceSequence < oldest || sinceSequence > current) {
    sinceSequence = oldest;
    truncated = true;
  }

  size_t skip = (size_t)(sinceSequence - oldest);
  if (skip > _consoleCount) skip = _consoleCount;
  const size_t count = _consoleCount - skip;
  const size_t oldestIndex = (_consoleHead + CONSOLE_CAPACITY - _consoleCount) % CONSOLE_CAPACITY;
  size_t pos = (oldestIndex + skip) % CONSOLE_CAPACITY;

  String text;
  String hex;
  text.reserve(count + count / 8U + 16U);
  hex.reserve(count * 2U + 1U);
  char hexEscape[7];
  char hexByte[3];
  for (size_t i = 0; i < count; ++i) {
    const uint8_t b = _console[pos];
    pos = (pos + 1U) % CONSOLE_CAPACITY;
    snprintf(hexByte, sizeof(hexByte), "%02X", (unsigned)b);
    hex += hexByte;
    switch (b) {
      case '\\': text += "\\\\"; break;
      case '"': text += "\\\""; break;
      case '\n': text += "\\n"; break;
      case '\r': text += "\\r"; break;
      case '\t': text += "\\t"; break;
      default:
        if (b >= 32U && b <= 126U) text += (char)b;
        else {
          snprintf(hexEscape, sizeof(hexEscape), "\\u%04X", (unsigned)b);
          text += hexEscape;
        }
        break;
    }
  }

  String j;
  j.reserve(text.length() + hex.length() + 340U);
  j += "{\"mode\":\"" + jsonEscape(modeText()) + "\",\"running\":" + String(_running ? "true" : "false");
  j += ",\"tx_enabled\":" + String(_txEnabled ? "true" : "false");
  j += ",\"tx_ready\":" + String(consoleTxReady() ? "true" : "false");
  j += ",\"tx_owner\":\"" + jsonEscape(txOwnerText()) + "\"";
  j += ",\"sequence\":" + String(current) + ",\"oldest\":" + String(oldest);
  j += ",\"truncated\":" + String(truncated ? "true" : "false");
  j += ",\"rx_bytes\":" + String((unsigned long)(_totalBytes & 0xFFFFFFFFULL));
  j += ",\"tx_bytes\":" + String(_consoleTxBytes);
  j += ",\"text\":\"" + text + "\",\"hex\":\"" + hex + "\"}";
  return j;
}

String UartManager::rawHex(size_t maxBytes) const {
  size_t count = _rawCount < maxBytes ? _rawCount : maxBytes;
  if (!count) return "";
  size_t start = (_rawHead + RAW_CAPACITY - count) % RAW_CAPACITY;
  String out;
  out.reserve(count * 3U + count / 16U + 8U);
  char buf[4];
  for (size_t i = 0; i < count; ++i) {
    uint8_t b = _raw[(start + i) % RAW_CAPACITY];
    snprintf(buf, sizeof(buf), "%02X", b);
    out += buf;
    if ((i + 1U) % 16U == 0U) out += '\n';
    else if (i + 1U < count) out += ' ';
  }
  return out;
}

String UartManager::rawAscii(size_t maxBytes) const {
  size_t count = _rawCount < maxBytes ? _rawCount : maxBytes;
  size_t start = (_rawHead + RAW_CAPACITY - count) % RAW_CAPACITY;
  String out;
  out.reserve(count + count / 64U + 8U);
  for (size_t i = 0; i < count; ++i) {
    uint8_t b = _raw[(start + i) % RAW_CAPACITY];
    out += (b >= 32 && b <= 126) ? (char)b : '.';
    if ((i + 1U) % 64U == 0U) out += '\n';
  }
  return out;
}

void UartManager::resetParser() {
  _packetPos = 0;
  _expectedPacketLength = 0;
  _syncState = 0;
}

void UartManager::processDecoderByte(uint8_t value) {
  if (_packetPos == 0) {
    if (_syncState == 0) {
      if (value == 0xFF) _syncState = 1;
      return;
    }
    if (_syncState == 1) {
      if (value == 0xFB) {
        _packet[0] = 0xFF;
        _packet[1] = 0xFB;
        _packetPos = 2;
        _syncState = 0;
      } else {
        _syncState = (value == 0xFF) ? 1 : 0;
      }
      return;
    }
  }

  if (_packetPos >= PACKET_CAPACITY) {
    ++_invalidPacketCount;
    resetParser();
    return;
  }

  _packet[_packetPos++] = value;
  if (_packetPos == 4) {
    _expectedPacketLength = 4U + (size_t)_packet[3];
    if (_expectedPacketLength < 7U || _expectedPacketLength > PACKET_CAPACITY) {
      ++_invalidPacketCount;
      resetParser();
      return;
    }
  }

  if (_expectedPacketLength && _packetPos == _expectedPacketLength) {
    handlePacket(_expectedPacketLength);
    resetParser();
  }
}

void UartManager::handlePacket(size_t packetLength) {
  ++_packetCount;
  _lastPacketMillis = millis();
  _lastRoute = packetLength >= 3U ? _packet[2] : 0;
  _lastOuterLength = packetLength >= 4U ? _packet[3] : 0;
  _lastInnerType = 0;
  _lastInnerLength = 0;
  _lastCommand = 0;
  _lastDataLength = 0;
  _lastChecksumOk = false;

  // st10: FF FB route outer_length [body with exactly outer_length bytes]
  if (packetLength < 11U || _lastOuterLength + 4U != packetLength) {
    ++_invalidPacketCount;
    return;
  }

  // Inner frame must start with FF FD or FF FE.
  if (_packet[4] != 0xFF || (_packet[5] != 0xFD && _packet[5] != 0xFE)) {
    ++_invalidPacketCount;
    return;
  }
  _lastInnerType = _packet[5];

  _lastInnerLength = (uint16_t)_packet[6] | ((uint16_t)_packet[7] << 8U);
  if (_lastInnerLength < 3U || (uint16_t)(_lastInnerLength + 4U) != _lastOuterLength) {
    ++_invalidPacketCount;
    return;
  }

  _lastCommand = (uint16_t)_packet[8] | ((uint16_t)_packet[9] << 8U);
  const uint16_t seenByte = (uint16_t)(_lastCommand >> 3U);
  const uint8_t seenMask = (uint8_t)(1U << (_lastCommand & 7U));
  const bool commandSeenBefore = (_seenCommandBits[seenByte] & seenMask) != 0U;
  _seenCommandBits[seenByte] |= seenMask;
  _lastDataLength = (uint16_t)(_lastInnerLength - 3U);
  const size_t expectedDataEnd = 10U + (size_t)_lastDataLength;
  if (expectedDataEnd + 1U != packetLength) {
    ++_invalidPacketCount;
    return;
  }

  // st10 checksum: XOR from InnerLength low byte through the final data byte.
  uint8_t cs = 0;
  for (size_t i = 6U; i + 1U < packetLength; ++i) cs ^= _packet[i];
  _lastChecksumOk = (cs == _packet[packetLength - 1U]);
  if (!_lastChecksumOk) {
    ++_invalidPacketCount;
    return;
  }

  ++_validPacketCount;
  const uint8_t* data = &_packet[10];

  if (_lastCommand == 0x0021 && _lastDataLength == 14U && packetLength == 25U) {
    ++_cmd0021Count;
    _hasMainPacket = true;
    memcpy(_lastMainPacket, _packet, 25U);
    const bool firstFreshTemplate = !_hasLastMainPacketCopy;
    _hasLastMainPacketCopy = true;
    if (firstFreshTemplate && !_txActive) {
      _txStatus = !_txEnabled ? String("TX gesperrt") : ((_txPin >= 0) ? String("bereit: gueltiges 0x0021-Paket vorhanden") : String("nicht bereit: TX GPIO ist deaktiviert"));
    }
    // 0x0021 DATA: 2 meta bytes, then six uint16 little-endian fields.
    _last0021Meta[0] = data[0];
    _last0021Meta[1] = data[1];
    for (uint8_t i = 0; i < 6; ++i) {
      const size_t pos = 2U + 2U * i;
      _fields[i] = (uint16_t)data[pos] | ((uint16_t)data[pos + 1U] << 8U);
    }
    for (uint8_t i = 0; i < 5; ++i) {
      _normalized[i] = applyDeadband(normalize(_fields[i], _cal[i]));
    }
  }
  else if (_lastCommand == 0x0023 && _lastDataLength == 11U) {
    ++_cmd0023Count;
    _has0023 = true;
    _cmd0023Value1 = (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
    _cmd0023Value2 = data[2];
    _cmd0023Value3 = (uint16_t)data[3] | ((uint16_t)data[4] << 8U);
  }
  else if (_lastCommand == 0x0031 && _lastDataLength == 3U) {
    ++_cmd0031Count;
    _has0031 = true;
    _cmd0031Value = (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
    _cmd0031Status = data[2];
  }
  else if (_lastCommand == 0x0033 && _lastDataLength == 14U) {
    ++_cmd0033Count;
    _has0033 = true;
    // st10: same structural form as 0x0021 (2 meta bytes + 6 x uint16 LE),
    // but semantic ordering remains intentionally unnamed/open.
    _last0033Meta[0] = data[0];
    _last0033Meta[1] = data[1];
    for (uint8_t i = 0; i < 6; ++i) {
      const size_t pos = 2U + 2U * i;
      _cmd0033Fields[i] = (uint16_t)data[pos] | ((uint16_t)data[pos + 1U] << 8U);
    }
  }
  else {
    ++_unknownCommandCount;
  }

  observeProbeFrame(_lastCommand, data, _lastDataLength, commandSeenBefore);
}

float UartManager::normalize(uint16_t raw, const Calibration& c) const {
  if (!(c.minV < c.centerV && c.centerV < c.maxV)) return 0.0f;
  float value;
  if (raw < c.centerV) value = (float)((int32_t)raw - (int32_t)c.centerV) / (float)((int32_t)c.centerV - (int32_t)c.minV);
  else value = (float)((int32_t)raw - (int32_t)c.centerV) / (float)((int32_t)c.maxV - (int32_t)c.centerV);
  if (value < -1.0f) value = -1.0f;
  if (value > 1.0f) value = 1.0f;
  return value;
}

float UartManager::applyDeadband(float value) const {
  const float a = fabsf(value);
  if (a <= _deadband) return 0.0f;
  if (_deadband >= 0.999f) return 0.0f;
  const float sign = value < 0.0f ? -1.0f : 1.0f;
  return sign * (a - _deadband) / (1.0f - _deadband);
}

uint16_t UartManager::denormalize(float value, const Calibration& c) const {
  if (value < -1.0f) value = -1.0f;
  if (value > 1.0f) value = 1.0f;
  float raw;
  if (value < 0.0f) raw = (float)c.centerV + value * (float)((int32_t)c.centerV - (int32_t)c.minV);
  else raw = (float)c.centerV + value * (float)((int32_t)c.maxV - (int32_t)c.centerV);
  long rounded = lroundf(raw);
  if (rounded < 0) rounded = 0;
  if (rounded > 65535) rounded = 65535;
  return (uint16_t)rounded;
}

void UartManager::updatePacketChecksum(uint8_t* packet, size_t packetLength) const {
  if (!packet || packetLength < 7U) return;
  uint8_t cs = 0;
  for (size_t i = 6; i + 1U < packetLength; ++i) cs ^= packet[i];
  packet[packetLength - 1U] = cs;
}

bool UartManager::txReady() const {
  return baseTxReady() && !imageActive() && _hasLastMainPacketCopy && _probeState == ProbeState::Idle && !_txActive;
}

bool UartManager::sendField5ForOneSecond(float normalizedValue) {
  if (imageActive()) {
    _txStatus = "nicht bereit: UART Image-Transfer ist aktiv";
    return false;
  }
  if (_probeState != ProbeState::Idle) {
    _txStatus = "nicht bereit: automatischer Probe-Runner ist aktiv";
    return false;
  }
  if (_txActive) {
    _txStatus = "Senden bereits aktiv";
    return false;
  }
  if (!_running || !_serial) {
    _txStatus = "nicht bereit: UART ist gestoppt";
    return false;
  }
  if (!_txEnabled) {
    _txStatus = "nicht bereit: TX ist in UART Einstellungen gesperrt";
    return false;
  }
  if (_txPin < 0 || !validTxPin(_txPin)) {
    _txStatus = "nicht bereit: gueltiger TX GPIO fehlt";
    return false;
  }
  if (!_hasLastMainPacketCopy) {
    _txStatus = "nicht bereit: noch kein gueltiges 0x0021-Paket empfangen";
    return false;
  }
  if (!isfinite(normalizedValue) || normalizedValue < -1.0f || normalizedValue > 1.0f) {
    _txStatus = "ungueltiger normierter Zielwert";
    return false;
  }

  memcpy(_txPacket, _lastMainPacket, 25U);
  _txTargetNormalized = normalizedValue;
  _txField5Raw = denormalize(normalizedValue, _cal[4]);
  _txPacket[20] = (uint8_t)(_txField5Raw & 0xFFU);
  _txPacket[21] = (uint8_t)((_txField5Raw >> 8U) & 0xFFU);
  updatePacketChecksum(_txPacket, 25U);

  _txPacketCount = 0;
  _txStartMillis = millis();
  _txNextMillis = _txStartMillis;
  _txActive = true;
  _txStatus = "sendet Feld 5 = " + String(_txTargetNormalized, 2) + " fuer ca. 1 s bei 50 Hz";
  Serial.printf("[UART-TX] Start: Feld 5 norm=%.2f raw=%u, TX GPIO%d, 50 Hz, 1 s\n",
                _txTargetNormalized, (unsigned)_txField5Raw, _txPin);
  return true;
}

void UartManager::serviceTxReplay() {
  if (!_txActive || !_serial) return;
  const unsigned long now = millis();
  if ((unsigned long)(now - _txStartMillis) >= 1000UL) {
    _txActive = false;
    _txStatus = "fertig: " + String(_txPacketCount) + " Pakete gesendet";
    Serial.printf("[UART-TX] Fertig: %lu Pakete\n", (unsigned long)_txPacketCount);
    return;
  }

  if ((long)(now - _txNextMillis) >= 0) {
    const size_t written = _serial->write(_txPacket, 25U);
    if (written != 25U) {
      _txActive = false;
      _txStatus = "Fehler: UART-TX konnte Paket nicht vollstaendig schreiben";
      Serial.printf("[UART-TX] Fehler: nur %u/25 Bytes geschrieben\n", (unsigned)written);
      return;
    }
    ++_txPacketCount;
    _txNextMillis += 20UL;
    if ((long)(now - _txNextMillis) > 100L) _txNextMillis = now + 20UL;
  }
}

namespace {
constexpr unsigned long PROBE_BASELINE_MS = 2000UL;
constexpr unsigned long PROBE_CANDIDATE_MS = 5000UL;
constexpr unsigned long PROBE_SEND_INTERVAL_MS = 250UL;
constexpr unsigned long PROBE_GAP_MS = 500UL;
constexpr unsigned long PROBE_PAUSE_DETECT_MS = 500UL;
constexpr uint16_t PROBE_VALUE_DELTA = 30U;
constexpr uint32_t PROBE_REACTION_NEW_COMMAND = 1U << 0;
constexpr uint32_t PROBE_REACTION_VALUE_CHANGE = 1U << 1;
constexpr uint32_t PROBE_REACTION_META_STATUS = 1U << 2;
constexpr uint32_t PROBE_REACTION_RATE_CHANGE = 1U << 3;
constexpr uint32_t PROBE_REACTION_PAUSE = 1U << 4;

static uint16_t absDelta16(uint16_t a, uint16_t b) {
  return a > b ? (uint16_t)(a - b) : (uint16_t)(b - a);
}
}

bool UartManager::startProbeSweep(bool stopOnReaction) {
  if (imageActive()) {
    _probeStatus = "nicht bereit: UART Image-Transfer ist aktiv";
    return false;
  }
  if (_probeState != ProbeState::Idle) {
    _probeStatus = "Probe-Runner laeuft bereits";
    return false;
  }
  if (_txActive) {
    _probeStatus = "nicht bereit: experimentelles Feld-5-Senden ist aktiv";
    return false;
  }
  if (!_running || !_serial) {
    _probeStatus = "nicht bereit: UART ist gestoppt";
    return false;
  }
  if (!_txEnabled) {
    _probeStatus = "nicht bereit: TX ist in UART Einstellungen gesperrt";
    return false;
  }
  if (_txPin < 0 || !validTxPin(_txPin)) {
    _probeStatus = "nicht bereit: gueltiger TX GPIO fehlt";
    return false;
  }

  _probeStopOnReaction = stopOnReaction;
  _probeOrderIndex = 0;
  _probeCompleted = 0;
  _probeCandidate = UartTestCandidate{};
  _probeSentCount = 0;
  _probeReactionFlags = 0;
  _probeFirstUnknownCommand = 0;
  _probeMaxFieldDelta = 0;
  _probeState = ProbeState::Baseline;
  _probeStateStartMillis = millis();
  _probeBaselineCounts[0] = _cmd0021Count;
  _probeBaselineCounts[1] = _cmd0023Count;
  _probeBaselineCounts[2] = _cmd0031Count;
  _probeBaselineCounts[3] = _cmd0033Count;
  _probeStatus = "Baseline wird 2 s aufgezeichnet";

  Serial.println();
  Serial.println("[PROBE] ================================================");
  Serial.printf("[PROBE] Automatischer Test gestartet: %u Kandidaten, 5 s/Kandidat, Senden alle 250 ms\n",
                (unsigned)TestCandidateCatalog::COUNT);
  Serial.printf("[PROBE] Reihenfolge: P0 -> P1 -> P2 -> P3; Treffer-Stopp: %s\n",
                _probeStopOnReaction ? "JA" : "NEIN");
  Serial.println("[PROBE] Baseline: D2 wird 2 s ohne Senden beobachtet ...");
  return true;
}

void UartManager::stopProbeSweep() {
  if (_probeState == ProbeState::Idle) return;
  _probeState = ProbeState::Idle;
  _probeStatus = "manuell gestoppt nach " + String(_probeCompleted) + " Kandidaten";
  Serial.printf("[PROBE] STOP: %lu Kandidaten abgeschlossen\n", (unsigned long)_probeCompleted);
}

void UartManager::setProbeReaction(uint32_t flag, const char* text) {
  if ((_probeReactionFlags & flag) != 0U) return;
  _probeReactionFlags |= flag;
  Serial.printf("[PROBE] !!! REAKTION: %s\n", text);
}

String UartManager::probeReactionText() const {
  if (_probeReactionFlags == 0U) return "keine Aenderung erkannt";
  String r;
  auto add = [&r](const char* t) { if (r.length()) r += ", "; r += t; };
  if (_probeReactionFlags & PROBE_REACTION_NEW_COMMAND) add("neuer/unbekannter Command");
  if (_probeReactionFlags & PROBE_REACTION_VALUE_CHANGE) add("Wertaenderung");
  if (_probeReactionFlags & PROBE_REACTION_META_STATUS) add("Meta/Status geaendert");
  if (_probeReactionFlags & PROBE_REACTION_RATE_CHANGE) add("Rate geaendert");
  if (_probeReactionFlags & PROBE_REACTION_PAUSE) add("D2 Pause/Reset");
  if (_probeFirstUnknownCommand) {
    char b[20];
    snprintf(b, sizeof(b), " (Cmd 0x%04X)", _probeFirstUnknownCommand);
    r += b;
  }
  if (_probeMaxFieldDelta) r += ", max Delta=" + String(_probeMaxFieldDelta);
  return r;
}

void UartManager::beginProbeCandidate() {
  const uint16_t catalogNo = TestCandidateCatalog::catalogNumberForTestOrder(_probeOrderIndex);
  if (!catalogNo || !TestCandidateCatalog::build(catalogNo, _probeCandidate)) {
    _probeState = ProbeState::Idle;
    _probeStatus = "Fehler beim Erzeugen des Testkandidaten";
    Serial.println("[PROBE] FEHLER: Kandidat konnte nicht erzeugt werden");
    return;
  }

  _probeReactionFlags = 0;
  _probeFirstUnknownCommand = 0;
  _probeMaxFieldDelta = 0;
  _probeSentCount = 0;
  _probeStateStartMillis = millis();
  _probeNextSendMillis = _probeStateStartMillis;
  _probeLastProgressMillis = _probeStateStartMillis;
  _probeCandidateStartValid = _validPacketCount;
  _probeCandidateStartUnknown = _unknownCommandCount;
  _probeCandidateStartCounts[0] = _cmd0021Count;
  _probeCandidateStartCounts[1] = _cmd0023Count;
  _probeCandidateStartCounts[2] = _cmd0031Count;
  _probeCandidateStartCounts[3] = _cmd0033Count;

  _probeStartHas0021 = _hasMainPacket;
  _probeStartHas0031 = _has0031;
  _probeStartHas0033 = _has0033;
  memcpy(_probeStartFields0021, _fields, sizeof(_probeStartFields0021));
  memcpy(_probeStartFields0033, _cmd0033Fields, sizeof(_probeStartFields0033));
  _probeStart0031Value = _cmd0031Value;
  _probeStart0031Status = _cmd0031Status;
  _probeStart0021Meta[0] = _last0021Meta[0];
  _probeStart0021Meta[1] = _last0021Meta[1];
  _probeStart0033Meta[0] = _last0033Meta[0];
  _probeStart0033Meta[1] = _last0033Meta[1];

  _probeState = ProbeState::Testing;
  _probeStatus = "testet " + String(_probeCandidate.id);

  Serial.println();
  Serial.printf("[PROBE %03u/%u] Katalog #%u  %s  P%u\n",
                (unsigned)(_probeOrderIndex + 1U), (unsigned)TestCandidateCatalog::COUNT,
                (unsigned)_probeCandidate.catalogNumber, _probeCandidate.id, (unsigned)_probeCandidate.priority);
  Serial.printf("[PROBE] Frame (%u B): %s\n", (unsigned)_probeCandidate.length,
                TestCandidateCatalog::frameHex(_probeCandidate).c_str());
}

void UartManager::finishProbeCandidate() {
  const uint32_t nowCounts[4] = {_cmd0021Count, _cmd0023Count, _cmd0031Count, _cmd0033Count};
  const char* names[4] = {"0x0021", "0x0023", "0x0031", "0x0033"};
  for (uint8_t i = 0; i < 4; ++i) {
    const uint32_t baseline = _probeBaselineCounts[i];
    if (baseline < 2U) continue;
    const float expected = (float)baseline * ((float)PROBE_CANDIDATE_MS / (float)PROBE_BASELINE_MS);
    const float actual = (float)(nowCounts[i] - _probeCandidateStartCounts[i]);
    const float diff = fabsf(actual - expected);
    const float limit = expected * 0.40f + 2.0f;
    if (diff > limit) {
      char msg[96];
      snprintf(msg, sizeof(msg), "Rate %s: %.1f erwartet / %.0f beobachtet", names[i], expected, actual);
      setProbeReaction(PROBE_REACTION_RATE_CHANGE, msg);
    }
  }

  const uint32_t observedValid = _validPacketCount - _probeCandidateStartValid;
  Serial.printf("[PROBE] Ende: gesendet=%lu, gueltige RX-Frames=%lu, Ergebnis=%s\n",
                (unsigned long)_probeSentCount, (unsigned long)observedValid, probeReactionText().c_str());

  ++_probeCompleted;
  if (_probeReactionFlags != 0U && _probeStopOnReaction) {
    _probeState = ProbeState::Idle;
    _probeStatus = "TREFFER - gestoppt bei " + String(_probeCandidate.id) + ": " + probeReactionText();
    Serial.printf("[PROBE] *** SWEEP GESTOPPT BEI TREFFER: #%u %s ***\n",
                  (unsigned)_probeCandidate.catalogNumber, _probeCandidate.id);
    return;
  }

  ++_probeOrderIndex;
  if (_probeOrderIndex >= TestCandidateCatalog::COUNT) {
    _probeState = ProbeState::Idle;
    _probeStatus = "fertig: alle 1000 Kandidaten getestet";
    Serial.println("[PROBE] ================================================");
    Serial.println("[PROBE] FERTIG: alle 1000 Kandidaten getestet");
    return;
  }

  _probeState = ProbeState::Gap;
  _probeStateStartMillis = millis();
  _probeStatus = "Pause vor naechstem Kandidaten";
}

void UartManager::observeProbeFrame(uint16_t command, const uint8_t* data, size_t dataLen, bool commandSeenBefore) {
  if (_probeState != ProbeState::Testing) return;

  if (command != 0x0021 && command != 0x0023 && command != 0x0031 && command != 0x0033 && !commandSeenBefore) {
    if (!_probeFirstUnknownCommand) _probeFirstUnknownCommand = command;
    char msg[64];
    snprintf(msg, sizeof(msg), "bisher nicht dekodierter Command 0x%04X", command);
    setProbeReaction(PROBE_REACTION_NEW_COMMAND, msg);
    return;
  }

  if (command == 0x0021 && dataLen == 14U && _probeStartHas0021) {
    if (data[0] != _probeStart0021Meta[0] || data[1] != _probeStart0021Meta[1])
      setProbeReaction(PROBE_REACTION_META_STATUS, "0x0021 Meta-Bytes geaendert");
    for (uint8_t i = 0; i < 6; ++i) {
      const uint16_t v = (uint16_t)data[2U + 2U * i] | ((uint16_t)data[3U + 2U * i] << 8U);
      const uint16_t d = absDelta16(v, _probeStartFields0021[i]);
      if (d > _probeMaxFieldDelta) _probeMaxFieldDelta = d;
      if (d >= PROBE_VALUE_DELTA)
        setProbeReaction(PROBE_REACTION_VALUE_CHANGE, "0x0021 Feldwert deutlich geaendert");
    }
  }
  else if (command == 0x0031 && dataLen == 3U && _probeStartHas0031) {
    const uint16_t v = (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
    const uint16_t d = absDelta16(v, _probeStart0031Value);
    if (d > _probeMaxFieldDelta) _probeMaxFieldDelta = d;
    if (data[2] != _probeStart0031Status)
      setProbeReaction(PROBE_REACTION_META_STATUS, "0x0031 Statusbyte geaendert");
    if (d >= PROBE_VALUE_DELTA)
      setProbeReaction(PROBE_REACTION_VALUE_CHANGE, "0x0031 Wert deutlich geaendert");
  }
  else if (command == 0x0033 && dataLen == 14U && _probeStartHas0033) {
    if (data[0] != _probeStart0033Meta[0] || data[1] != _probeStart0033Meta[1])
      setProbeReaction(PROBE_REACTION_META_STATUS, "0x0033 Meta-Bytes geaendert");
    for (uint8_t i = 0; i < 6; ++i) {
      const uint16_t v = (uint16_t)data[2U + 2U * i] | ((uint16_t)data[3U + 2U * i] << 8U);
      const uint16_t d = absDelta16(v, _probeStartFields0033[i]);
      if (d > _probeMaxFieldDelta) _probeMaxFieldDelta = d;
      if (d >= PROBE_VALUE_DELTA)
        setProbeReaction(PROBE_REACTION_VALUE_CHANGE, "0x0033 Feldwert deutlich geaendert");
    }
  }
}

void UartManager::serviceProbe() {
  if (_probeState == ProbeState::Idle || !_serial) return;
  const unsigned long now = millis();

  if (_probeState == ProbeState::Baseline) {
    if ((unsigned long)(now - _probeStateStartMillis) < PROBE_BASELINE_MS) return;
    _probeBaselineCounts[0] = _cmd0021Count - _probeBaselineCounts[0];
    _probeBaselineCounts[1] = _cmd0023Count - _probeBaselineCounts[1];
    _probeBaselineCounts[2] = _cmd0031Count - _probeBaselineCounts[2];
    _probeBaselineCounts[3] = _cmd0033Count - _probeBaselineCounts[3];
    Serial.printf("[PROBE] Baseline 2 s: 0021=%lu, 0023=%lu, 0031=%lu, 0033=%lu\n",
      (unsigned long)_probeBaselineCounts[0], (unsigned long)_probeBaselineCounts[1],
      (unsigned long)_probeBaselineCounts[2], (unsigned long)_probeBaselineCounts[3]);
    beginProbeCandidate();
    return;
  }

  if (_probeState == ProbeState::Gap) {
    if ((unsigned long)(now - _probeStateStartMillis) >= PROBE_GAP_MS) beginProbeCandidate();
    return;
  }

  if (_probeState != ProbeState::Testing) return;

  if ((unsigned long)(now - _probeStateStartMillis) >= PROBE_CANDIDATE_MS) {
    finishProbeCandidate();
    return;
  }

  const uint32_t baselineKnown = _probeBaselineCounts[0] + _probeBaselineCounts[1] + _probeBaselineCounts[2] + _probeBaselineCounts[3];
  if (baselineKnown > 0U && (unsigned long)(now - _lastPacketMillis) >= PROBE_PAUSE_DETECT_MS) {
    setProbeReaction(PROBE_REACTION_PAUSE, "D2 liefert seit mindestens 500 ms kein gueltiges Frame");
  }

  if ((long)(now - _probeNextSendMillis) >= 0) {
    const size_t written = _serial->write(_probeCandidate.frame, _probeCandidate.length);
    if (written != _probeCandidate.length) {
      _probeState = ProbeState::Idle;
      _probeStatus = "TX-Fehler bei " + String(_probeCandidate.id);
      Serial.printf("[PROBE] FEHLER: nur %u/%u Bytes geschrieben - Sweep abgebrochen\n",
                    (unsigned)written, (unsigned)_probeCandidate.length);
      return;
    }
    ++_probeSentCount;
    _probeNextSendMillis += PROBE_SEND_INTERVAL_MS;
    if ((long)(now - _probeNextSendMillis) > (long)PROBE_SEND_INTERVAL_MS)
      _probeNextSendMillis = now + PROBE_SEND_INTERVAL_MS;
  }

  if ((unsigned long)(now - _probeLastProgressMillis) >= 1000UL) {
    _probeLastProgressMillis = now;
    const unsigned elapsed = (unsigned)((now - _probeStateStartMillis) / 1000UL);
    Serial.printf("[PROBE] %us/5s  TX=%lu  RX-valid=%lu  %s\n", elapsed,
                  (unsigned long)_probeSentCount,
                  (unsigned long)(_validPacketCount - _probeCandidateStartValid),
                  _probeReactionFlags ? probeReactionText().c_str() : "keine Auffaelligkeit");
  }
}

uint16_t UartManager::rawField(uint8_t index) const {
  return index < 6 ? _fields[index] : 0;
}

float UartManager::normalizedField(uint8_t index) const {
  return index < 5 ? _normalized[index] : 0.0f;
}

String UartManager::fieldAlias(uint8_t index) const {
  return index < 6 ? _aliases[index] : String();
}

void UartManager::setFieldAlias(uint8_t index, const String& value) {
  if (index >= 6) return;
  _aliases[index] = value;
  _aliases[index].trim();
  if (_aliases[index].length() > 32) _aliases[index].remove(32);
}

UartManager::Calibration UartManager::calibration(uint8_t index) const {
  if (index < 5) return _cal[index];
  return {0, 0, 0};
}

void UartManager::setCalibration(uint8_t index, uint16_t minV, uint16_t centerV, uint16_t maxV) {
  if (index >= 5) return;
  if (minV < centerV && centerV < maxV) _cal[index] = {minV, centerV, maxV};
}

void UartManager::setDeadband(float value) {
  if (isfinite(value) && value >= 0.0f && value <= 0.5f) _deadband = value;
}

String UartManager::jsonEscape(const String& value) {
  String out;
  out.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value[i];
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: if ((uint8_t)c >= 32) out += c; break;
    }
  }
  return out;
}

String UartManager::statusJson() const {
  String j;
  j.reserve(3400);
  j += "{\"mode\":\"" + jsonEscape(modeText()) + "\",\"running\":" + String(_running ? "true" : "false");
  j += ",\"status\":\"" + jsonEscape(_status) + "\",\"total_bytes\":" + String((unsigned long)(_totalBytes & 0xFFFFFFFFULL));
  j += ",\"raw_hex\":\"" + jsonEscape(rawHex(256)) + "\",\"raw_ascii\":\"" + jsonEscape(rawAscii(256)) + "\"";
  j += ",\"packets\":" + String(_packetCount) + ",\"valid\":" + String(_validPacketCount) + ",\"invalid\":" + String(_invalidPacketCount);
  j += ",\"cmd0021\":" + String(_cmd0021Count) + ",\"cmd0023\":" + String(_cmd0023Count) + ",\"cmd0031\":" + String(_cmd0031Count) + ",\"cmd0033\":" + String(_cmd0033Count) + ",\"unknown_cmd\":" + String(_unknownCommandCount);
  j += ",\"last_route\":" + String(_lastRoute) + ",\"last_outer_length\":" + String(_lastOuterLength) + ",\"last_inner_type\":" + String(_lastInnerType);
  j += ",\"last_inner_length\":" + String(_lastInnerLength) + ",\"last_command\":" + String(_lastCommand) + ",\"last_data_length\":" + String(_lastDataLength) + ",\"checksum_ok\":" + String(_lastChecksumOk ? "true" : "false");
  j += ",\"has_main\":" + String(_hasMainPacket ? "true" : "false");
  j += ",\"tx_enabled\":" + String(_txEnabled ? "true" : "false");
  j += ",\"tx_ready\":" + String(txReady() ? "true" : "false") + ",\"console_tx_ready\":" + String(consoleTxReady() ? "true" : "false") + ",\"tx_active\":" + String(_txActive ? "true" : "false");
  j += ",\"tx_owner\":\"" + jsonEscape(txOwnerText()) + "\"";
  j += ",\"tx_status\":\"" + jsonEscape(_txStatus) + "\",\"tx_target\":" + String(_txTargetNormalized, 3) + ",\"tx_raw\":" + String(_txField5Raw) + ",\"tx_packets\":" + String(_txPacketCount);
  j += ",\"probe_active\":" + String(_probeState != ProbeState::Idle ? "true" : "false");
  j += ",\"probe_stop_on_reaction\":" + String(_probeStopOnReaction ? "true" : "false");
  j += ",\"probe_status\":\"" + jsonEscape(_probeStatus) + "\",\"probe_completed\":" + String(_probeCompleted);
  j += ",\"probe_order\":" + String(_probeOrderIndex + (_probeState == ProbeState::Testing ? 1U : 0U));
  j += ",\"probe_catalog\":" + String(_probeCandidate.catalogNumber) + ",\"probe_id\":\"" + jsonEscape(String(_probeCandidate.id)) + "\"";
  j += ",\"probe_priority\":" + String(_probeCandidate.priority) + ",\"probe_sent\":" + String(_probeSentCount);
  j += ",\"probe_reaction\":" + String(_probeReactionFlags ? "true" : "false") + ",\"probe_reaction_text\":\"" + jsonEscape(probeReactionText()) + "\"";
  j += ",\"fields\":[";
  for (uint8_t i = 0; i < 6; ++i) {
    if (i) j += ',';
    j += "{\"index\":" + String(i + 1) + ",\"raw\":" + String(_fields[i]);
    if (i < 5) j += ",\"norm\":" + String(_normalized[i], 4);
    j += '}';
  }
  j += "]";
  j += ",\"cmd0023_state\":{\"available\":" + String(_has0023 ? "true" : "false") + ",\"v1\":" + String(_cmd0023Value1) + ",\"v2\":" + String(_cmd0023Value2) + ",\"v3\":" + String(_cmd0023Value3) + "}";
  j += ",\"cmd0031_state\":{\"available\":" + String(_has0031 ? "true" : "false") + ",\"value\":" + String(_cmd0031Value) + ",\"status\":" + String(_cmd0031Status) + "}";
  j += ",\"cmd0033_state\":{\"available\":" + String(_has0033 ? "true" : "false") + ",\"fields\":[";
  for (uint8_t i = 0; i < 6; ++i) { if (i) j += ','; j += String(_cmd0033Fields[i]); }
  j += "]}}";
  return j;
}


// -----------------------------------------------------------------------------
// UART image transfer (v0.17)
// -----------------------------------------------------------------------------
namespace {
static void shaHex(const unsigned char hash[32], char out[65]) {
  static const char* H = "0123456789abcdef";
  for (size_t i = 0; i < 32; ++i) {
    out[i * 2] = H[(hash[i] >> 4) & 0x0F];
    out[i * 2 + 1] = H[hash[i] & 0x0F];
  }
  out[64] = '\0';
}

static bool isHex64(const String& s) {
  if (s.length() != 64U) return false;
  for (size_t i = 0; i < 64U; ++i) {
    const char c = s[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
  }
  return true;
}
}

bool UartManager::validateImageSource(const String& source, uint8_t& mtdNo) const {
  // Version 1 intentionally limits reads to read-only MTD character devices.
  // Accepted examples: /dev/mtd0ro ... /dev/mtd31ro
  if (!source.startsWith("/dev/mtd") || !source.endsWith("ro")) return false;
  const String n = source.substring(8, source.length() - 2);
  if (!n.length() || n.length() > 2U) return false;
  for (size_t i = 0; i < n.length(); ++i) if (n[i] < '0' || n[i] > '9') return false;
  const int v = n.toInt();
  if (v < 0 || v > 31) return false;
  mtdNo = (uint8_t)v;
  return true;
}

bool UartManager::imageActive() const {
  return _imageState != ImageState::Idle && _imageState != ImageState::Done && _imageState != ImageState::Error;
}

bool UartManager::imageBlockReady() const {
  return _imageState == ImageState::BlockReady;
}

bool UartManager::startImageTransfer(const String& source, uint32_t startBlock) {
  uint8_t mtdNo = 0;
  if (imageActive()) { _imageStatus = "Transfer laeuft bereits"; return false; }
  if (!validateImageSource(source, mtdNo)) { _imageStatus = "Quelle ungueltig; erlaubt ist /dev/mtdNro"; return false; }
  if (!_running || !_serial) { _imageStatus = "UART ist gestoppt"; return false; }
  if (!_txEnabled || _txPin < 0 || !validTxPin(_txPin)) { _imageStatus = "TX ist nicht freigegeben"; return false; }
  if (_probeState != ProbeState::Idle || _txActive) { _imageStatus = "TX ist durch eine andere Funktion belegt"; return false; }

  _imageSource = source;
  _imageStartBlock = startBlock;
  _imageCurrentBlock = startBlock;
  _imageTotalSize = 0;
  _imageTotalBlocks = 0;
  _imageExpectedSize = 0;
  _imageBlockLength = 0;
  _imageExpectedSha[0] = _imageBlockSha[0] = _imageLocalTotalSha[0] = _imageRemoteTotalSha[0] = '\0';
  _imageRetries = 0;
  _imageRetryTotal = 0;
  _imageErrors = 0;
  _imageAcceptedBytes = 0;
  _imageLine = "";
  _imageStartedMillis = millis();
  _imageLastRxMillis = millis();
  _imageOverallVerify = (startBlock == 0U);
  _imageMtdMarkerSeen = false;
  _imageShaStarted = false;
  clearConsole();
  clearRaw();
  resetParser();

  mbedtls_sha256_init(&_imageSha);
  if (_imageOverallVerify) {
    if (mbedtls_sha256_starts_ret(&_imageSha, 0) != 0) {
      _imageState = ImageState::Error;
      _imageStatus = "SHA256 konnte nicht initialisiert werden";
      return false;
    }
    _imageShaStarted = true;
  }

  _imageState = ImageState::WaitMtd;
  _imageStatus = "ermittle MTD-Groesse";
  const String cmd = "\r\nprintf '\\n<<<BX3IMG:MTD_BEGIN>>>\\n'; cat /proc/mtd | grep '^mtd" + String(mtdNo) + ":'; printf '<<<BX3IMG:MTD_END>>>\\n'\r";
  _serial->write((const uint8_t*)cmd.c_str(), cmd.length());
  _imageLastRxMillis = millis();
  Serial.printf("[IMAGE] Start %s ab Block %lu\n", _imageSource.c_str(), (unsigned long)_imageStartBlock);
  return true;
}

void UartManager::abortImageTransfer(const String& reason) {
  if (_serial && _running && imageActive()) {
    const char* cleanup = "\r\nrm -f /tmp/bx3blk\r";
    _serial->write((const uint8_t*)cleanup, strlen(cleanup));
  }
  if (_imageShaStarted) {
    mbedtls_sha256_free(&_imageSha);
    _imageShaStarted = false;
  }
  _imageState = ImageState::Error;
  _imageStatus = reason;
  _imageLine = "";
  Serial.printf("[IMAGE] Abbruch: %s\n", reason.c_str());
}

bool UartManager::requestImageBlock() {
  if (!_serial || !_running || !_imageTotalSize || _imageCurrentBlock >= _imageTotalBlocks) return false;
  const uint32_t remaining = _imageTotalSize - _imageCurrentBlock * (uint32_t)IMAGE_BLOCK_SIZE;
  _imageExpectedSize = remaining < IMAGE_BLOCK_SIZE ? remaining : (uint32_t)IMAGE_BLOCK_SIZE;
  _imageBlockLength = 0;
  _imageExpectedSha[0] = '\0';
  _imageBlockSha[0] = '\0';
  _imageLine = "";

  char num[10];
  snprintf(num, sizeof(num), "%06lu", (unsigned long)_imageCurrentBlock);
  String cmd;
  cmd.reserve(520);
  cmd += "rm -f /tmp/bx3blk; dd if=" + _imageSource + " of=/tmp/bx3blk bs=" + String(IMAGE_BLOCK_SIZE) + " skip=" + String(_imageCurrentBlock) + " count=1 2>/dev/null; ";
  cmd += "printf '\\n<<<BX3IMG:BEGIN:" + String(num) + ">>>\\n'; ";
  cmd += "S=$(wc -c < /tmp/bx3blk); printf '<<<BX3IMG:SIZE:%s>>>\\n' \"$S\"; ";
  cmd += "H=$(sha256sum /tmp/bx3blk); H=${H%% *}; printf '<<<BX3IMG:SHA256:%s>>>\\n' \"$H\"; ";
  cmd += "base64 /tmp/bx3blk; printf '<<<BX3IMG:END:" + String(num) + ">>>\\n'; rm -f /tmp/bx3blk\r";

  const size_t written = _serial->write((const uint8_t*)cmd.c_str(), cmd.length());
  if (written != cmd.length()) {
    _imageState = ImageState::Error;
    _imageStatus = "Blockanforderung konnte nicht vollstaendig gesendet werden";
    return false;
  }
  _imageState = ImageState::WaitBegin;
  _imageStatus = "warte auf Block " + String(_imageCurrentBlock) + " / " + String(_imageTotalBlocks);
  _imageLastRxMillis = millis();
  return true;
}

void UartManager::retryImageBlock(const String& reason) {
  if (!imageActive()) return;
  ++_imageErrors;
  if (_imageState == ImageState::WaitMtd) {
    abortImageTransfer("MTD-Ermittlung fehlgeschlagen: " + reason);
    return;
  }
  if (_imageState == ImageState::WaitTotalSha) {
    abortImageTransfer("Gesamt-SHA256 fehlgeschlagen: " + reason);
    return;
  }
  ++_imageRetries;
  ++_imageRetryTotal;
  if (_imageRetries > IMAGE_MAX_RETRIES) {
    abortImageTransfer("Block " + String(_imageCurrentBlock) + ": Retry-Limit erreicht (" + reason + ")");
    return;
  }
  _imageStatus = "Block " + String(_imageCurrentBlock) + " Fehler: " + reason + ", Retry " + String(_imageRetries) + "/" + String(IMAGE_MAX_RETRIES);
  requestImageBlock();
}

void UartManager::processImageByte(uint8_t value) {
  _imageLastRxMillis = millis();
  if (value == '\r') return;
  if (value == '\n') {
    String line = _imageLine;
    _imageLine = "";
    if (line.length()) processImageLine(line);
    return;
  }
  if (_imageLine.length() < 512U) _imageLine += (char)value;
  else retryImageBlock("Zeile zu lang");
}

void UartManager::processImageLine(String line) {
  line.trim();
  if (!line.length()) return;

  if (_imageState == ImageState::WaitMtd) {
    if (line == "<<<BX3IMG:MTD_BEGIN>>>") { _imageMtdMarkerSeen = true; return; }
    if (_imageMtdMarkerSeen && line.startsWith("mtd")) {
      const int colon = line.indexOf(':');
      if (colon > 3) {
        int p = colon + 1;
        while (p < (int)line.length() && line[p] == ' ') ++p;
        int e = p;
        while (e < (int)line.length() && line[e] != ' ') ++e;
        const String hexSize = line.substring(p, e);
        _imageTotalSize = (uint32_t)strtoul(hexSize.c_str(), nullptr, 16);
      }
      return;
    }
    if (line == "<<<BX3IMG:MTD_END>>>") {
      if (!_imageMtdMarkerSeen) { abortImageTransfer("Shell-Synchronisierung fehlgeschlagen"); return; }
      if (!_imageTotalSize) { abortImageTransfer("MTD-Groesse konnte nicht gelesen werden"); return; }
      _imageTotalBlocks = (_imageTotalSize + IMAGE_BLOCK_SIZE - 1U) / IMAGE_BLOCK_SIZE;
      if (_imageStartBlock >= _imageTotalBlocks) { abortImageTransfer("Startblock liegt ausserhalb des Images"); return; }
      _imageCurrentBlock = _imageStartBlock;
      _imageStatus = "MTD-Groesse " + String(_imageTotalSize) + " Byte; starte Blocktransfer";
      requestImageBlock();
    }
    return;
  }

  char num[10];
  snprintf(num, sizeof(num), "%06lu", (unsigned long)_imageCurrentBlock);
  const String beginMarker = "<<<BX3IMG:BEGIN:" + String(num) + ">>>";
  const String endMarker = "<<<BX3IMG:END:" + String(num) + ">>>";

  if (_imageState == ImageState::WaitBegin) {
    if (line == beginMarker) _imageState = ImageState::WaitSize;
    return; // shell echo and unrelated lines are intentionally ignored here
  }
  if (_imageState == ImageState::WaitSize) {
    if (!line.startsWith("<<<BX3IMG:SIZE:") || !line.endsWith(">>>")) { retryImageBlock("SIZE-Marker fehlt"); return; }
    const String n = line.substring(15, line.length() - 3);
    const uint32_t got = (uint32_t)strtoul(n.c_str(), nullptr, 10);
    if (got != _imageExpectedSize) { retryImageBlock("Groesse " + String(got) + " statt " + String(_imageExpectedSize)); return; }
    _imageState = ImageState::WaitSha;
    return;
  }
  if (_imageState == ImageState::WaitSha) {
    if (!line.startsWith("<<<BX3IMG:SHA256:") || !line.endsWith(">>>")) { retryImageBlock("SHA256-Marker fehlt"); return; }
    String h = line.substring(17, line.length() - 3); h.toLowerCase();
    if (!isHex64(h)) { retryImageBlock("SHA256 ungueltig"); return; }
    strncpy(_imageExpectedSha, h.c_str(), sizeof(_imageExpectedSha));
    _imageExpectedSha[64] = '\0';
    _imageState = ImageState::RxBase64;
    return;
  }
  if (_imageState == ImageState::RxBase64) {
    if (line == endMarker) {
      if (_imageBlockLength != _imageExpectedSize) { retryImageBlock("dekodierte Laenge stimmt nicht"); return; }
      unsigned char hash[32];
      mbedtls_sha256_context ctx;
      mbedtls_sha256_init(&ctx);
      if (mbedtls_sha256_starts_ret(&ctx, 0) != 0 || mbedtls_sha256_update_ret(&ctx, _imageBlock, _imageBlockLength) != 0 || mbedtls_sha256_finish_ret(&ctx, hash) != 0) {
        mbedtls_sha256_free(&ctx); retryImageBlock("lokaler SHA256 Fehler"); return;
      }
      mbedtls_sha256_free(&ctx);
      shaHex(hash, _imageBlockSha);
      if (strcmp(_imageBlockSha, _imageExpectedSha) != 0) { retryImageBlock("SHA256 stimmt nicht"); return; }
      _imageState = ImageState::BlockReady;
      _imageStatus = "Block " + String(_imageCurrentBlock) + " validiert - wartet auf Browser";
      return;
    }
    if (line.startsWith("<<<BX3IMG:")) { retryImageBlock("unerwarteter Marker"); return; }
    uint8_t decoded[96];
    size_t outLen = 0;
    const int rc = mbedtls_base64_decode(decoded, sizeof(decoded), &outLen, (const unsigned char*)line.c_str(), line.length());
    if (rc != 0) { retryImageBlock("Base64-Fehler"); return; }
    if (_imageBlockLength + outLen > IMAGE_BLOCK_SIZE || _imageBlockLength + outLen > _imageExpectedSize) { retryImageBlock("Blockpuffer ueberlaufen"); return; }
    memcpy(_imageBlock + _imageBlockLength, decoded, outLen);
    _imageBlockLength += (uint32_t)outLen;
    return;
  }
  if (_imageState == ImageState::WaitTotalSha) {
    if (line.startsWith("<<<BX3IMG:TOTAL_SHA256:") && line.endsWith(">>>")) {
      String h = line.substring(23, line.length() - 3); h.toLowerCase();
      if (!isHex64(h)) { abortImageTransfer("BX3 Gesamt-SHA256 ungueltig"); return; }
      strncpy(_imageRemoteTotalSha, h.c_str(), sizeof(_imageRemoteTotalSha));
      _imageRemoteTotalSha[64] = '\0';
      finishImageSha();
    }
  }
}

size_t UartManager::imageReadBlock(size_t offset, uint8_t* out, size_t maxLen) const {
  if (_imageState != ImageState::BlockReady || !out || offset >= _imageBlockLength) return 0;
  size_t n = _imageBlockLength - offset;
  if (n > maxLen) n = maxLen;
  memcpy(out, _imageBlock + offset, n);
  return n;
}

bool UartManager::imageAcknowledgeBlock() {
  if (_imageState != ImageState::BlockReady) return false;
  if (_imageOverallVerify && _imageShaStarted) {
    if (mbedtls_sha256_update_ret(&_imageSha, _imageBlock, _imageBlockLength) != 0) {
      abortImageTransfer("Gesamt-SHA256 Update fehlgeschlagen");
      return false;
    }
  }
  _imageAcceptedBytes += _imageBlockLength;
  ++_imageCurrentBlock;
  _imageRetries = 0;
  if (_imageCurrentBlock >= _imageTotalBlocks) {
    requestImageTotalSha();
    return true;
  }
  return requestImageBlock();
}

void UartManager::requestImageTotalSha() {
  if (!_serial || !_running) { abortImageTransfer("UART vor Gesamt-SHA256 verloren"); return; }
  _imageState = ImageState::WaitTotalSha;
  _imageStatus = "warte auf Gesamt-SHA256 des BX3";
  String cmd = "H=$(sha256sum " + _imageSource + "); H=${H%% *}; printf '\\n<<<BX3IMG:TOTAL_SHA256:%s>>>\\n' \"$H\"\r";
  _serial->write((const uint8_t*)cmd.c_str(), cmd.length());
  _imageLastRxMillis = millis();
}

void UartManager::finishImageSha() {
  bool ok = true;
  if (_imageOverallVerify && _imageShaStarted) {
    unsigned char hash[32];
    if (mbedtls_sha256_finish_ret(&_imageSha, hash) != 0) ok = false;
    else shaHex(hash, _imageLocalTotalSha);
    mbedtls_sha256_free(&_imageSha);
    _imageShaStarted = false;
    if (ok && strcmp(_imageLocalTotalSha, _imageRemoteTotalSha) != 0) ok = false;
  }
  if (!ok) {
    _imageState = ImageState::Error;
    _imageStatus = "Gesamt-SHA256 stimmt nicht";
    ++_imageErrors;
    return;
  }
  _imageState = ImageState::Done;
  _imageStatus = _imageOverallVerify ? "IMAGE VERIFIED" : "Transfer beendet; Gesamtpruefung bei Resume nicht verfuegbar";
  Serial.printf("[IMAGE] Fertig: %s\n", _imageStatus.c_str());
}

String UartManager::imageStatusJson() const {
  const unsigned long elapsed = millis() - _imageStartedMillis;
  const float seconds = elapsed ? (float)elapsed / 1000.0f : 0.0f;
  const float rate = seconds > 0.01f ? (float)_imageAcceptedBytes / seconds : 0.0f;
  const bool active = imageActive();
  const bool done = _imageState == ImageState::Done;
  const bool error = _imageState == ImageState::Error;
  String j;
  j.reserve(900);
  j += "{\"active\":" + String(active ? "true" : "false") + ",\"done\":" + String(done ? "true" : "false") + ",\"error\":" + String(error ? "true" : "false");
  j += ",\"block_ready\":" + String(_imageState == ImageState::BlockReady ? "true" : "false");
  j += ",\"status\":\"" + jsonEscape(_imageStatus) + "\",\"source\":\"" + jsonEscape(_imageSource) + "\"";
  j += ",\"total_size\":" + String(_imageTotalSize) + ",\"block_size\":" + String(IMAGE_BLOCK_SIZE) + ",\"total_blocks\":" + String(_imageTotalBlocks);
  j += ",\"current_block\":" + String(_imageCurrentBlock) + ",\"start_block\":" + String(_imageStartBlock) + ",\"accepted_bytes\":" + String((unsigned long)(_imageAcceptedBytes & 0xFFFFFFFFULL));
  j += ",\"errors\":" + String(_imageErrors) + ",\"retries\":" + String(_imageRetryTotal) + ",\"retry_current\":" + String(_imageRetries);
  j += ",\"rate_bps\":" + String(rate, 1) + ",\"overall_verify\":" + String(_imageOverallVerify ? "true" : "false");
  j += ",\"local_sha256\":\"" + String(_imageLocalTotalSha) + "\",\"remote_sha256\":\"" + String(_imageRemoteTotalSha) + "\"}";
  return j;
}
