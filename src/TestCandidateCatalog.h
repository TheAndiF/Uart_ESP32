#pragma once

#include <Arduino.h>

struct UartTestCandidate {
  uint16_t catalogNumber = 0;   // 1..1000, identical to the PDF catalog numbering
  uint8_t priority = 3;         // 0..3 (P0..P3)
  char id[48]{};
  uint8_t frame[32]{};
  uint8_t length = 0;
};

class TestCandidateCatalog {
public:
  static constexpr uint16_t COUNT = 1000;

  // Builds one of the 1000 catalog entries from the test-candidate document.
  static bool build(uint16_t catalogNumber, UartTestCandidate& out);

  // Automatic sweep order follows the document priorities:
  // P0 (family D), P1 (B OBS/ZOBS), P2 (remaining B + C), P3 (family A).
  // orderIndex is zero based (0..999), return value is the PDF catalog number (1..1000).
  static uint16_t catalogNumberForTestOrder(uint16_t orderIndex);

  static String frameHex(const UartTestCandidate& candidate);

private:
  static uint8_t buildInner(uint8_t marker, uint16_t command,
                            const uint8_t* payload, uint8_t payloadLen,
                            uint8_t* out);
  static uint8_t wrap(uint8_t route, const uint8_t* inner, uint8_t innerLen,
                      uint8_t* out);
};
