#pragma once
#include "Config.h"

namespace Inputs {
void begin();
uint16_t readRaw(uint8_t axis);
uint16_t simulate(uint8_t axis, uint32_t nowMs);
uint16_t toCrsf(uint16_t raw, const AxisCal& cal, uint8_t deadband, bool invert);
}

namespace Battery {
uint16_t readMillivolts(uint16_t ratioX1000);
uint8_t percent(uint16_t cellMv);
}
