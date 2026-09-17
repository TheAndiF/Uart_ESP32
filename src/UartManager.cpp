#include "UartManager.h"
#include <math.h>

static bool prefHas(Preferences& p, const char* key) { return p.isKey(key); }

void UartManager::load() {
  Preferences p;
  p.begin("uartconf", true);
  _mode = (Mode)(prefHas(p, "mode") ? p.getUChar("mode", 0) : 0);
  if ((uint8_t)_mode > (uint8_t)Mode::Decode) _mode = Mode::Off;
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
  p.putUChar("mode", (uint8_t)_mode);
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
  if (_mode == Mode::Off) {
    _status = "deaktiviert - Modus Aus";
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
  _txStatus = (_txPin >= 0) ? String("warte auf gueltiges 0x0021-Paket") : String("nicht bereit: TX GPIO ist deaktiviert");
  _serial = (_uartNumber == 1) ? &_uart1 : &_uart2;
  _serial->begin(_baud, serialConfig(), _rxPin, _txPin);
  _running = true;
  _status = modeText() + " aktiv auf UART" + String(_uartNumber) + ", RX GPIO" + String(_rxPin);
  if (_txPin >= 0) _status += ", TX GPIO" + String(_txPin);
  _status += ", " + String(_baud) + " " + _frame;
  Serial.printf("[UART] %s\n", _status.c_str());
  return true;
}

void UartManager::stop() {
  _txActive = false;
  if (_serial) {
    _serial->end();
    delay(5);
  }
  _serial = nullptr;
  _running = false;
  resetParser();
  if (_txStatus.startsWith("sendet")) _txStatus = "gestoppt";
  _status = (_mode == Mode::Off) ? String("deaktiviert") : modeText() + " gestoppt";
  Serial.printf("[UART] %s\n", _status.c_str());
}

bool UartManager::restart() {
  stop();
  if (_mode == Mode::Off) {
    _status = "deaktiviert";
    return true;
  }
  return start();
}

void UartManager::loop() {
  if (!_running || !_serial || _mode == Mode::Off) return;
  int budget = 512;
  while (_serial->available() > 0 && budget-- > 0) {
    int value = _serial->read();
    if (value < 0) break;
    uint8_t b = (uint8_t)value;
    pushRaw(b);
    ++_totalBytes;
    if (_mode == Mode::Decode) processDecoderByte(b);
  }
  serviceTxReplay();
}

String UartManager::modeText() const {
  switch (_mode) {
    case Mode::Raw: return "Raw / Sniffer";
    case Mode::Decode: return "Protokoll-Decoder";
    default: return "Aus";
  }
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
      _txStatus = (_txPin >= 0) ? String("bereit: gueltiges 0x0021-Paket vorhanden") : String("nicht bereit: TX GPIO ist deaktiviert");
    }
    // 0x0021 DATA: 2 meta bytes, then six uint16 little-endian fields.
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
    for (uint8_t i = 0; i < 6; ++i) {
      const size_t pos = 2U + 2U * i;
      _cmd0033Fields[i] = (uint16_t)data[pos] | ((uint16_t)data[pos + 1U] << 8U);
    }
  }
  else {
    ++_unknownCommandCount;
  }
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
  return _running && _serial && _mode == Mode::Decode && _txPin >= 0 && validTxPin(_txPin) && _hasLastMainPacketCopy;
}

bool UartManager::sendField5ForOneSecond(float normalizedValue) {
  if (_txActive) {
    _txStatus = "Senden bereits aktiv";
    return false;
  }
  if (!_running || !_serial) {
    _txStatus = "nicht bereit: UART ist gestoppt";
    return false;
  }
  if (_mode != Mode::Decode) {
    _txStatus = "nicht bereit: Protokoll-Decoder ist nicht aktiv";
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
  j.reserve(2600);
  j += "{\"mode\":\"" + jsonEscape(modeText()) + "\",\"running\":" + String(_running ? "true" : "false");
  j += ",\"status\":\"" + jsonEscape(_status) + "\",\"total_bytes\":" + String((unsigned long)(_totalBytes & 0xFFFFFFFFULL));
  j += ",\"raw_hex\":\"" + jsonEscape(rawHex(256)) + "\",\"raw_ascii\":\"" + jsonEscape(rawAscii(256)) + "\"";
  j += ",\"packets\":" + String(_packetCount) + ",\"valid\":" + String(_validPacketCount) + ",\"invalid\":" + String(_invalidPacketCount);
  j += ",\"cmd0021\":" + String(_cmd0021Count) + ",\"cmd0023\":" + String(_cmd0023Count) + ",\"cmd0031\":" + String(_cmd0031Count) + ",\"cmd0033\":" + String(_cmd0033Count) + ",\"unknown_cmd\":" + String(_unknownCommandCount);
  j += ",\"last_route\":" + String(_lastRoute) + ",\"last_outer_length\":" + String(_lastOuterLength) + ",\"last_inner_type\":" + String(_lastInnerType);
  j += ",\"last_inner_length\":" + String(_lastInnerLength) + ",\"last_command\":" + String(_lastCommand) + ",\"last_data_length\":" + String(_lastDataLength) + ",\"checksum_ok\":" + String(_lastChecksumOk ? "true" : "false");
  j += ",\"has_main\":" + String(_hasMainPacket ? "true" : "false");
  j += ",\"tx_ready\":" + String(txReady() ? "true" : "false") + ",\"tx_active\":" + String(_txActive ? "true" : "false");
  j += ",\"tx_status\":\"" + jsonEscape(_txStatus) + "\",\"tx_target\":" + String(_txTargetNormalized, 3) + ",\"tx_raw\":" + String(_txField5Raw) + ",\"tx_packets\":" + String(_txPacketCount);
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

