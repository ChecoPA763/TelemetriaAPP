#include <Arduino.h>
#include <LobosDualMcuProtocol.h>

#include "BoardConfig.h"
#include "Interface.h"

using namespace lobos;

namespace {

uint8_t packetBuffer[kMaxPacketSize];
uint8_t txFrame[kMaxCobsFrameSize];
uint8_t rxFrame[kMaxCobsFrameSize];
size_t rxLength = 0;

uint32_t received = 0;
uint32_t sent = 0;
uint32_t decodeErrors = 0;
uint32_t sequenceGaps = 0;
uint32_t lastRawSequence = 0;
uint32_t processedSequence = 0;
uint32_t lastMetricsMs = 0;
uint64_t totalProcessingUs = 0;
uint32_t maxProcessingUs = 0;

uint32_t accumulatedPulses = 0;
uint32_t accumulatedWindowUs = 0;
uint32_t filteredRpmX10 = 0;

bool bmsValid(const BmsSample& bms) {
  return (bms.flags & (BmsConnected | BmsDataValid)) ==
             (BmsConnected | BmsDataValid) &&
         bms.age_ms <= 1000U;
}

uint32_t calculateRpmX10(const RawSensorFrame& raw) {
  if ((raw.sensor_flags & SensorHallValid) == 0 || raw.hall_window_us == 0 ||
      raw.last_pulse_age_us >= board::kHallStopTimeoutUs) {
    accumulatedPulses = 0;
    accumulatedWindowUs = 0;
    filteredRpmX10 = 0;
    return 0;
  }
  accumulatedPulses += raw.hall_pulse_delta;
  accumulatedWindowUs += raw.hall_window_us;
  if (accumulatedWindowUs < board::kRpmMeasurementWindowUs) {
    return filteredRpmX10;
  }
  const uint64_t numerator =
      static_cast<uint64_t>(accumulatedPulses) * 600000000ULL;
  const uint32_t denominator =
      static_cast<uint32_t>(board::kMagnetsPerRevolution) * accumulatedWindowUs;
  const uint32_t measured = denominator == 0 ? 0 : numerator / denominator;
  accumulatedPulses = 0;
  accumulatedWindowUs = 0;
  filteredRpmX10 = filteredRpmX10 == 0
                       ? measured
                       : (4U * measured + 6U * filteredRpmX10) / 10U;
  return filteredRpmX10;
}

uint16_t speedX10(uint32_t rpmX10) {
  const uint64_t value = static_cast<uint64_t>(rpmX10) *
                         board::kWheelCircumferenceMm * 6ULL / 100000ULL;
  return value > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(value);
}

ProcessedTelemetryFrame process(const RawSensorFrame& raw,
                                uint32_t startedUs) {
  ProcessedTelemetryFrame out{};
  out.processed_seq = ++processedSequence;
  out.source_acquisition_seq = raw.acquisition_seq;
  out.source_uptime_ms = raw.source_uptime_ms;
  const uint32_t rpmX10 = calculateRpmX10(raw);
  out.rpm = rpmX10 / 10U > UINT16_MAX ? UINT16_MAX
                                      : static_cast<uint16_t>(rpmX10 / 10U);
  out.speed_x10_kmh = speedX10(rpmX10);
  out.temperature_x10_C = raw.temperature_x10_C;
  out.bms1 = raw.bms1;
  out.bms2 = raw.bms2;

  if ((raw.sensor_flags & SensorHallValid) != 0) out.state_flags |= StateSpeedValid;
  if ((raw.sensor_flags & SensorTemperatureValid) != 0) out.state_flags |= StateTemperatureValid;
  if ((raw.sensor_flags & SensorChassisFault) != 0) {
    out.state_flags |= StateChassisFault;
    out.alarm_flags |= AlarmChassis;
  }
  const bool valid1 = bmsValid(raw.bms1);
  const bool valid2 = bmsValid(raw.bms2);
  if (valid1) out.state_flags |= StateBms1Valid;
  else out.alarm_flags |= AlarmBms1Missing;
  if (valid2) out.state_flags |= StateBms2Valid;
  else out.alarm_flags |= AlarmBms2Missing;
  if ((raw.sensor_flags & SensorTemperatureValid) != 0 &&
      raw.temperature_x10_C >= board::kTemperatureAlarmX10C) {
    out.alarm_flags |= AlarmTemperature;
  }
  if ((valid1 && raw.bms1.temperature_x10_C >= board::kBmsTemperatureAlarmX10C) ||
      (valid2 && raw.bms2.temperature_x10_C >= board::kBmsTemperatureAlarmX10C)) {
    out.alarm_flags |= AlarmBmsTemperature;
  }
  if ((valid1 && raw.bms1.soc_x10_pct <= board::kLowSocX10Pct) ||
      (valid2 && raw.bms2.soc_x10_pct <= board::kLowSocX10Pct)) {
    out.alarm_flags |= AlarmLowSoc;
  }
  const uint32_t elapsed = micros() - startedUs;
  out.processing_time_us = elapsed > UINT16_MAX ? UINT16_MAX
                                                 : static_cast<uint16_t>(elapsed);
  const uint32_t rawAgeMs = (elapsed + 999U) / 1000U;
  out.raw_age_ms = rawAgeMs > UINT16_MAX
                       ? UINT16_MAX
                       : static_cast<uint16_t>(rawAgeMs);
  return out;
}

void send(const ProcessedTelemetryFrame& processed) {
  const size_t packetLength =
      encodeProcessedPacket(processed, packetBuffer, sizeof(packetBuffer));
  const size_t frameLength =
      makeCobsFrame(packetBuffer, packetLength, txFrame, sizeof(txFrame));
  if (packetLength == 0 || frameLength == 0) {
    ++decodeErrors;
    return;
  }
  Serial1.write(txFrame, frameLength);
  ++sent;
}

void accept(const uint8_t* encoded, size_t encodedLength) {
  const uint32_t started = micros();
  size_t packetLength = 0;
  DecodeStatus status = cobsDecode(encoded, encodedLength, packetBuffer,
                                   sizeof(packetBuffer), packetLength);
  RawSensorFrame raw{};
  if (status == DecodeStatus::Ok) {
    status = decodeRawPacket(packetBuffer, packetLength, raw);
  }
  if (status != DecodeStatus::Ok) {
    ++decodeErrors;
    return;
  }
  if (lastRawSequence != 0 && raw.acquisition_seq > lastRawSequence + 1U) {
    sequenceGaps += raw.acquisition_seq - lastRawSequence - 1U;
  }
  lastRawSequence = raw.acquisition_seq;
  ++received;

  const ProcessedTelemetryFrame processed = process(raw, started);
  const uint32_t elapsed = micros() - started;
  totalProcessingUs += elapsed;
  if (elapsed > maxProcessingUs) maxProcessingUs = elapsed;
  ui::update(processed);
  // Respuesta a 10 Hz; UI y MQTT no requieren 20 Hz.
  if ((received & 1U) == 0U) send(processed);
}

void receiveMain() {
  while (Serial1.available() > 0) {
    const uint8_t byte = static_cast<uint8_t>(Serial1.read());
    if (byte == 0) {
      if (rxLength != 0) accept(rxFrame, rxLength);
      rxLength = 0;
    } else if (rxLength < sizeof(rxFrame)) {
      rxFrame[rxLength++] = byte;
    } else {
      rxLength = 0;
      ++decodeErrors;
    }
  }
}

void metrics() {
  const uint32_t average = received == 0 ? 0 : totalProcessingUs / received;
  Serial.print("\n[RP] RX="); Serial.print(received);
  Serial.print(" TX="); Serial.print(sent);
  Serial.print(" decode="); Serial.print(decodeErrors);
  Serial.print(" gaps="); Serial.println(sequenceGaps);
  Serial.print("[RP] processing_us avg="); Serial.print(average);
  Serial.print(" max="); Serial.println(maxProcessingUs);
}

}  // namespace

void setup() {
  Serial.begin(board::kDebugBaud);
  Serial1.setTX(board::kMainTxPin);
  Serial1.setRX(board::kMainRxPin);
  Serial1.begin(board::kMainBaud);
  delay(300);
  ui::begin();
  lastMetricsMs = millis();
  if (Serial) Serial.println("Lobos Racing - RP2040 interfaz integrada v0");
}

void loop() {
  receiveMain();
  ui::serviceAudio();
  if (millis() - lastMetricsMs >= 5000U) {
    lastMetricsMs = millis();
    metrics();
  }
}

