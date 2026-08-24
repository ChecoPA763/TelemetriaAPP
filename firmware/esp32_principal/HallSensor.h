#pragma once

#include <Arduino.h>

namespace lobos::hall {

struct Sample {
  uint16_t pulses;
  uint32_t window_us;
  uint32_t last_pulse_age_us;
};

void begin(uint8_t pin);
Sample sample(uint32_t now_us, uint32_t window_us);

}  // namespace lobos::hall

