#pragma once

#include <Arduino.h>

namespace lobos::board {

constexpr uint32_t kDebugBaud = 115200;

// UART interno de la tarjeta Dual MCU (ESP32 <-> RP2040).
constexpr uint8_t kRpRxPin = 16;
constexpr uint8_t kRpTxPin = 17;
constexpr uint32_t kRpBaud = 115200;

// Enlace independiente: TX del ESP32-C6 -> GPIO36/SENSOR_VP del ESP32.
// GPIO36 es solo entrada, justo lo que se necesita para este enlace unidireccional.
constexpr uint8_t kC6RxPin = 36;
constexpr uint32_t kC6Baud = 115200;

constexpr uint8_t kHallPin = 2;
constexpr uint8_t kPc817Pin = 4;
constexpr bool kPc817FaultLevel = LOW;  // Cambiar solo después de medir el circuito.

constexpr uint8_t kI2cSdaPin = 21;
constexpr uint8_t kI2cSclPin = 22;
constexpr uint8_t kPca9548Address = 0x70;
constexpr uint8_t kMlx90614Address = 0x5A;
constexpr uint8_t kTemperatureChannelCount = 4;

constexpr uint32_t kRawPeriodUs = 50000;       // 20 Hz hacia RP2040.
constexpr uint32_t kMqttPeriodMs = 200;        // 5 Hz hacia broker/app.
constexpr uint32_t kMetricsPeriodMs = 5000;
constexpr uint32_t kRpTimeoutUs = 500000;
constexpr uint32_t kC6TimeoutMs = 4000;

}  // namespace lobos::board

