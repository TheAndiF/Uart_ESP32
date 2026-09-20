#include "TestCandidateCatalog.h"
#include <cstring>

namespace {

struct ByteSpan {
  const uint8_t* data;
  uint8_t len;
};

static const uint8_t PAY_Z1[] = {0x00};
static const uint8_t PAY_O1[] = {0x01};
static const uint8_t PAY_F1[] = {0xFF};
static const uint8_t PAY_Z2[] = {0x00, 0x00};
static const uint8_t PAY_O2[] = {0x01, 0x00};
static const uint8_t PAY_F2[] = {0xFF, 0xFF};

static const uint8_t OBS_0021[] = {0x00,0x00,0xD3,0x05,0xCF,0x05,0xD5,0x05,0xDE,0x05,0xDE,0x05,0x54,0x01};
static const uint8_t OBS_0023[] = {0x8D,0x05,0x03,0xF3,0x04,0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t OBS_0031[] = {0x54,0x01,0x03};
static const uint8_t OBS_0033[] = {0x00,0x00,0xDE,0x05,0xD3,0x05,0xCF,0x05,0xD5,0x05,0xDE,0x05,0x54,0x01};
static const uint8_t ZERO_14[14] = {};
static const uint8_t ZERO_11[11] = {};
static const uint8_t ZERO_3[3] = {};

static const char* PROFILE_NAMES[8] = {"Z1","O1","F1","Z2","O2","F2","OBS","ZOBS"};
static const uint16_t B_COMMANDS[4] = {0x0021,0x0023,0x0031,0x0033};

static ByteSpan observedPayload(uint16_t cmd, bool zeros) {
  switch (cmd) {
    case 0x0021: return zeros ? ByteSpan{ZERO_14,14} : ByteSpan{OBS_0021,14};
    case 0x0023: return zeros ? ByteSpan{ZERO_11,11} : ByteSpan{OBS_0023,11};
    case 0x0031: return zeros ? ByteSpan{ZERO_3,3} : ByteSpan{OBS_0031,3};
    case 0x0033: return zeros ? ByteSpan{ZERO_14,14} : ByteSpan{OBS_0033,14};
    default: return {nullptr,0};
  }
}

static ByteSpan profilePayload(uint8_t profile, uint16_t cmd) {
  switch (profile) {
    case 0: return {PAY_Z1,1};
    case 1: return {PAY_O1,1};
    case 2: return {PAY_F1,1};
    case 3: return {PAY_Z2,2};
    case 4: return {PAY_O2,2};
    case 5: return {PAY_F2,2};
    case 6: return observedPayload(cmd, false);
    case 7: return observedPayload(cmd, true);
    default: return {nullptr,0};
  }
}

// Exact foreign/analogy frames used by family C. They are intentionally copied
// byte-for-byte from the candidate catalog, including their external length model.
static const uint8_t C0[] = {0xFF,0xFD,0x03,0x00,0x00,0x16,0x15,0x00};
static const uint8_t C1[] = {0xFF,0xFE,0x04,0x00,0x73,0x10,0x00,0x67,0x00};
static const uint8_t C2[] = {0xFF,0xFD,0x03,0x00,0x35,0x16,0x20,0x00};
static const uint8_t C3[] = {0xFF,0xFD,0x04,0x00,0x00,0x12,0x20,0x36};
static const uint8_t C4[] = {0xFF,0xFD,0x04,0x00,0x00,0x12,0x01,0x17};
static const uint8_t C5[] = {0xFF,0xFD,0x06,0x00,0x00,0x03,0x00,0x00,0x00,0x05,0x00};
static const uint8_t* C_FRAMES[6] = {C0,C1,C2,C3,C4,C5};
static const uint8_t C_LENGTHS[6] = {sizeof(C0),sizeof(C1),sizeof(C2),sizeof(C3),sizeof(C4),sizeof(C5)};
static const char* C_NAMES[6] = {"GET_FPV_INFO","REMOTER_GET_INFO","GET_SETTINGS","CAMERA_GET_MODE","CAMERA_GET_STATUS","HEARTBEAT_INNER"};

static const char* transportSuffix(uint8_t transport) {
  return transport == 0 ? "I" : (transport == 1 ? "R10" : "R30");
}

static uint8_t transportRoute(uint8_t transport) {
  return transport == 1 ? 0x10 : 0x30;
}

static void copyFrame(const uint8_t* src, uint8_t len, uint8_t* dst) {
  memcpy(dst, src, len);
}

} // namespace

uint8_t TestCandidateCatalog::buildInner(uint8_t marker, uint16_t command,
                                         const uint8_t* payload, uint8_t payloadLen,
                                         uint8_t* out) {
  const uint16_t ilen = 3U + payloadLen;
  out[0] = 0xFF;
  out[1] = marker;
  out[2] = (uint8_t)(ilen & 0xFFU);
  out[3] = (uint8_t)(ilen >> 8U);
  out[4] = (uint8_t)(command & 0xFFU);
  out[5] = (uint8_t)(command >> 8U);
  if (payloadLen && payload) memcpy(&out[6], payload, payloadLen);
  uint8_t cs = 0;
  for (uint8_t i = 2; i < (uint8_t)(6U + payloadLen); ++i) cs ^= out[i];
  out[6U + payloadLen] = cs;
  return (uint8_t)(7U + payloadLen);
}

uint8_t TestCandidateCatalog::wrap(uint8_t route, const uint8_t* inner, uint8_t innerLen,
                                   uint8_t* out) {
  out[0] = 0xFF;
  out[1] = 0xFB;
  out[2] = route;
  out[3] = innerLen;
  memcpy(&out[4], inner, innerLen);
  return (uint8_t)(innerLen + 4U);
}

bool TestCandidateCatalog::build(uint16_t catalogNumber, UartTestCandidate& out) {
  if (catalogNumber < 1 || catalogNumber > COUNT) return false;
  out = UartTestCandidate{};
  out.catalogNumber = catalogNumber;

  // Family A: 128 commands * 2 markers * 3 transports = 768.
  if (catalogNumber <= 768) {
    const uint16_t z = catalogNumber - 1U;
    const uint16_t cmd = z / 6U;
    const uint8_t variant = (uint8_t)(z % 6U);
    const uint8_t marker = variant < 3U ? 0xFD : 0xFE;
    const uint8_t transport = (uint8_t)(variant % 3U);
    uint8_t inner[32];
    const uint8_t innerLen = buildInner(marker, cmd, nullptr, 0, inner);
    out.length = transport == 0 ? innerLen : wrap(transportRoute(transport), inner, innerLen, out.frame);
    if (transport == 0) copyFrame(inner, innerLen, out.frame);
    out.priority = 3;
    snprintf(out.id, sizeof(out.id), "A-%04X-%s-%s", cmd, marker == 0xFD ? "FD" : "FE", transportSuffix(transport));
    return true;
  }

  // Family B: four observed commands * eight payload profiles * six marker/transport variants = 192.
  if (catalogNumber <= 960) {
    const uint16_t z = catalogNumber - 769U;
    const uint8_t commandIndex = (uint8_t)(z / 48U);
    const uint8_t withinCommand = (uint8_t)(z % 48U);
    const uint8_t profile = (uint8_t)(withinCommand / 6U);
    const uint8_t variant = (uint8_t)(withinCommand % 6U);
    const uint16_t cmd = B_COMMANDS[commandIndex];
    const uint8_t marker = variant < 3U ? 0xFD : 0xFE;
    const uint8_t transport = (uint8_t)(variant % 3U);
    const ByteSpan payload = profilePayload(profile, cmd);
    uint8_t inner[32];
    const uint8_t innerLen = buildInner(marker, cmd, payload.data, payload.len, inner);
    out.length = transport == 0 ? innerLen : wrap(transportRoute(transport), inner, innerLen, out.frame);
    if (transport == 0) copyFrame(inner, innerLen, out.frame);
    out.priority = profile >= 6U ? 1 : 2;
    snprintf(out.id, sizeof(out.id), "B-%04X-%s-%s-%s", cmd, PROFILE_NAMES[profile], marker == 0xFD ? "FD" : "FE", transportSuffix(transport));
    return true;
  }

  // Family C: six exact frames, each inner-only/R10/R30.
  if (catalogNumber <= 978) {
    const uint16_t z = catalogNumber - 961U;
    const uint8_t source = (uint8_t)(z / 3U);
    const uint8_t transport = (uint8_t)(z % 3U);
    const uint8_t* inner = C_FRAMES[source];
    const uint8_t innerLen = C_LENGTHS[source];
    out.length = transport == 0 ? innerLen : wrap(transportRoute(transport), inner, innerLen, out.frame);
    if (transport == 0) copyFrame(inner, innerLen, out.frame);
    out.priority = 2;
    snprintf(out.id, sizeof(out.id), "C-%s-%s", C_NAMES[source], transportSuffix(transport));
    return true;
  }

  // Family D: 11 mild anchor frames * the two historical timing labels.
  // The automatic runner deliberately overrides both labels with the user-selected
  // uniform test timing: 5 s total, send every 250 ms. The original IDs are kept
  // so every PDF test case remains traceable.
  const uint16_t z = catalogNumber - 979U;
  const uint8_t anchor = (uint8_t)(z / 2U);
  const bool initLabel = (z & 1U) != 0;
  const char* timing = initLabel ? "INIT" : "SLOW";

  uint8_t inner[32];
  uint8_t innerLen = 0;
  uint8_t transport = 0;
  const char* prefix = nullptr;

  switch (anchor) {
    case 0: innerLen = buildInner(0xFD,0x0021,nullptr,0,inner); transport=0; prefix="D-A-0021-FD-I"; break;
    case 1: innerLen = buildInner(0xFE,0x0021,nullptr,0,inner); transport=0; prefix="D-A-0021-FE-I"; break;
    case 2: innerLen = buildInner(0xFD,0x0021,nullptr,0,inner); transport=1; prefix="D-A-0021-FD-R10"; break;
    case 3: innerLen = buildInner(0xFE,0x0021,nullptr,0,inner); transport=2; prefix="D-A-0021-FE-R30"; break;
    case 4: innerLen = buildInner(0xFD,0x0031,nullptr,0,inner); transport=0; prefix="D-A-0031-FD-I"; break;
    case 5: innerLen = buildInner(0xFE,0x0031,nullptr,0,inner); transport=0; prefix="D-A-0031-FE-I"; break;
    case 6: innerLen = buildInner(0xFD,0x0031,nullptr,0,inner); transport=1; prefix="D-A-0031-FD-R10"; break;
    case 7: innerLen = buildInner(0xFE,0x0031,nullptr,0,inner); transport=2; prefix="D-A-0031-FE-R30"; break;
    case 8: copyFrame(C5, sizeof(C5), inner); innerLen=sizeof(C5); transport=0; prefix="D-C-HEARTBEAT_INNER-I"; break;
    case 9: copyFrame(C5, sizeof(C5), inner); innerLen=sizeof(C5); transport=1; prefix="D-C-HEARTBEAT_INNER-R10"; break;
    case 10: copyFrame(C5, sizeof(C5), inner); innerLen=sizeof(C5); transport=2; prefix="D-C-HEARTBEAT_INNER-R30"; break;
    default: return false;
  }

  out.length = transport == 0 ? innerLen : wrap(transportRoute(transport), inner, innerLen, out.frame);
  if (transport == 0) copyFrame(inner, innerLen, out.frame);
  out.priority = 0;
  snprintf(out.id, sizeof(out.id), "%s-%s", prefix, timing);
  return true;
}

uint16_t TestCandidateCatalog::catalogNumberForTestOrder(uint16_t orderIndex) {
  if (orderIndex >= COUNT) return 0;

  // P0: D, catalog 979..1000 (22)
  if (orderIndex < 22U) return (uint16_t)(979U + orderIndex);
  orderIndex -= 22U;

  // P1: B profiles OBS/ZOBS (profile indices 6,7), four command blocks.
  if (orderIndex < 48U) {
    const uint8_t cmdBlock = (uint8_t)(orderIndex / 12U);
    const uint8_t inBlock = (uint8_t)(orderIndex % 12U);
    const uint8_t profile = (uint8_t)(6U + inBlock / 6U);
    const uint8_t variant = (uint8_t)(inBlock % 6U);
    return (uint16_t)(769U + cmdBlock * 48U + profile * 6U + variant);
  }
  orderIndex -= 48U;

  // P2: B small payload profiles Z1..F2 (144).
  if (orderIndex < 144U) {
    const uint8_t cmdBlock = (uint8_t)(orderIndex / 36U);
    const uint8_t inBlock = (uint8_t)(orderIndex % 36U);
    const uint8_t profile = (uint8_t)(inBlock / 6U);
    const uint8_t variant = (uint8_t)(inBlock % 6U);
    return (uint16_t)(769U + cmdBlock * 48U + profile * 6U + variant);
  }
  orderIndex -= 144U;

  // P2: C analogies (18).
  if (orderIndex < 18U) return (uint16_t)(961U + orderIndex);
  orderIndex -= 18U;

  // P3: A (768).
  return (uint16_t)(1U + orderIndex);
}

String TestCandidateCatalog::frameHex(const UartTestCandidate& candidate) {
  String s;
  s.reserve((size_t)candidate.length * 3U + 1U);
  char b[4];
  for (uint8_t i = 0; i < candidate.length; ++i) {
    snprintf(b, sizeof(b), "%02X", candidate.frame[i]);
    if (i) s += ' ';
    s += b;
  }
  return s;
}
