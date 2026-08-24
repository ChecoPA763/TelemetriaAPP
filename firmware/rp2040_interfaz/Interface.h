#pragma once

#include <Arduino.h>
#include <LobosDualMcuProtocol.h>

namespace lobos::ui {

void begin();
void update(const ProcessedTelemetryFrame& frame);
void serviceAudio();

}  // namespace lobos::ui

