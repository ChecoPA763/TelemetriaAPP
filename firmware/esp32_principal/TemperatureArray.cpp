#include "TemperatureArray.h"

#include <math.h>

namespace lobos {
namespace {
constexpr uint32_t kChannelReadPeriodMs = 25;
constexpr uint32_t kReadingStaleMs = 1000;
}

bool TemperatureArray::select(uint8_t channel) {
  if (channel >= 8 || wire_ == nullptr) return false;
  wire_->beginTransmission(board::kPca9548Address);
  wire_->write(static_cast<uint8_t>(1U << channel));
  return wire_->endTransmission() == 0;
}

void TemperatureArray::begin(TwoWire& wire) {
  wire_ = &wire;
  for (uint8_t channel = 0; channel < board::kTemperatureChannelCount;
       ++channel) {
    present_[channel] = select(channel) &&
                        sensors_[channel].begin(board::kMlx90614Address, wire_);
    Serial.printf("[TEMP] canal=%u MLX=%s\n", channel,
                  present_[channel] ? "ok" : "ausente");
  }
  next_read_ms_ = millis();
}

void TemperatureArray::update() {
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - next_read_ms_) < 0) return;
  next_read_ms_ = now + kChannelReadPeriodMs;

  const uint8_t channel = next_channel_;
  next_channel_ = (next_channel_ + 1U) % board::kTemperatureChannelCount;
  if (!present_[channel] || !select(channel)) return;

  const double measured = sensors_[channel].readObjectTempC();
  if (!isfinite(measured) || measured < -40.0 || measured > 300.0) return;
  const long scaled = lround(measured * 10.0);
  value_[channel] = static_cast<int16_t>(constrain(scaled, -400L, 3000L));
  updated_ms_[channel] = now;
}

TemperatureArray::Reading TemperatureArray::reading(uint8_t channel) const {
  Reading out{};
  if (channel >= board::kTemperatureChannelCount) return out;
  const uint32_t now = millis();
  out.age_ms = updated_ms_[channel] == 0 ? UINT32_MAX
                                        : now - updated_ms_[channel];
  out.valid = present_[channel] && out.age_ms <= kReadingStaleMs;
  out.object_x10_c = value_[channel];
  return out;
}

bool TemperatureArray::hottest(int16_t& valueX10C) const {
  bool found = false;
  int16_t hottest = INT16_MIN;
  for (uint8_t channel = 0; channel < board::kTemperatureChannelCount;
       ++channel) {
    const Reading current = reading(channel);
    if (current.valid && (!found || current.object_x10_c > hottest)) {
      hottest = current.object_x10_c;
      found = true;
    }
  }
  if (found) valueX10C = hottest;
  return found;
}

}  // namespace lobos

