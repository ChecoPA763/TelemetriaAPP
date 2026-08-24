#include "DalyBms.h"

#include <Arduino.h>
#include <BLEClient.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <math.h>

namespace lobos::daly {
namespace {

constexpr char kServiceUuid[] = "0000fff0-0000-1000-8000-00805f9b34fb";
constexpr char kNotifyUuid[] = "0000fff1-0000-1000-8000-00805f9b34fb";
constexpr char kWriteUuid[] = "0000fff2-0000-1000-8000-00805f9b34fb";
constexpr uint32_t kResponseTimeoutMs = 1500;
constexpr uint32_t kRescanIntervalMs = 5000;
constexpr uint32_t kFreshTimeoutMs = 3000;
constexpr uint32_t kPauseBetweenBmsMs = 250;
constexpr uint32_t kScanSeconds = 2;
constexpr size_t kRxCapacity = 300;

BLEUUID serviceUuid(kServiceUuid);
BLEUUID notifyUuid(kNotifyUuid);
BLEUUID writeUuid(kWriteUuid);

enum class Query : uint8_t { None, Cells, State };

struct Context {
  uint8_t number;
  const char* name;
  const char* mac;
  BLEAdvertisedDevice* device = nullptr;
  BLEClient* client = nullptr;
  BLERemoteCharacteristic* notify = nullptr;
  BLERemoteCharacteristic* write = nullptr;
  bool found = false;
  bool connected = false;
  uint8_t rx[kRxCapacity]{};
  size_t rxLength = 0;
  Query query = Query::None;
  bool requestState = false;
  uint32_t queryStartedMs = 0;
  uint32_t lastCellsMs = 0;
  uint32_t lastStateMs = 0;
  uint16_t cellsMv[c6::kCellCount]{};
  uint16_t packMv = 0;
  int16_t currentX10A = 0;
  uint16_t socX10Pct = 0;
  int16_t temperatureX10C = 0;
  bool chargeMos = false;
  bool dischargeMos = false;
  bool chargerDetected = false;
  bool loadDetected = false;
  uint32_t crcErrors = 0;
  uint32_t timeouts = 0;
  uint32_t goodFrames = 0;
};

Context bms1{1, "DL-FB4C2E0C68AF2", "b4:c2:e0:c6:8a:f2"};
Context bms2{2, "DL-FB4C2E0C79052", "b4:c2:e0:c7:90:52"};
Context* active = nullptr;
bool queryActive = false;
uint8_t turn = 1;
uint32_t nextQueryMs = 0;
uint32_t lastScanMs = 0;
uint16_t outputSequence = 0;

void releaseQuery() {
  queryActive = false;
  active = nullptr;
}

uint16_t be16(const uint8_t* data) {
  return static_cast<uint16_t>(data[0]) << 8U | data[1];
}

void parseCells(Context& bms, const uint8_t* data, size_t registerCount) {
  if (registerCount < c6::kCellCount) return;
  for (size_t i = 0; i < c6::kCellCount; ++i) {
    bms.cellsMv[i] = be16(data + i * 2U);
  }
  bms.lastCellsMs = millis();
}

void parseState(Context& bms, const uint8_t* data, size_t registerCount) {
  if (registerCount < 16U) return;
  uint16_t r[18]{};
  const size_t count = registerCount > 18U ? 18U : registerCount;
  for (size_t i = 0; i < count; ++i) r[i] = be16(data + i * 2U);
  bms.packMv = static_cast<uint16_t>(r[0] * 100U);
  bms.currentX10A = static_cast<int16_t>(static_cast<int32_t>(r[1]) - 30000);
  bms.socX10Pct = r[2];
  bms.temperatureX10C = static_cast<int16_t>(
      (static_cast<int32_t>(r[5]) + static_cast<int32_t>(r[6]) - 80) * 5);
  bms.chargerDetected = r[11] != 0;
  bms.loadDetected = r[12] != 0;
  bms.chargeMos = r[13] != 0;
  bms.dischargeMos = r[14] != 0;
  bms.lastStateMs = millis();
}

void processFrame(Context& bms, const uint8_t* frame, size_t length) {
  if (length < 5U) return;
  const uint16_t receivedCrc = static_cast<uint16_t>(frame[length - 2U]) |
                               (static_cast<uint16_t>(frame[length - 1U]) << 8U);
  if (receivedCrc != c6::crc16Modbus(frame, length - 2U)) {
    ++bms.crcErrors;
    bms.query = Query::None;
    bms.requestState = false;
    releaseQuery();
    return;
  }
  if (frame[0] != 0xD2 || frame[1] != 0x03) {
    bms.query = Query::None;
    releaseQuery();
    return;
  }
  const size_t registers = frame[2] / 2U;
  const Query completed = bms.query;
  bms.query = Query::None;
  if (completed == Query::Cells) {
    parseCells(bms, frame + 3U, registers);
    bms.requestState = true;
  } else if (completed == Query::State) {
    parseState(bms, frame + 3U, registers);
    bms.requestState = false;
    ++bms.goodFrames;
    turn = bms.number == 1 ? 2 : 1;
    nextQueryMs = millis() + kPauseBetweenBmsMs;
  }
  releaseQuery();
}

void processBytes(Context& bms, const uint8_t* data, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    if (bms.rxLength >= kRxCapacity) {
      bms.rxLength = 0;
      bms.query = Query::None;
      releaseQuery();
      return;
    }
    bms.rx[bms.rxLength++] = data[i];
  }
  while (bms.rxLength >= 3U) {
    while (bms.rxLength > 0 && bms.rx[0] != 0xD2) {
      memmove(bms.rx, bms.rx + 1U, --bms.rxLength);
    }
    if (bms.rxLength < 3U) return;
    const size_t expected = bms.rx[1] == 0x03
                                ? 3U + bms.rx[2] + 2U
                                : ((bms.rx[1] & 0x80U) != 0U ? 5U : 0U);
    if (expected == 0 || expected > kRxCapacity) {
      memmove(bms.rx, bms.rx + 1U, --bms.rxLength);
      continue;
    }
    if (bms.rxLength < expected) return;
    processFrame(bms, bms.rx, expected);
    const size_t remaining = bms.rxLength - expected;
    if (remaining > 0) memmove(bms.rx, bms.rx + expected, remaining);
    bms.rxLength = remaining;
  }
}

void notifyCallback(BLERemoteCharacteristic* characteristic, uint8_t* data,
                    size_t length, bool) {
  if (characteristic == bms1.notify) processBytes(bms1, data, length);
  if (characteristic == bms2.notify) processBytes(bms2, data, length);
}

class ClientCallbacks : public BLEClientCallbacks {
 public:
  explicit ClientCallbacks(Context* context) : context_(context) {}
  void onConnect(BLEClient*) override {}
  void onDisconnect(BLEClient*) override {
    context_->connected = false;
    context_->found = false;
    context_->notify = nullptr;
    context_->write = nullptr;
    context_->query = Query::None;
    context_->requestState = false;
    context_->rxLength = 0;
    if (active == context_) releaseQuery();
  }

 private:
  Context* context_;
};

class AdvertisedCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertised) override {
    const String address = advertised.getAddress().toString();
    Context* target = nullptr;
    if (address.equalsIgnoreCase(bms1.mac)) target = &bms1;
    if (address.equalsIgnoreCase(bms2.mac)) target = &bms2;
    if (target == nullptr || target->found || target->connected) return;
    target->found = true;
    delete target->device;
    target->device = new BLEAdvertisedDevice(advertised);
    if ((bms1.found || bms1.connected) && (bms2.found || bms2.connected)) {
      BLEDevice::getScan()->stop();
    }
  }
};

bool connect(Context& bms) {
  if (bms.device == nullptr) return false;
  if (bms.client == nullptr) {
    bms.client = BLEDevice::createClient();
    bms.client->setClientCallbacks(new ClientCallbacks(&bms));
  }
  if (!bms.client->connect(bms.device)) {
    bms.found = false;
    return false;
  }
  bms.client->setMTU(517);
  BLERemoteService* service = bms.client->getService(serviceUuid);
  if (service == nullptr) {
    bms.client->disconnect();
    return false;
  }
  bms.notify = service->getCharacteristic(notifyUuid);
  bms.write = service->getCharacteristic(writeUuid);
  if (bms.notify == nullptr || bms.write == nullptr || !bms.notify->canNotify()) {
    bms.client->disconnect();
    return false;
  }
  bms.notify->registerForNotify(notifyCallback);
  bms.connected = true;
  return true;
}

bool writeCommand(Context& bms, uint8_t* command, size_t length) {
  if (!bms.connected || bms.write == nullptr) return false;
  if (bms.write->canWriteNoResponse()) return bms.write->writeValue(command, length, false);
  if (bms.write->canWrite()) return bms.write->writeValue(command, length, true);
  return false;
}

void startQuery(Context& bms, Query query, uint8_t* command, size_t length) {
  bms.rxLength = 0;
  bms.query = query;
  bms.queryStartedMs = millis();
  active = &bms;
  queryActive = true;
  if (!writeCommand(bms, command, length)) {
    bms.query = Query::None;
    releaseQuery();
  }
}

void requestCells(Context& bms) {
  static uint8_t command[]{0xD2, 0x03, 0x00, 0x00, 0x00, 0x10, 0x57, 0xA5};
  startQuery(bms, Query::Cells, command, sizeof(command));
}

void requestState(Context& bms) {
  static uint8_t command[]{0xD2, 0x03, 0x00, 0x28, 0x00, 0x12, 0x56, 0x6C};
  startQuery(bms, Query::State, command, sizeof(command));
}

Context* chooseNext() {
  if (turn == 1 && bms1.connected) return &bms1;
  if (turn == 2 && bms2.connected) return &bms2;
  if (bms1.connected) return &bms1;
  if (bms2.connected) return &bms2;
  return nullptr;
}

void scanMissing() {
  if (bms1.connected && bms2.connected) return;
  BLEDevice::getScan()->start(kScanSeconds, false);
  lastScanMs = millis();
}

c6::BmsRecord makeRecord(const Context& bms, uint32_t nowMs) {
  c6::BmsRecord out{};
  const uint32_t stateAgeMs =
      bms.lastStateMs == 0 ? UINT32_MAX : nowMs - bms.lastStateMs;
  out.age_ms = stateAgeMs > UINT16_MAX
                   ? UINT16_MAX
                   : static_cast<uint16_t>(stateAgeMs);
  if (!bms.connected) return out;
  out.flags |= c6::Connected;
  if (bms.chargeMos) out.flags |= c6::ChargeMos;
  if (bms.dischargeMos) out.flags |= c6::DischargeMos;
  if (bms.chargerDetected) out.flags |= c6::ChargerDetected;
  if (bms.loadDetected) out.flags |= c6::LoadDetected;
  if (bms.lastCellsMs != 0 && nowMs - bms.lastCellsMs <= kFreshTimeoutMs) {
    out.flags |= c6::CellsValid;
  }
  if (stateAgeMs <= kFreshTimeoutMs) {
    out.flags |= c6::StateValid;
  }
  out.pack_mV = bms.packMv;
  out.current_x10_A = bms.currentX10A;
  out.soc_x10_pct = bms.socX10Pct;
  out.temperature_x10_C = bms.temperatureX10C;
  for (size_t i = 0; i < c6::kCellCount; ++i) out.cells_mV[i] = bms.cellsMv[i];
  return out;
}

}  // namespace

void begin() {
  BLEDevice::init("LOBOS-C6-DUAL-DALY");
  BLEScan* scan = BLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new AdvertisedCallbacks());
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(99);
  scanMissing();
}

void loop() {
  const uint32_t nowMs = millis();
  if (bms1.found && !bms1.connected) connect(bms1);
  if (bms2.found && !bms2.connected) connect(bms2);

  if (queryActive && active != nullptr &&
      nowMs - active->queryStartedMs > kResponseTimeoutMs) {
    ++active->timeouts;
    active->query = Query::None;
    active->requestState = false;
    active->rxLength = 0;
    turn = active->number == 1 ? 2 : 1;
    releaseQuery();
    nextQueryMs = nowMs + kPauseBetweenBmsMs;
  }
  if (!queryActive) {
    if (bms1.requestState && bms1.connected) {
      requestState(bms1);
    } else if (bms2.requestState && bms2.connected) {
      requestState(bms2);
    } else if (static_cast<int32_t>(nowMs - nextQueryMs) >= 0) {
      Context* next = chooseNext();
      if (next != nullptr) requestCells(*next);
      else nextQueryMs = nowMs + 1000U;
    }
  }
  if (!queryActive && (!bms1.connected || !bms2.connected) &&
      nowMs - lastScanMs >= kRescanIntervalMs) {
    scanMissing();
  }
}

c6::DualBmsFrame snapshot() {
  const uint32_t nowMs = millis();
  c6::DualBmsFrame frame{};
  frame.sequence = ++outputSequence;
  frame.source_uptime_ms = nowMs;
  frame.bms1 = makeRecord(bms1, nowMs);
  frame.bms2 = makeRecord(bms2, nowMs);
  return frame;
}

void printMetrics() {
  Serial.printf("[C6] BMS1 connected=%u good=%lu crc=%lu timeouts=%lu\n",
                bms1.connected, static_cast<unsigned long>(bms1.goodFrames),
                static_cast<unsigned long>(bms1.crcErrors),
                static_cast<unsigned long>(bms1.timeouts));
  Serial.printf("[C6] BMS2 connected=%u good=%lu crc=%lu timeouts=%lu\n",
                bms2.connected, static_cast<unsigned long>(bms2.goodFrames),
                static_cast<unsigned long>(bms2.crcErrors),
                static_cast<unsigned long>(bms2.timeouts));
}

}  // namespace lobos::daly

