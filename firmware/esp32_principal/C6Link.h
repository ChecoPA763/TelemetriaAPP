#pragma once

#include <Arduino.h>
#include <LobosC6BmsProtocol.h>
#include <LobosDualMcuProtocol.h>

namespace lobos {

struct C6LinkMetrics {
  uint32_t frames;
  uint32_t decode_errors;
  uint32_t sequence_gaps;
  uint32_t restarts;
  uint32_t last_frame_age_ms;
};

class C6Link {
 public:
  void begin(HardwareSerial& serial, uint32_t baud, int8_t rx_pin);
  void update();
  void samples(BmsSample& bms1, BmsSample& bms2) const;
  bool fresh() const;
  C6LinkMetrics metrics() const;

 private:
  void accept(const c6::DualBmsFrame& frame);
  BmsSample convert(const c6::BmsRecord& source) const;

  HardwareSerial* serial_ = nullptr;
  uint8_t buffer_[c6::kFrameLength]{};
  size_t length_ = 0;
  c6::DualBmsFrame latest_{};
  bool has_frame_ = false;
  uint16_t last_sequence_ = 0;
  uint32_t received_at_ms_ = 0;
  C6LinkMetrics metrics_{};
};

}  // namespace lobos

