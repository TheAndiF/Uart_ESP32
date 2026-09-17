#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include <Preferences.h>

class UartManager {
public:
  enum class Mode : uint8_t { Off = 0, Raw = 1, Decode = 2 };

  struct Calibration {
    uint16_t minV;
    uint16_t centerV;
    uint16_t maxV;
  };

  void begin();
  void loop();
  void load();
  void save();
  bool restart();

  void setMode(Mode mode) { _mode = mode; }
  Mode mode() const { return _mode; }
  String modeText() const;

  void setUartNumber(uint8_t value) { _uartNumber = value == 1 ? 1 : 2; }
  uint8_t uartNumber() const { return _uartNumber; }
  void setRxPin(int value) { _rxPin = value; }
  int rxPin() const { return _rxPin; }
  void setTxPin(int value) { _txPin = value; }
  int txPin() const { return _txPin; }
  void setBaud(uint32_t value) { _baud = value; }
  uint32_t baud() const { return _baud; }
  void setFrame(const String& value);
  String frame() const { return _frame; }

  bool isRunning() const { return _running; }
  String status() const { return _status; }
  bool validPins() const;
  bool validRxPin(int pin) const;
  bool validTxPin(int pin) const;

  void clearRaw();
  String rawHex(size_t maxBytes = 256) const;
  String rawAscii(size_t maxBytes = 256) const;
  uint64_t totalBytes() const { return _totalBytes; }

  uint32_t packetCount() const { return _packetCount; }
  uint32_t validPacketCount() const { return _validPacketCount; }
  uint32_t invalidPacketCount() const { return _invalidPacketCount; }
  uint32_t mainPacketCount() const { return _mainPacketCount; }
  uint8_t lastPacketType() const { return _lastPacketType; }
  uint8_t lastPacketLength() const { return _lastPacketLength; }
  bool lastChecksumOk() const { return _lastChecksumOk; }
  unsigned long lastPacketMillis() const { return _lastPacketMillis; }
  bool hasMainPacket() const { return _hasMainPacket; }

  uint16_t rawField(uint8_t index) const;
  float normalizedField(uint8_t index) const;
  String fieldAlias(uint8_t index) const;
  void setFieldAlias(uint8_t index, const String& value);
  Calibration calibration(uint8_t index) const;
  void setCalibration(uint8_t index, uint16_t minV, uint16_t centerV, uint16_t maxV);
  void setDeadband(float value);
  float deadband() const { return _deadband; }

  String statusJson() const;

private:
  static constexpr size_t RAW_CAPACITY = 512;
  static constexpr size_t PACKET_CAPACITY = 128;

  HardwareSerial _uart1{1};
  HardwareSerial _uart2{2};
  HardwareSerial* _serial = nullptr;

  Mode _mode = Mode::Off;
  uint8_t _uartNumber = 2;
  int _rxPin = 16;
  int _txPin = -1;
  uint32_t _baud = 115200;
  String _frame = "8N1";
  bool _running = false;
  String _status = "deaktiviert";

  uint8_t _raw[RAW_CAPACITY]{};
  size_t _rawHead = 0;
  size_t _rawCount = 0;
  uint64_t _totalBytes = 0;

  uint8_t _packet[PACKET_CAPACITY]{};
  size_t _packetPos = 0;
  size_t _expectedPacketLength = 0;
  uint8_t _syncState = 0;

  uint32_t _packetCount = 0;
  uint32_t _validPacketCount = 0;
  uint32_t _invalidPacketCount = 0;
  uint32_t _mainPacketCount = 0;
  uint8_t _lastPacketType = 0;
  uint8_t _lastPacketLength = 0;
  bool _lastChecksumOk = false;
  unsigned long _lastPacketMillis = 0;
  bool _hasMainPacket = false;
  uint16_t _fields[6]{};
  float _normalized[5]{};
  String _aliases[6];
  Calibration _cal[5] = {
    {1000, 1503, 2000},
    {1000, 1486, 2000},
    {1000, 1493, 2000},
    {1006, 1511, 2000},
    {1000, 1498, 2000}
  };
  float _deadband = 0.03f;

  uint32_t serialConfig() const;
  void pushRaw(uint8_t value);
  void processDecoderByte(uint8_t value);
  void resetParser();
  void handlePacket(size_t packetLength);
  float normalize(uint16_t raw, const Calibration& c) const;
  float applyDeadband(float value) const;
  static String jsonEscape(const String& value);
};
