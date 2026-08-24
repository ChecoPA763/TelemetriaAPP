#pragma once

#include <stddef.h>
#include <stdint.h>

namespace lobos::c6 {

constexpr uint8_t kMagic0 = 0xB5;
constexpr uint8_t kMagic1 = 0x4D;
constexpr uint8_t kVersion = 2;
constexpr uint8_t kDualBmsType = 1;
constexpr uint16_t kPayloadLength = 92;
constexpr size_t kCellCount = 16;
constexpr size_t kBmsRecordLength = 43;
constexpr size_t kFrameLength = 100;

enum BmsFlags : uint8_t {
  Connected = 1U << 0,
  ChargeMos = 1U << 1,
  DischargeMos = 1U << 2,
  ChargerDetected = 1U << 3,
  LoadDetected = 1U << 4,
  CellsValid = 1U << 5,
  StateValid = 1U << 6,
};

struct BmsRecord {
  uint8_t flags;
  uint16_t age_ms;
  uint16_t pack_mV;
  int16_t current_x10_A;
  uint16_t soc_x10_pct;
  int16_t temperature_x10_C;
  uint16_t cells_mV[kCellCount];
};

struct DualBmsFrame {
  uint16_t sequence;
  uint32_t source_uptime_ms;
  BmsRecord bms1;
  BmsRecord bms2;
};

inline uint16_t crc16Modbus(const uint8_t* data, size_t length) {
  uint16_t crc = 0xFFFFU;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8U; ++bit) {
      crc = (crc & 1U) != 0U ? static_cast<uint16_t>((crc >> 1U) ^ 0xA001U)
                             : static_cast<uint16_t>(crc >> 1U);
    }
  }
  return crc;
}

inline void putU16(uint8_t* output, size_t& offset, uint16_t value) {
  output[offset++] = static_cast<uint8_t>(value);
  output[offset++] = static_cast<uint8_t>(value >> 8U);
}

inline void putI16(uint8_t* output, size_t& offset, int16_t value) {
  putU16(output, offset, static_cast<uint16_t>(value));
}

inline void putU32(uint8_t* output, size_t& offset, uint32_t value) {
  output[offset++] = static_cast<uint8_t>(value);
  output[offset++] = static_cast<uint8_t>(value >> 8U);
  output[offset++] = static_cast<uint8_t>(value >> 16U);
  output[offset++] = static_cast<uint8_t>(value >> 24U);
}

inline uint16_t getU16(const uint8_t* input, size_t& offset) {
  const uint16_t value = static_cast<uint16_t>(input[offset]) |
                         (static_cast<uint16_t>(input[offset + 1U]) << 8U);
  offset += 2U;
  return value;
}

inline int16_t getI16(const uint8_t* input, size_t& offset) {
  return static_cast<int16_t>(getU16(input, offset));
}

inline uint32_t getU32(const uint8_t* input, size_t& offset) {
  const uint32_t value = static_cast<uint32_t>(input[offset]) |
                         (static_cast<uint32_t>(input[offset + 1U]) << 8U) |
                         (static_cast<uint32_t>(input[offset + 2U]) << 16U) |
                         (static_cast<uint32_t>(input[offset + 3U]) << 24U);
  offset += 4U;
  return value;
}

inline void putBms(uint8_t* output, size_t& offset, const BmsRecord& bms) {
  output[offset++] = bms.flags;
  putU16(output, offset, bms.age_ms);
  putU16(output, offset, bms.pack_mV);
  putI16(output, offset, bms.current_x10_A);
  putU16(output, offset, bms.soc_x10_pct);
  putI16(output, offset, bms.temperature_x10_C);
  for (size_t i = 0; i < kCellCount; ++i) putU16(output, offset, bms.cells_mV[i]);
}

inline BmsRecord getBms(const uint8_t* input, size_t& offset) {
  BmsRecord bms{};
  bms.flags = input[offset++];
  bms.age_ms = getU16(input, offset);
  bms.pack_mV = getU16(input, offset);
  bms.current_x10_A = getI16(input, offset);
  bms.soc_x10_pct = getU16(input, offset);
  bms.temperature_x10_C = getI16(input, offset);
  for (size_t i = 0; i < kCellCount; ++i) bms.cells_mV[i] = getU16(input, offset);
  return bms;
}

inline size_t encode(const DualBmsFrame& frame, uint8_t* output,
                     size_t capacity) {
  if (capacity < kFrameLength) return 0;
  size_t offset = 0;
  output[offset++] = kMagic0;
  output[offset++] = kMagic1;
  output[offset++] = kVersion;
  output[offset++] = kDualBmsType;
  putU16(output, offset, kPayloadLength);
  putU16(output, offset, frame.sequence);
  putU32(output, offset, frame.source_uptime_ms);
  putBms(output, offset, frame.bms1);
  putBms(output, offset, frame.bms2);
  putU16(output, offset, crc16Modbus(output, offset));
  return offset == kFrameLength ? offset : 0;
}

inline bool decode(const uint8_t* input, size_t length, DualBmsFrame& frame) {
  if (length != kFrameLength || input[0] != kMagic0 || input[1] != kMagic1 ||
      input[2] != kVersion || input[3] != kDualBmsType) {
    return false;
  }
  size_t offset = 4;
  if (getU16(input, offset) != kPayloadLength) return false;
  size_t crcOffset = kFrameLength - 2U;
  const uint16_t receivedCrc = getU16(input, crcOffset);
  if (receivedCrc != crc16Modbus(input, kFrameLength - 2U)) return false;
  frame.sequence = getU16(input, offset);
  frame.source_uptime_ms = getU32(input, offset);
  frame.bms1 = getBms(input, offset);
  frame.bms2 = getBms(input, offset);
  return offset == kFrameLength - 2U;
}

}  // namespace lobos::c6

