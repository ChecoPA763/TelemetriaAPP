#include <Arduino.h>
#include <LobosDualMcuProtocol.h>
#include <Wire.h>

#include "BoardConfig.h"
#include "C6Link.h"
#include "HallSensor.h"
#include "MqttBridge.h"
#include "TemperatureArray.h"

using namespace lobos;

namespace {

constexpr uint32_t kDataStaleMs = 3000;

uint8_t packetBuffer[kMaxPacketSize];
uint8_t txFrame[kMaxCobsFrameSize];
uint8_t rxFrame[kMaxCobsFrameSize];
size_t rxLength = 0;

TemperatureArray temperatures;
C6Link c6Link;
MqttBridge mqtt;
ProcessedTelemetryFrame latestProcessed{};
bool hasProcessed = false;

uint32_t acquisitionSequence = 0;
uint32_t lastRawUs = 0;
uint32_t lastResponseUs = 0;
uint32_t lastMqttMs = 0;
uint32_t lastMetricsMs = 0;
uint32_t sent = 0;
uint32_t received = 0;
uint32_t decodeErrors = 0;
uint32_t responseTimeouts = 0;
bool rpOffline = false;

RawSensorFrame acquire(uint32_t nowUs, uint32_t windowUs) {
  RawSensorFrame raw{};
  raw.acquisition_seq = ++acquisitionSequence;
  raw.source_uptime_ms = millis();

  const hall::Sample hallSample = hall::sample(nowUs, windowUs);
  raw.hall_pulse_delta = hallSample.pulses;
  raw.hall_window_us = hallSample.window_us;
  raw.last_pulse_age_us = hallSample.last_pulse_age_us;
  raw.sensor_flags = SensorHallValid;

  int16_t hottest;
  if (temperatures.hottest(hottest)) {
    raw.temperature_x10_C = hottest;
    raw.sensor_flags |= SensorTemperatureValid;
  }

  if (digitalRead(board::kPc817Pin) == board::kPc817FaultLevel) {
    raw.sensor_flags |= SensorChassisFault;
  }

  c6Link.samples(raw.bms1, raw.bms2);
  if ((raw.bms1.flags & BmsConnected) != 0) {
    raw.sensor_flags |= SensorBms1Available;
  }
  if ((raw.bms2.flags & BmsConnected) != 0) {
    raw.sensor_flags |= SensorBms2Available;
  }
  return raw;
}

void sendRaw(uint32_t nowUs, uint32_t windowUs) {
  const RawSensorFrame raw = acquire(nowUs, windowUs);
  const size_t packetLength =
      encodeRawPacket(raw, packetBuffer, sizeof(packetBuffer));
  const size_t frameLength =
      makeCobsFrame(packetBuffer, packetLength, txFrame, sizeof(txFrame));
  if (packetLength == 0 || frameLength == 0) {
    ++decodeErrors;
    return;
  }
  Serial1.write(txFrame, frameLength);
  ++sent;
}

void acceptRpFrame(const uint8_t* encoded, size_t encodedLength) {
  size_t packetLength = 0;
  DecodeStatus status = cobsDecode(encoded, encodedLength, packetBuffer,
                                   sizeof(packetBuffer), packetLength);
  if (status != DecodeStatus::Ok) {
    ++decodeErrors;
    return;
  }
  ProcessedTelemetryFrame decoded{};
  status = decodeProcessedPacket(packetBuffer, packetLength, decoded);
  if (status != DecodeStatus::Ok) {
    ++decodeErrors;
    return;
  }
  latestProcessed = decoded;
  hasProcessed = true;
  lastResponseUs = micros();
  rpOffline = false;
  ++received;
}

void receiveRp() {
  while (Serial1.available() > 0) {
    const uint8_t byte = static_cast<uint8_t>(Serial1.read());
    if (byte == 0) {
      if (rxLength != 0) acceptRpFrame(rxFrame, rxLength);
      rxLength = 0;
    } else if (rxLength < sizeof(rxFrame)) {
      rxFrame[rxLength++] = byte;
    } else {
      rxLength = 0;
      ++decodeErrors;
    }
  }
}

void printMetrics() {
  const C6LinkMetrics c6 = c6Link.metrics();
  const MqttMetrics mq = mqtt.metrics();
  Serial.printf("\n[MAIN] RP tx=%lu rx=%lu decode=%lu timeout=%lu offline=%u\n",
                static_cast<unsigned long>(sent),
                static_cast<unsigned long>(received),
                static_cast<unsigned long>(decodeErrors),
                static_cast<unsigned long>(responseTimeouts), rpOffline);
  Serial.printf("[MAIN] C6 frames=%lu decode=%lu gaps=%lu restarts=%lu age_ms=%lu\n",
                static_cast<unsigned long>(c6.frames),
                static_cast<unsigned long>(c6.decode_errors),
                static_cast<unsigned long>(c6.sequence_gaps),
                static_cast<unsigned long>(c6.restarts),
                static_cast<unsigned long>(c6.last_frame_age_ms));
  for (uint8_t channel = 0; channel < board::kTemperatureChannelCount;
       ++channel) {
    const auto reading = temperatures.reading(channel);
    Serial.printf("[MAIN] TEMP ch=%u valid=%u value=%.1fC age_ms=%lu\n",
                  channel, reading.valid, reading.object_x10_c / 10.0,
                  static_cast<unsigned long>(reading.age_ms));
  }
  Serial.printf("[MAIN] MQTT state=%s rssi=%ld queued=%lu published=%lu fail=%lu overwrite=%lu\n",
                MqttBridge::stateName(mq.state), static_cast<long>(mq.wifi_rssi_dbm),
                static_cast<unsigned long>(mq.queued),
                static_cast<unsigned long>(mq.published),
                static_cast<unsigned long>(mq.publish_failures),
                static_cast<unsigned long>(mq.queue_overwrites));
}

}  // namespace

void setup() {
  Serial.begin(board::kDebugBaud);
  delay(500);
  Serial.println("Lobos Racing - ESP32 principal integrado v0");

  pinMode(board::kPc817Pin, INPUT_PULLUP);
  hall::begin(board::kHallPin);
  Wire.begin(board::kI2cSdaPin, board::kI2cSclPin);
  Wire.setClock(400000);
  temperatures.begin(Wire);

  Serial1.begin(board::kRpBaud, SERIAL_8N1, board::kRpRxPin,
                board::kRpTxPin);
  c6Link.begin(Serial2, board::kC6Baud, board::kC6RxPin);

  if (!mqtt.begin()) Serial.println("[MQTT] ERROR creando tarea");
  lastRawUs = micros();
  lastResponseUs = lastRawUs;
  lastMqttMs = lastMetricsMs = millis();
}

void loop() {
  temperatures.update();
  c6Link.update();
  receiveRp();

  const uint32_t nowUs = micros();
  if (nowUs - lastRawUs >= board::kRawPeriodUs) {
    const uint32_t windowUs = nowUs - lastRawUs;
    lastRawUs = nowUs;
    sendRaw(nowUs, windowUs);
  }
  if (!rpOffline && nowUs - lastResponseUs >= board::kRpTimeoutUs) {
    rpOffline = true;
    ++responseTimeouts;
  }

  const uint32_t nowMs = millis();
  if (hasProcessed && nowMs - lastMqttMs >= board::kMqttPeriodMs) {
    lastMqttMs = nowMs;
    ProcessedTelemetryFrame outgoing = latestProcessed;
    const uint32_t heldAgeMs = (nowUs - lastResponseUs) / 1000U;
    const uint64_t totalAgeMs =
        static_cast<uint64_t>(outgoing.raw_age_ms) + heldAgeMs;
    outgoing.raw_age_ms = totalAgeMs > UINT16_MAX
                              ? UINT16_MAX
                              : static_cast<uint16_t>(totalAgeMs);
    mqtt.enqueue(outgoing, totalAgeMs < kDataStaleMs, !rpOffline);
  }
  if (nowMs - lastMetricsMs >= board::kMetricsPeriodMs) {
    lastMetricsMs = nowMs;
    printMetrics();
  }
}

