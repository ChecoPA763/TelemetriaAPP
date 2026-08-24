#include "HallSensor.h"

namespace lobos::hall {
namespace {

constexpr uint32_t kMinimumPulseSpacingUs = 250;
volatile uint32_t pulseCount = 0;
volatile uint32_t lastPulseUs = 0;
portMUX_TYPE hallMux = portMUX_INITIALIZER_UNLOCKED;

void IRAM_ATTR onPulse() {
  const uint32_t now = micros();
  portENTER_CRITICAL_ISR(&hallMux);
  if (lastPulseUs == 0 || now - lastPulseUs >= kMinimumPulseSpacingUs) {
    lastPulseUs = now;
    ++pulseCount;
  }
  portEXIT_CRITICAL_ISR(&hallMux);
}

}  // namespace

void begin(uint8_t pin) {
  pinMode(pin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pin), onPulse, FALLING);
}

Sample sample(uint32_t nowUs, uint32_t windowUs) {
  uint32_t count;
  uint32_t last;
  portENTER_CRITICAL(&hallMux);
  count = pulseCount;
  pulseCount = 0;
  last = lastPulseUs;
  portEXIT_CRITICAL(&hallMux);

  Sample out{};
  out.pulses = count > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(count);
  out.window_us = windowUs;
  out.last_pulse_age_us = last == 0 ? UINT32_MAX : nowUs - last;
  return out;
}

}  // namespace lobos::hall

