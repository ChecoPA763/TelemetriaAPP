#pragma once

#include <LobosC6BmsProtocol.h>

namespace lobos::daly {

void begin();
void loop();
c6::DualBmsFrame snapshot();
void printMetrics();

}  // namespace lobos::daly

