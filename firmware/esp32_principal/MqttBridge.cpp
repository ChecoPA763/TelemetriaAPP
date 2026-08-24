#include "MqttBridge.h"

#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <time.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#warning "Usando secrets.example.h: MQTT no conectara hasta crear secrets.h"
#endif

namespace lobos {
namespace {

constexpr uint32_t kWifiRetryMs = 10000;
constexpr uint32_t kBrokerRetryMs = 5000;
constexpr time_t kMinimumValidEpoch = 1700000000;
constexpr size_t kPayloadBytes = 704;

struct PublishItem {
  uint16_t length;
  char payload[kPayloadBytes];
};

WiFiClientSecure tls;
PubSubClient mqtt(tls);
QueueHandle_t queue = nullptr;
portMUX_TYPE metricsMux = portMUX_INITIALIZER_UNLOCKED;
MqttMetrics current{MqttState::Starting, 0, 0, 0, 0, 0};
uint32_t publishSequence = 0;

void setState(MqttState state) {
  portENTER_CRITICAL(&metricsMux);
  current.state = state;
  portEXIT_CRITICAL(&metricsMux);
}

void increment(uint32_t& value) {
  portENTER_CRITICAL(&metricsMux);
  ++value;
  portEXIT_CRITICAL(&metricsMux);
}

bool flag(const BmsSample& bms, BmsFlags value) {
  return (bms.flags & static_cast<uint8_t>(value)) != 0;
}

size_t buildJson(const ProcessedTelemetryFrame& f, bool dataValid,
                 bool rp2040Online, char* output, size_t capacity) {
  const int n = snprintf(
      output, capacity,
      "{\"seq\":%lu,\"uptime_ms\":%lu,\"speed_kmh\":%.1f,\"rpm\":%u,"
      "\"temp_c\":%.1f,\"chassis_fault\":%s,\"state_flags\":%u,"
      "\"alarm_flags\":%u,\"source_processed_seq\":%lu,"
      "\"source_acquisition_seq\":%lu,\"processing_time_us\":%u,"
      "\"raw_age_ms\":%u,\"data_age_ms\":%u,\"data_valid\":%s,"
      "\"rp2040_online\":%s,"
      "\"bms1\":{\"voltage_v\":%.3f,\"current_a\":%.1f,\"temp_c\":%.1f,"
      "\"soc_pct\":%.1f,\"age_ms\":%u,\"connected\":%s,\"valid\":%s,\"flags\":%u},"
      "\"bms2\":{\"voltage_v\":%.3f,\"current_a\":%.1f,\"temp_c\":%.1f,"
      "\"soc_pct\":%.1f,\"age_ms\":%u,\"connected\":%s,\"valid\":%s,\"flags\":%u}}",
      static_cast<unsigned long>(++publishSequence),
      static_cast<unsigned long>(f.source_uptime_ms), f.speed_x10_kmh / 10.0,
      f.rpm, f.temperature_x10_C / 10.0,
      (f.state_flags & StateChassisFault) != 0 ? "true" : "false",
      f.state_flags, f.alarm_flags,
      static_cast<unsigned long>(f.processed_seq),
      static_cast<unsigned long>(f.source_acquisition_seq),
      f.processing_time_us, f.raw_age_ms, f.raw_age_ms,
      dataValid ? "true" : "false", rp2040Online ? "true" : "false",
      f.bms1.voltage_mV / 1000.0, f.bms1.current_x10_A / 10.0,
      f.bms1.temperature_x10_C / 10.0, f.bms1.soc_x10_pct / 10.0,
      f.bms1.age_ms, flag(f.bms1, BmsConnected) ? "true" : "false",
      flag(f.bms1, BmsDataValid) ? "true" : "false", f.bms1.flags,
      f.bms2.voltage_mV / 1000.0, f.bms2.current_x10_A / 10.0,
      f.bms2.temperature_x10_C / 10.0, f.bms2.soc_x10_pct / 10.0,
      f.bms2.age_ms, flag(f.bms2, BmsConnected) ? "true" : "false",
      flag(f.bms2, BmsDataValid) ? "true" : "false", f.bms2.flags);
  return n > 0 && static_cast<size_t>(n) < capacity ? static_cast<size_t>(n) : 0;
}

void startWifi() {
  WiFi.disconnect(false, false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(LOBOS_WIFI_SSID, LOBOS_WIFI_PASSWORD);
  setState(MqttState::WifiDisconnected);
}

bool connectBroker() {
  char id[40];
  snprintf(id, sizeof(id), "lobos-kart01-%04X",
           static_cast<uint16_t>(ESP.getEfuseMac()));
  return mqtt.connect(id, LOBOS_MQTT_USERNAME, LOBOS_MQTT_PASSWORD);
}

}  // namespace

bool MqttBridge::begin() {
  queue = xQueueCreate(1, sizeof(PublishItem));
  if (queue == nullptr) return false;
  if (LOBOS_MQTT_ALLOW_INSECURE) {
    tls.setInsecure();
    Serial.println("[MQTT] ADVERTENCIA: TLS sin validacion de CA");
  } else {
    tls.setCACert(LOBOS_MQTT_CA_CERT);
  }
  tls.setTimeout(2000);
  mqtt.setServer(LOBOS_MQTT_HOST, LOBOS_MQTT_PORT);
  mqtt.setBufferSize(768);
  mqtt.setKeepAlive(15);
  mqtt.setSocketTimeout(2);
  startWifi();
  return xTaskCreatePinnedToCore(taskEntry, "lobos-mqtt", 6144, this, 1,
                                 nullptr, 0) == pdPASS;
}

bool MqttBridge::enqueue(const ProcessedTelemetryFrame& frame, bool dataValid,
                         bool rp2040Online) {
  if (queue == nullptr) return false;
  PublishItem item{};
  item.length = static_cast<uint16_t>(
      buildJson(frame, dataValid, rp2040Online, item.payload,
                sizeof(item.payload)));
  if (item.length == 0) return false;
  if (uxQueueMessagesWaiting(queue) != 0) increment(current.queue_overwrites);
  if (xQueueOverwrite(queue, &item) != pdPASS) return false;
  increment(current.queued);
  return true;
}

MqttMetrics MqttBridge::metrics() const {
  portENTER_CRITICAL(&metricsMux);
  MqttMetrics copy = current;
  portEXIT_CRITICAL(&metricsMux);
  copy.wifi_rssi_dbm = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  return copy;
}

const char* MqttBridge::stateName(MqttState state) {
  switch (state) {
    case MqttState::Starting: return "starting";
    case MqttState::WifiDisconnected: return "wifi_down";
    case MqttState::SynchronizingTime: return "time_sync";
    case MqttState::BrokerDisconnected: return "broker_down";
    case MqttState::Connected: return "connected";
  }
  return "unknown";
}

void MqttBridge::taskEntry(void* context) {
  static_cast<MqttBridge*>(context)->taskLoop();
}

void MqttBridge::taskLoop() {
  uint32_t wifiAttempt = millis();
  uint32_t brokerAttempt = 0;
  bool ntpStarted = false;
  PublishItem item{};
  for (;;) {
    const uint32_t now = millis();
    if (WiFi.status() != WL_CONNECTED) {
      setState(MqttState::WifiDisconnected);
      if (now - wifiAttempt >= kWifiRetryMs) {
        wifiAttempt = now;
        startWifi();
      }
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    if (!LOBOS_MQTT_ALLOW_INSECURE && time(nullptr) < kMinimumValidEpoch) {
      setState(MqttState::SynchronizingTime);
      if (!ntpStarted) {
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
        ntpStarted = true;
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    if (!mqtt.connected()) {
      setState(MqttState::BrokerDisconnected);
      if (now - brokerAttempt >= kBrokerRetryMs) {
        brokerAttempt = now;
        if (connectBroker()) setState(MqttState::Connected);
      }
      vTaskDelay(pdMS_TO_TICKS(25));
      continue;
    }
    setState(MqttState::Connected);
    mqtt.loop();
    if (xQueueReceive(queue, &item, 0) == pdPASS) {
      if (mqtt.publish(LOBOS_MQTT_TOPIC,
                       reinterpret_cast<const uint8_t*>(item.payload),
                       item.length, false)) {
        increment(current.published);
      } else {
        increment(current.publish_failures);
        xQueueOverwrite(queue, &item);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

}  // namespace lobos

