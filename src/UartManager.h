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
  bool start();
  void stop();

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
  uint32_t mainPacketCount() const { return _cmd0021Count; }
  uint32_t command0021Count() const { return _cmd0021Count; }
  uint32_t command0023Count() const { return _cmd0023Count; }
  uint32_t command0031Count() const { return _cmd0031Count; }
  uint32_t command0033Count() const { return _cmd0033Count; }
  uint32_t unknownCommandCount() const { return _unknownCommandCount; }
  uint8_t lastPacketType() const { return _lastRoute; }
  uint8_t lastPacketLength() const { return _lastOuterLength; }
  uint8_t lastRoute() const { return _lastRoute; }
  uint8_t lastInnerType() const { return _lastInnerType; }
  uint16_t lastInnerLength() const { return _lastInnerLength; }
  uint16_t lastCommand() const { return _lastCommand; }
  uint16_t lastDataLength() const { return _lastDataLength; }
  bool lastChecksumOk() const { return _lastChecksumOk; }
  unsigned long lastPacketMillis() const { return _lastPacketMillis; }
  bool hasMainPacket() const { return _hasMainPacket; }
  bool hasCommand0023() const { return _has0023; }
  uint16_t command0023Value1() const { return _cmd0023Value1; }
  uint8_t command0023Value2() const { return _cmd0023Value2; }
  uint16_t command0023Value3() const { return _cmd0023Value3; }
  bool hasCommand0031() const { return _has0031; }
  uint16_t command0031Value() const { return _cmd0031Value; }
  uint8_t command0031Status() const { return _cmd0031Status; }
  bool hasCommand0033() const { return _has0033; }
  uint16_t command0033Field(uint8_t index) const { return index < 6 ? _cmd0033Fields[index] : 0; }

  uint16_t rawField(uint8_t index) const;
  float normalizedField(uint8_t index) const;
  String fieldAlias(uint8_t index) const;
  void setFieldAlias(uint8_t index, const String& value);
  Calibration calibration(uint8_t index) const;
  void setCalibration(uint8_t index, uint16_t minV, uint16_t centerV, uint16_t maxV);
  void setDeadband(float value);
  float deadband() const { return _deadband; }

  String statusJson() const;

  // Replay the most recently received valid 0x0021 packet for ~1 s at 50 Hz.
  // Only field 5 is changed; all other bytes remain copied from the received packet.
  bool sendField5ForOneSecond(float normalizedValue);
  bool txActive() const { return _txActive; }
  bool txReady() const;
  String txStatus() const { return _txStatus; }
  float txTargetNormalized() const { return _txTargetNormalized; }
  uint16_t txField5Raw() const { return _txField5Raw; }
  uint32_t txPacketCount() const { return _txPacketCount; }

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
  uint32_t _cmd0021Count = 0;
  uint32_t _cmd0023Count = 0;
  uint32_t _cmd0031Count = 0;
  uint32_t _cmd0033Count = 0;
  uint32_t _unknownCommandCount = 0;
  uint8_t _lastRoute = 0;
  uint8_t _lastOuterLength = 0;
  uint8_t _lastInnerType = 0;
  uint16_t _lastInnerLength = 0;
  uint16_t _lastCommand = 0;
  uint16_t _lastDataLength = 0;
  bool _lastChecksumOk = false;
  unsigned long _lastPacketMillis = 0;
  bool _hasMainPacket = false;
  uint8_t _lastMainPacket[25]{};
  bool _hasLastMainPacketCopy = false;
  uint16_t _fields[6]{};
  bool _has0023 = false;
  uint16_t _cmd0023Value1 = 0;
  uint8_t _cmd0023Value2 = 0;
  uint16_t _cmd0023Value3 = 0;
  bool _has0031 = false;
  uint16_t _cmd0031Value = 0;
  uint8_t _cmd0031Status = 0;
  bool _has0033 = false;
  uint16_t _cmd0033Fields[6]{};
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

  // Non-blocking TX replay state.
  uint8_t _txPacket[25]{};
  bool _txActive = false;
  unsigned long _txStartMillis = 0;
  unsigned long _txNextMillis = 0;
  float _txTargetNormalized = 0.0f;
  uint16_t _txField5Raw = 0;
  uint32_t _txPacketCount = 0;
  String _txStatus = "bereit";

  uint32_t serialConfig() const;
  void pushRaw(uint8_t value);
  void processDecoderByte(uint8_t value);
  void resetParser();
  void handlePacket(size_t packetLength);
  float normalize(uint16_t raw, const Calibration& c) const;
  float applyDeadband(float value) const;
  uint16_t denormalize(float value, const Calibration& c) const;
  void serviceTxReplay();
  void updatePacketChecksum(uint8_t* packet, size_t packetLength) const;
  static String jsonEscape(const String& value);
};
