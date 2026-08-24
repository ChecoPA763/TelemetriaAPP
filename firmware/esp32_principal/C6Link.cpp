#include "C6Link.h"

#include "BoardConfig.h"

namespace lobos {

void C6Link::begin(HardwareSerial& serial, uint32_t baud, int8_t rxPin) {
  serial_ = &serial;
  // GPIO36 es solo RX: el ESP32 principal no necesita transmitirle nada al C6.
  serial_->begin(baud, SERIAL_8N1, rxPin, -1);
}

void C6Link::accept(const c6::DualBmsFrame& frame) {
  if (has_frame_) {
    const uint16_t delta = static_cast<uint16_t>(frame.sequence - last_sequence_);
    if (delta == 0) {
      return;
    } else if (delta < 0x8000U) {
      if (delta > 1U) metrics_.sequence_gaps += delta - 1U;
    } else {
      ++metrics_.restarts;
    }
  }
  latest_ = frame;
  has_frame_ = true;
  last_sequence_ = frame.sequence;
  received_at_ms_ = millis();
  ++metrics_.frames;
}

void C6Link::update() {
  if (serial_ == nullptr) return;
  while (serial_->available() > 0) {
    const uint8_t byte = static_cast<uint8_t>(serial_->read());
    if (length_ == 0 && byte != c6::kMagic0) continue;
    if (length_ == 1 && byte != c6::kMagic1) {
      length_ = byte == c6::kMagic0 ? 1U : 0U;
      continue;
    }
    buffer_[length_++] = byte;
    if (length_ < c6::kFrameLength) continue;

    c6::DualBmsFrame decoded{};
    if (c6::decode(buffer_, sizeof(buffer_), decoded)) {
      accept(decoded);
    } else {
      ++metrics_.decode_errors;
    }
    length_ = 0;
  }
}

bool C6Link::fresh() const {
  return has_frame_ && millis() - received_at_ms_ <= board::kC6TimeoutMs;
}

BmsSample C6Link::convert(const c6::BmsRecord& source) const {
  BmsSample out{};
  const uint32_t localAge = has_frame_ ? millis() - received_at_ms_ : UINT32_MAX;
  const uint64_t totalAge =
      static_cast<uint64_t>(source.age_ms) + static_cast<uint64_t>(localAge);
  out.age_ms = totalAge > UINT16_MAX ? UINT16_MAX
                                     : static_cast<uint16_t>(totalAge);
  if (!fresh()) return out;

  out.voltage_mV = source.pack_mV;
  out.current_x10_A = source.current_x10_A;
  out.temperature_x10_C = source.temperature_x10_C;
  out.soc_x10_pct = source.soc_x10_pct;
  if ((source.flags & c6::Connected) != 0) out.flags |= BmsConnected;
  if ((source.flags & c6::StateValid) != 0) out.flags |= BmsDataValid;
  if ((source.flags & c6::ChargeMos) != 0) out.flags |= BmsCharging;
  if ((source.flags & c6::DischargeMos) != 0) out.flags |= BmsDischarging;
  return out;
}

void C6Link::samples(BmsSample& bms1, BmsSample& bms2) const {
  bms1 = convert(latest_.bms1);
  bms2 = convert(latest_.bms2);
}

C6LinkMetrics C6Link::metrics() const {
  C6LinkMetrics out = metrics_;
  out.last_frame_age_ms = has_frame_ ? millis() - received_at_ms_ : UINT32_MAX;
  return out;
}

}  // namespace lobos

