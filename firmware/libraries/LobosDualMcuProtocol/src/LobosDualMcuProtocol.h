#pragma once

#include <stddef.h>
#include <stdint.h>

namespace lobos {

constexpr uint8_t kMagic0 = 'L';
constexpr uint8_t kMagic1 = 'R';
constexpr uint8_t kProtocolVersion = 0;
constexpr size_t kHeaderSize = 6;
constexpr size_t kCrcSize = 2;
constexpr size_t kRawPayloadSize = 44;
constexpr size_t kProcessedPayloadSize = 48;
constexpr size_t kRawPacketSize = kHeaderSize + kRawPayloadSize + kCrcSize;
constexpr size_t kProcessedPacketSize =
    kHeaderSize + kProcessedPayloadSize + kCrcSize;
constexpr size_t kMaxPacketSize = 64;
constexpr size_t kMaxCobsFrameSize = 68;

enum class MessageType : uint8_t {
  RawSensors = 0x01,
  ProcessedTelemetry = 0x02,
  Heartbeat = 0x03,
};

enum SensorFlags : uint16_t {
  SensorHallValid = 1U << 0,
  SensorTemperatureValid = 1U << 1,
  SensorChassisFault = 1U << 2,
  SensorBms1Available = 1U << 3,
  SensorBms2Available = 1U << 4,
  SensorSimulation = 1U << 15,
};

enum BmsFlags : uint8_t {
  BmsConnected = 1U << 0,
  BmsDataValid = 1U << 1,
  BmsCharging = 1U << 2,
  BmsDischarging = 1U << 3,
  BmsAlarm = 1U << 4,
  BmsSimulation = 1U << 7,
};

enum StateFlags : uint16_t {
  StateSpeedValid = 1U << 0,
  StateTemperatureValid = 1U << 1,
  StateChassisFault = 1U << 2,
  StateBms1Valid = 1U << 3,
  StateBms2Valid = 1U << 4,
  StateSimulation = 1U << 15,
};

enum AlarmFlags : uint16_t {
  AlarmNone = 0,
  AlarmChassis = 1U << 0,
  AlarmTemperature = 1U << 1,
  AlarmBms1Missing = 1U << 2,
  AlarmBms2Missing = 1U << 3,
  AlarmBmsTemperature = 1U << 4,
  AlarmLowSoc = 1U << 5,
};

enum class DecodeStatus : uint8_t {
  Ok,
  TooShort,
  BadMagic,
  UnsupportedVersion,
  WrongType,
  WrongLength,
  BadCrc,
  OutputTooSmall,
  BadCobs,
};

struct BmsSample {
  uint16_t voltage_mV;
  int16_t current_x10_A;
  int16_t temperature_x10_C;
  uint16_t soc_x10_pct;
  uint16_t age_ms;
  uint8_t flags;
};

struct RawSensorFrame {
  uint32_t acquisition_seq;
  uint32_t source_uptime_ms;
  uint16_t hall_pulse_delta;
  uint32_t hall_window_us;
  uint32_t last_pulse_age_us;
  int16_t temperature_x10_C;
  uint16_t sensor_flags;
  BmsSample bms1;
  BmsSample bms2;
};

struct ProcessedTelemetryFrame {
  uint32_t processed_seq;
  uint32_t source_acquisition_seq;
  uint32_t source_uptime_ms;
  uint16_t speed_x10_kmh;
  uint16_t rpm;
  int16_t temperature_x10_C;
  uint16_t state_flags;
  uint16_t alarm_flags;
  BmsSample bms1;
  BmsSample bms2;
  uint16_t processing_time_us;
  uint16_t raw_age_ms;
};

inline void putU16(uint8_t* output, size_t& offset, uint16_t value) {
  output[offset++] = static_cast<uint8_t>(value & 0xFFU);
  output[offset++] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

inline void putI16(uint8_t* output, size_t& offset, int16_t value) {
  putU16(output, offset, static_cast<uint16_t>(value));
}

inline void putU32(uint8_t* output, size_t& offset, uint32_t value) {
  output[offset++] = static_cast<uint8_t>(value & 0xFFU);
  output[offset++] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
  output[offset++] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
  output[offset++] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
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

inline uint16_t crc16Ccitt(const uint8_t* data, size_t length) {
  uint16_t crc = 0xFFFFU;
  for (size_t i = 0; i < length; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8U;
    for (uint8_t bit = 0; bit < 8U; ++bit) {
      crc = (crc & 0x8000U) != 0U
                ? static_cast<uint16_t>((crc << 1U) ^ 0x1021U)
                : static_cast<uint16_t>(crc << 1U);
    }
  }
  return crc;
}

inline void putBms(uint8_t* output, size_t& offset, const BmsSample& bms) {
  putU16(output, offset, bms.voltage_mV);
  putI16(output, offset, bms.current_x10_A);
  putI16(output, offset, bms.temperature_x10_C);
  putU16(output, offset, bms.soc_x10_pct);
  putU16(output, offset, bms.age_ms);
  output[offset++] = bms.flags;
}

inline BmsSample getBms(const uint8_t* input, size_t& offset) {
  BmsSample bms{};
  bms.voltage_mV = getU16(input, offset);
  bms.current_x10_A = getI16(input, offset);
  bms.temperature_x10_C = getI16(input, offset);
  bms.soc_x10_pct = getU16(input, offset);
  bms.age_ms = getU16(input, offset);
  bms.flags = input[offset++];
  return bms;
}

inline size_t beginPacket(uint8_t* output, MessageType type,
                          uint16_t payloadLength) {
  size_t offset = 0;
  output[offset++] = kMagic0;
  output[offset++] = kMagic1;
  output[offset++] = kProtocolVersion;
  output[offset++] = static_cast<uint8_t>(type);
  putU16(output, offset, payloadLength);
  return offset;
}

inline size_t finishPacket(uint8_t* output, size_t offset) {
  const uint16_t crc = crc16Ccitt(output, offset);
  putU16(output, offset, crc);
  return offset;
}

inline size_t encodeRawPacket(const RawSensorFrame& frame, uint8_t* output,
                              size_t capacity) {
  if (capacity < kRawPacketSize) {
    return 0;
  }
  size_t offset = beginPacket(output, MessageType::RawSensors, kRawPayloadSize);
  putU32(output, offset, frame.acquisition_seq);
  putU32(output, offset, frame.source_uptime_ms);
  putU16(output, offset, frame.hall_pulse_delta);
  putU32(output, offset, frame.hall_window_us);
  putU32(output, offset, frame.last_pulse_age_us);
  putI16(output, offset, frame.temperature_x10_C);
  putU16(output, offset, frame.sensor_flags);
  putBms(output, offset, frame.bms1);
  putBms(output, offset, frame.bms2);
  return finishPacket(output, offset);
}

inline size_t encodeProcessedPacket(const ProcessedTelemetryFrame& frame,
                                    uint8_t* output, size_t capacity) {
  if (capacity < kProcessedPacketSize) {
    return 0;
  }
  size_t offset =
      beginPacket(output, MessageType::ProcessedTelemetry, kProcessedPayloadSize);
  putU32(output, offset, frame.processed_seq);
  putU32(output, offset, frame.source_acquisition_seq);
  putU32(output, offset, frame.source_uptime_ms);
  putU16(output, offset, frame.speed_x10_kmh);
  putU16(output, offset, frame.rpm);
  putI16(output, offset, frame.temperature_x10_C);
  putU16(output, offset, frame.state_flags);
  putU16(output, offset, frame.alarm_flags);
  putBms(output, offset, frame.bms1);
  putBms(output, offset, frame.bms2);
  putU16(output, offset, frame.processing_time_us);
  putU16(output, offset, frame.raw_age_ms);
  return finishPacket(output, offset);
}

inline DecodeStatus validatePacket(const uint8_t* packet, size_t length,
                                   MessageType expectedType,
                                   uint16_t expectedPayloadLength) {
  if (length < kHeaderSize + kCrcSize) {
    return DecodeStatus::TooShort;
  }
  if (packet[0] != kMagic0 || packet[1] != kMagic1) {
    return DecodeStatus::BadMagic;
  }
  if (packet[2] != kProtocolVersion) {
    return DecodeStatus::UnsupportedVersion;
  }
  if (packet[3] != static_cast<uint8_t>(expectedType)) {
    return DecodeStatus::WrongType;
  }
  size_t offset = 4;
  const uint16_t payloadLength = getU16(packet, offset);
  if (payloadLength != expectedPayloadLength ||
      length != kHeaderSize + payloadLength + kCrcSize) {
    return DecodeStatus::WrongLength;
  }
  size_t crcOffset = length - kCrcSize;
  const uint16_t expectedCrc = getU16(packet, crcOffset);
  return expectedCrc == crc16Ccitt(packet, length - kCrcSize)
             ? DecodeStatus::Ok
             : DecodeStatus::BadCrc;
}

inline DecodeStatus decodeRawPacket(const uint8_t* packet, size_t length,
                                    RawSensorFrame& frame) {
  const DecodeStatus status = validatePacket(
      packet, length, MessageType::RawSensors, kRawPayloadSize);
  if (status != DecodeStatus::Ok) {
    return status;
  }
  size_t offset = kHeaderSize;
  frame.acquisition_seq = getU32(packet, offset);
  frame.source_uptime_ms = getU32(packet, offset);
  frame.hall_pulse_delta = getU16(packet, offset);
  frame.hall_window_us = getU32(packet, offset);
  frame.last_pulse_age_us = getU32(packet, offset);
  frame.temperature_x10_C = getI16(packet, offset);
  frame.sensor_flags = getU16(packet, offset);
  frame.bms1 = getBms(packet, offset);
  frame.bms2 = getBms(packet, offset);
  return DecodeStatus::Ok;
}

inline DecodeStatus decodeProcessedPacket(const uint8_t* packet, size_t length,
                                          ProcessedTelemetryFrame& frame) {
  const DecodeStatus status = validatePacket(
      packet, length, MessageType::ProcessedTelemetry, kProcessedPayloadSize);
  if (status != DecodeStatus::Ok) {
    return status;
  }
  size_t offset = kHeaderSize;
  frame.processed_seq = getU32(packet, offset);
  frame.source_acquisition_seq = getU32(packet, offset);
  frame.source_uptime_ms = getU32(packet, offset);
  frame.speed_x10_kmh = getU16(packet, offset);
  frame.rpm = getU16(packet, offset);
  frame.temperature_x10_C = getI16(packet, offset);
  frame.state_flags = getU16(packet, offset);
  frame.alarm_flags = getU16(packet, offset);
  frame.bms1 = getBms(packet, offset);
  frame.bms2 = getBms(packet, offset);
  frame.processing_time_us = getU16(packet, offset);
  frame.raw_age_ms = getU16(packet, offset);
  return DecodeStatus::Ok;
}

inline size_t cobsEncode(const uint8_t* input, size_t length, uint8_t* output,
                         size_t capacity) {
  if (capacity == 0) {
    return 0;
  }
  size_t readIndex = 0;
  size_t writeIndex = 1;
  size_t codeIndex = 0;
  uint8_t code = 1;

  while (readIndex < length) {
    if (input[readIndex] == 0) {
      if (codeIndex >= capacity) {
        return 0;
      }
      output[codeIndex] = code;
      code = 1;
      codeIndex = writeIndex++;
      if (writeIndex > capacity) {
        return 0;
      }
      ++readIndex;
    } else {
      if (writeIndex >= capacity) {
        return 0;
      }
      output[writeIndex++] = input[readIndex++];
      ++code;
      if (code == 0xFFU) {
        output[codeIndex] = code;
        code = 1;
        codeIndex = writeIndex++;
        if (writeIndex > capacity) {
          return 0;
        }
      }
    }
  }

  if (codeIndex >= capacity) {
    return 0;
  }
  output[codeIndex] = code;
  return writeIndex;
}

inline DecodeStatus cobsDecode(const uint8_t* input, size_t length,
                               uint8_t* output, size_t capacity,
                               size_t& outputLength) {
  outputLength = 0;
  size_t readIndex = 0;
  size_t writeIndex = 0;

  while (readIndex < length) {
    const uint8_t code = input[readIndex];
    if (code == 0 || readIndex + code > length + 1U) {
      return DecodeStatus::BadCobs;
    }
    ++readIndex;
    for (uint8_t i = 1; i < code; ++i) {
      if (readIndex >= length || writeIndex >= capacity) {
        return readIndex >= length ? DecodeStatus::BadCobs
                                   : DecodeStatus::OutputTooSmall;
      }
      output[writeIndex++] = input[readIndex++];
    }
    if (code != 0xFFU && readIndex < length) {
      if (writeIndex >= capacity) {
        return DecodeStatus::OutputTooSmall;
      }
      output[writeIndex++] = 0;
    }
  }

  outputLength = writeIndex;
  return DecodeStatus::Ok;
}

inline size_t makeCobsFrame(const uint8_t* packet, size_t packetLength,
                            uint8_t* output, size_t capacity) {
  if (capacity < 2U) {
    return 0;
  }
  const size_t encoded = cobsEncode(packet, packetLength, output, capacity - 1U);
  if (encoded == 0 || encoded >= capacity) {
    return 0;
  }
  output[encoded] = 0;
  return encoded + 1U;
}

}  // namespace lobos

