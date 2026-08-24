#include <Arduino.h>
#include <LobosC6BmsProtocol.h>

#include "DalyBms.h"

namespace {

constexpr uint32_t kLinkBaud = 115200;
constexpr int8_t kLinkRxPin = 17;  // reservado para futura respuesta del ESP32
constexpr int8_t kLinkTxPin = 16;  // conectar al RX elegido del ESP32 principal
constexpr uint32_t kSendIntervalMs = 500;
constexpr uint32_t kMetricsIntervalMs = 5000;

HardwareSerial linkSerial(1);
uint32_t lastSendMs = 0;
uint32_t lastMetricsMs = 0;
uint32_t sentFrames = 0;
uint32_t encodeErrors = 0;

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1000);
  linkSerial.begin(kLinkBaud, SERIAL_8N1, kLinkRxPin, kLinkTxPin);
  lobos::daly::begin();
  Serial.println("Lobos Racing - ESP32-C6 dual Daly -> UART");
  Serial.println("C6 TX GPIO16 -> ESP32 principal RX; GND comun obligatorio.");
}

void loop() {
  lobos::daly::loop();
  const uint32_t nowMs = millis();
  if (nowMs - lastSendMs >= kSendIntervalMs) {
    lastSendMs += kSendIntervalMs;
    const lobos::c6::DualBmsFrame snapshot = lobos::daly::snapshot();
    uint8_t output[lobos::c6::kFrameLength];
    const size_t length = lobos::c6::encode(snapshot, output, sizeof(output));
    if (length == lobos::c6::kFrameLength) {
      linkSerial.write(output, length);
      ++sentFrames;
    } else {
      ++encodeErrors;
    }
  }
  if (nowMs - lastMetricsMs >= kMetricsIntervalMs) {
    lastMetricsMs += kMetricsIntervalMs;
    Serial.printf("[C6] uart_tx=%lu encode_errors=%lu heap=%u\n",
                  static_cast<unsigned long>(sentFrames),
                  static_cast<unsigned long>(encodeErrors), ESP.getFreeHeap());
    lobos::daly::printMetrics();
  }
  delay(5);
}

