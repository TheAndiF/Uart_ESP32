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

bool UartManager::restart() {
  if (_serial) {
    _serial->end();
    delay(5);
  }
  _serial = nullptr;
  _running = false;
  resetParser();

  if (_mode == Mode::Off) {
    _status = "deaktiviert";
    return true;
  }
  if (!validPins()) {
    _status = "ungueltige GPIO-Konfiguration";
    return false;
  }
  if (_baud < 300U || _baud > 2000000U) {
    _status = "ungueltige Baudrate";
    return false;
  }

  _serial = (_uartNumber == 1) ? &_uart1 : &_uart2;
  _serial->begin(_baud, serialConfig(), _rxPin, _txPin);
  _running = true;
  _status = modeText() + " aktiv auf UART" + String(_uartNumber) + ", RX GPIO" + String(_rxPin);
  if (_txPin >= 0) _status += ", TX GPIO" + String(_txPin);
  _status += ", " + String(_baud) + " " + _frame;
  Serial.printf("[UART] %s\n", _status.c_str());
  return true;
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
  _lastPacketType = _packet[2];
  _lastPacketLength = _packet[3];
  _lastPacketMillis = millis();

  uint8_t cs = 0;
  if (packetLength <= 7U) {
    _lastChecksumOk = false;
  } else {
    for (size_t i = 6; i + 1U < packetLength; ++i) cs ^= _packet[i];
    _lastChecksumOk = (cs == _packet[packetLength - 1U]);
  }

  if (!_lastChecksumOk) {
    ++_invalidPacketCount;
    return;
  }
  ++_validPacketCount;

  if (_lastPacketType == 0x10 && _lastPacketLength == 0x15 && packetLength == 25U) {
    ++_mainPacketCount;
    _hasMainPacket = true;
    for (uint8_t i = 0; i < 6; ++i) {
      const size_t pos = 12U + 2U * i;
      _fields[i] = (uint16_t)_packet[pos] | ((uint16_t)_packet[pos + 1U] << 8U);
    }
    for (uint8_t i = 0; i < 5; ++i) {
      _normalized[i] = applyDeadband(normalize(_fields[i], _cal[i]));
    }
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
  j.reserve(1800);
  j += "{\"mode\":\"" + jsonEscape(modeText()) + "\",\"running\":" + String(_running ? "true" : "false");
  j += ",\"status\":\"" + jsonEscape(_status) + "\",\"total_bytes\":" + String((unsigned long)(_totalBytes & 0xFFFFFFFFULL));
  j += ",\"raw_hex\":\"" + jsonEscape(rawHex(256)) + "\",\"raw_ascii\":\"" + jsonEscape(rawAscii(256)) + "\"";
  j += ",\"packets\":" + String(_packetCount) + ",\"valid\":" + String(_validPacketCount) + ",\"invalid\":" + String(_invalidPacketCount) + ",\"main\":" + String(_mainPacketCount);
  j += ",\"last_type\":" + String(_lastPacketType) + ",\"last_length\":" + String(_lastPacketLength) + ",\"checksum_ok\":" + String(_lastChecksumOk ? "true" : "false");
  j += ",\"has_main\":" + String(_hasMainPacket ? "true" : "false") + ",\"fields\":[";
  for (uint8_t i = 0; i < 6; ++i) {
    if (i) j += ',';
    j += "{\"index\":" + String(i + 1) + ",\"raw\":" + String(_fields[i]);
    if (i < 5) j += ",\"norm\":" + String(_normalized[i], 4);
    j += '}';
  }
  j += "]}";
  return j;
}
