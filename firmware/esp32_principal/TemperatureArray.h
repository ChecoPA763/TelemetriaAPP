#pragma once

#include <Adafruit_MLX90614.h>
#include <Arduino.h>
#include <Wire.h>

#include "BoardConfig.h"

namespace lobos {

class TemperatureArray {
 public:
  struct Reading {
    int16_t object_x10_c;
    bool valid;
    uint32_t age_ms;
  };

  void begin(TwoWire& wire = Wire);
  void update();
  bool hottest(int16_t& value_x10_c) const;
  Reading reading(uint8_t channel) const;

 private:
  bool select(uint8_t channel);

  TwoWire* wire_ = nullptr;
  Adafruit_MLX90614 sensors_[board::kTemperatureChannelCount];
  bool present_[board::kTemperatureChannelCount]{};
  int16_t value_[board::kTemperatureChannelCount]{};
  uint32_t updated_ms_[board::kTemperatureChannelCount]{};
  uint8_t next_channel_ = 0;
  uint32_t next_read_ms_ = 0;
};

}  // namespace lobos

