#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include <Preferences.h>
#include "TestCandidateCatalog.h"
#include <mbedtls/sha256.h>

class UartManager {
public:
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

  // Since v0.15 the UART transport is the common basis for console, decoder
  // and probe runner. These features no longer compete for an exclusive mode.
  void setEnabled(bool value) { _enabled = value; }
  bool enabled() const { return _enabled; }
  void setTxEnabled(bool value) { _txEnabled = value; }
  bool txEnabled() const { return _txEnabled; }
  String modeText() const;
  String txOwnerText() const;

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

  // Universal UART console. Incoming bytes are always copied into this ring
  // buffer while the UART transport is running, regardless of whether the web
  // decoder page is open. TX is independently gated by txEnabled().
  bool consoleTxReady() const;
  size_t consoleWrite(const uint8_t* data, size_t length);
  void clearConsole();
  uint32_t consoleSequence() const { return _consoleSequence; }
  uint32_t consoleTxBytes() const { return _consoleTxBytes; }
  String consoleChunkJson(uint32_t sinceSequence) const;

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

  // Automatic UART probe runner. Each candidate is sent for 5 s every 250 ms
  // while the existing decoder continues to observe D2.
  bool startProbeSweep(bool stopOnReaction = true);
  void stopProbeSweep();
  bool probeActive() const { return _probeState != ProbeState::Idle; }
  bool probeStopOnReaction() const { return _probeStopOnReaction; }
  String probeStatus() const { return _probeStatus; }
  uint16_t probeOrderIndex() const { return _probeOrderIndex; }
  uint16_t probeCatalogNumber() const { return _probeCandidate.catalogNumber; }
  String probeCandidateId() const { return String(_probeCandidate.id); }
  uint8_t probeCandidatePriority() const { return _probeCandidate.priority; }
  uint32_t probeCandidateSentCount() const { return _probeSentCount; }
  uint32_t probeCandidatesCompleted() const { return _probeCompleted; }
  bool probeReactionDetected() const { return _probeReactionFlags != 0; }
  String probeReactionText() const;

  // BX3 image transfer. The transfer owns UART TX/RX while active. Since v0.19
  // blocks are validated with POSIX cksum CRC32, while SHA-256 is retained only
  // for the complete image. Since v0.20 the source may also be a prebuilt
  // snapshot file from /tmp or /var/tmp, e.g. /tmp/mtd7.img.gz.
  bool startImageTransfer(const String& source = "/dev/mtd7ro", uint32_t startBlock = 0);
  void abortImageTransfer(const String& reason = "manuell abgebrochen");
  bool imageActive() const;
  bool imageBlockReady() const;
  uint32_t imageReadyBlock() const { return _imageReadyBlock; }
  size_t imageReadyBlockLength() const { return imageBlockReady() ? _imageBlockLength : 0U; }
  bool imageAcknowledgeBlock(uint32_t blockNumber);
  bool confirmImageConsoleAccess();
  bool imageRestartRequired() const { return _imageRestartRequired; }
  bool imageRestartAllowed() const { return _imageRestartRequired && _imageConsoleConfirmed && !imageActive(); }
  String imageStatusJson() const;
  size_t imageReadBlock(uint32_t blockNumber, size_t offset, uint8_t* out, size_t maxLen) const;

private:
  static constexpr size_t RAW_CAPACITY = 512;
  static constexpr size_t PACKET_CAPACITY = 128;
  static constexpr size_t CONSOLE_CAPACITY = 8192;
  static constexpr size_t IMAGE_BLOCK_SIZE = 32768;
  static constexpr size_t UART_RX_BUFFER_SIZE = 16384;
  static constexpr size_t IMAGE_RX_DRAIN_BUDGET = 8192;
  static constexpr uint8_t IMAGE_MAX_RETRIES = 5;

  HardwareSerial _uart1{1};
  HardwareSerial _uart2{2};
  HardwareSerial* _serial = nullptr;

  bool _enabled = false;
  bool _txEnabled = false;
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

  uint8_t _console[CONSOLE_CAPACITY]{};
  size_t _consoleHead = 0;
  size_t _consoleCount = 0;
  uint32_t _consoleSequence = 0;
  uint32_t _consoleTxBytes = 0;

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

  enum class ProbeState : uint8_t { Idle = 0, Baseline = 1, Testing = 2, Gap = 3 };
  ProbeState _probeState = ProbeState::Idle;
  bool _probeStopOnReaction = true;
  uint16_t _probeOrderIndex = 0;
  uint32_t _probeCompleted = 0;
  UartTestCandidate _probeCandidate{};
  String _probeStatus = "bereit";
  unsigned long _probeStateStartMillis = 0;
  unsigned long _probeNextSendMillis = 0;
  unsigned long _probeLastProgressMillis = 0;
  uint32_t _probeSentCount = 0;
  uint32_t _probeReactionFlags = 0;
  uint16_t _probeFirstUnknownCommand = 0;
  uint16_t _probeMaxFieldDelta = 0;
  uint32_t _probeBaselineCounts[4]{};
  uint32_t _probeCandidateStartCounts[4]{};
  uint32_t _probeCandidateStartValid = 0;
  uint32_t _probeCandidateStartUnknown = 0;
  uint16_t _probeStartFields0021[6]{};
  uint16_t _probeStartFields0033[6]{};
  uint16_t _probeStart0031Value = 0;
  uint8_t _probeStart0031Status = 0;
  uint8_t _probeStart0021Meta[2]{};
  uint8_t _probeStart0033Meta[2]{};
  bool _probeStartHas0021 = false;
  bool _probeStartHas0031 = false;
  bool _probeStartHas0033 = false;
  uint8_t _last0021Meta[2]{};
  uint8_t _last0033Meta[2]{};
  uint8_t _seenCommandBits[8192]{}; // 65536 command IDs, 1 bit each

  enum class ImageState : uint8_t {
    Idle = 0, WaitMtd = 1, WaitSetup = 2, WaitBegin = 3, WaitSize = 4,
    WaitCrc = 5, RxBase64 = 6, BlockReady = 7, WaitTotalSha = 8,
    Done = 9, Error = 10
  };
  ImageState _imageState = ImageState::Idle;
  String _imageSource = "/dev/mtd7ro";
  String _imageStatus = "bereit";
  String _imageLine;
  uint32_t _imageTotalSize = 0;
  uint32_t _imageTotalBlocks = 0;
  uint32_t _imageCurrentBlock = 0;
  uint32_t _imageReadyBlock = 0xFFFFFFFFUL;
  uint32_t _imageLastAckedBlock = 0xFFFFFFFFUL;
  uint32_t _imageStartBlock = 0;
  uint32_t _imageExpectedSize = 0;
  uint32_t _imageBlockLength = 0;
  uint8_t _imageBlock[IMAGE_BLOCK_SIZE]{};
  uint32_t _imageExpectedCrc = 0;
  uint32_t _imageBlockCrc = 0;
  char _imageLocalTotalSha[65]{};
  char _imageRemoteTotalSha[65]{};
  uint8_t _imageRetries = 0;
  uint32_t _imageRetryTotal = 0;
  uint32_t _imageErrors = 0;
  uint32_t _imageTimeoutErrors = 0;
  uint32_t _imageCrcErrors = 0;
  uint32_t _imageShaErrors = 0;
  uint32_t _imageBase64Errors = 0;
  uint32_t _imageMarkerErrors = 0;
  uint32_t _imageSizeErrors = 0;
  uint32_t _imageOtherErrors = 0;
  uint64_t _imageAcceptedBytes = 0;
  unsigned long _imageStartedMillis = 0;
  unsigned long _imageLastRxMillis = 0;
  bool _imageOverallVerify = false;
  bool _imageMtdMarkerSeen = false;
  bool _imageSourceIsFile = false;
  bool _imageShaStarted = false;
  bool _imageRestartRequired = false;
  bool _imageConsoleConfirmed = false;
  mbedtls_sha256_context _imageSha{};

  uint32_t serialConfig() const;
  void pushRaw(uint8_t value);
  void pushConsole(uint8_t value);
  void processDecoderByte(uint8_t value);
  void resetParser();
  void handlePacket(size_t packetLength);
  float normalize(uint16_t raw, const Calibration& c) const;
  float applyDeadband(float value) const;
  uint16_t denormalize(float value, const Calibration& c) const;
  bool baseTxReady() const;
  void serviceTxReplay();
  void serviceProbe();
  void beginProbeCandidate();
  void finishProbeCandidate();
  void observeProbeFrame(uint16_t command, const uint8_t* data, size_t dataLen, bool commandSeenBefore);
  void setProbeReaction(uint32_t flag, const char* text);
  void updatePacketChecksum(uint8_t* packet, size_t packetLength) const;
  void processImageByte(uint8_t value);
  void processImageLine(String line);
  bool setupImageShellHelper();
  bool requestImageBlock();
  void retryImageBlock(const String& reason);
  void recordImageError(const String& reason);
  void requestImageTotalSha();
  void finishImageSha();
  bool validateImageSource(const String& source, uint8_t& mtdNo, bool& fileSource) const;
  static uint32_t posixCksum(const uint8_t* data, size_t length);
  static String jsonEscape(const String& value);
};
