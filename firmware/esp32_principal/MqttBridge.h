#pragma once

#include <Arduino.h>
#include <LobosDualMcuProtocol.h>

namespace lobos {

enum class MqttState : uint8_t {
  Starting,
  WifiDisconnected,
  SynchronizingTime,
  BrokerDisconnected,
  Connected,
};

struct MqttMetrics {
  MqttState state;
  int32_t wifi_rssi_dbm;
  uint32_t queued;
  uint32_t published;
  uint32_t publish_failures;
  uint32_t queue_overwrites;
};

class MqttBridge {
 public:
  bool begin();
  bool enqueue(const ProcessedTelemetryFrame& frame, bool data_valid,
               bool rp2040_online);
  MqttMetrics metrics() const;
  static const char* stateName(MqttState state);

 private:
  static void taskEntry(void* context);
  void taskLoop();
};

}  // namespace lobos

