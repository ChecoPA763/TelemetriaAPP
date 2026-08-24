#pragma once

#include <Arduino.h>

namespace lobos::board {

constexpr uint32_t kDebugBaud = 115200;
constexpr uint8_t kMainTxPin = 0;
constexpr uint8_t kMainRxPin = 1;
constexpr uint32_t kMainBaud = 115200;

constexpr uint8_t kDfTxPin = 8;   // RP TX -> RX del DFPlayer.
constexpr uint8_t kDfRxPin = 9;   // RP RX <- TX del DFPlayer.
constexpr uint32_t kDfBaud = 9600;

constexpr uint8_t kTftDcPin = 14;
constexpr uint8_t kTftResetPin = 15;
constexpr uint8_t kTftSckPin = 18;
constexpr uint8_t kTftMosiPin = 19;
constexpr uint8_t kTftCsPin = 21;
constexpr uint8_t kTftBacklightPin = 22;

constexpr uint8_t kMagnetsPerRevolution = 4;
// Valor provisional del banco. Hay que medir la circunferencia rodada real.
constexpr uint16_t kWheelCircumferenceMm = 1257;
constexpr uint32_t kRpmMeasurementWindowUs = 200000;
constexpr uint32_t kHallStopTimeoutUs = 500000;

constexpr int16_t kTemperatureAlarmX10C = 600;
constexpr int16_t kBmsTemperatureAlarmX10C = 550;
constexpr uint16_t kLowSocX10Pct = 100;

}  // namespace lobos::board

