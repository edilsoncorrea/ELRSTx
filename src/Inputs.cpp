#include "Inputs.h"
#include "CRSF.h"
#include "Pins.h"

namespace {
const uint8_t AXIS_PINS[AXIS_COUNT] = {PIN_JOY1_X, PIN_JOY2_Y, PIN_JOY1_Y, PIN_JOY2_X};
}

void Inputs::begin() {
    analogReadResolution(12);
    for (uint8_t pin : AXIS_PINS) analogSetPinAttenuation(pin, ADC_11db);
    analogSetPinAttenuation(PIN_BATTERY, ADC_11db);
}

uint16_t Inputs::readRaw(uint8_t axis) {
    return analogRead(AXIS_PINS[axis]);
}

uint16_t Inputs::simulate(uint8_t axis, uint32_t nowMs) {
    const float t = nowMs / 1000.0f;
    float v = 0.5f;
    switch (axis) {
    case AXIS_ROLL:     v = 0.5f + 0.4f * sinf(t); break;
    case AXIS_PITCH:    v = 0.5f + 0.4f * sinf(t * 0.7f); break;
    case AXIS_THROTTLE: v = 0.3f + 0.4f * (0.5f + 0.5f * sinf(t * 0.3f)); break;
    case AXIS_YAW:      v = 0.5f + 0.4f * sinf(t * 1.3f); break;
    }
    return (uint16_t)(v * 4095.0f);
}

uint16_t Inputs::toCrsf(uint16_t raw, const AxisCal& cal, uint8_t deadband, bool invert) {
    const int32_t r = raw, lo = cal.min, mid = cal.center, hi = cal.max, db = deadband;
    int32_t out;
    if (abs(r - mid) <= db) {
        out = CRSF_CHANNEL_VALUE_MID;
    } else if (r < mid) {
        const int32_t edge = mid - db;
        out = edge > lo ? map(max(r, lo), lo, edge, CRSF_CHANNEL_VALUE_MIN, CRSF_CHANNEL_VALUE_MID)
                        : CRSF_CHANNEL_VALUE_MIN;
    } else {
        const int32_t edge = mid + db;
        out = hi > edge ? map(min(r, hi), edge, hi, CRSF_CHANNEL_VALUE_MID, CRSF_CHANNEL_VALUE_MAX)
                        : CRSF_CHANNEL_VALUE_MAX;
    }
    out = constrain(out, CRSF_CHANNEL_VALUE_MIN, CRSF_CHANNEL_VALUE_MAX);
    if (invert) out = CRSF_CHANNEL_VALUE_MIN + CRSF_CHANNEL_VALUE_MAX - out;
    return out;
}

uint16_t Battery::readMillivolts(uint16_t ratioX1000) {
    return (uint32_t)analogReadMilliVolts(PIN_BATTERY) * ratioX1000 / 1000;
}

uint8_t Battery::percent(uint16_t cellMv) {
    static const uint16_t MV[] = {3300, 3500, 3700, 3800, 3900, 4000, 4100, 4200};
    static const uint8_t PCT[] = {0, 10, 40, 60, 75, 85, 95, 100};
    if (cellMv <= MV[0]) return 0;
    for (uint8_t i = 1; i < sizeof(MV) / sizeof(MV[0]); i++) {
        if (cellMv <= MV[i]) return map(cellMv, MV[i - 1], MV[i], PCT[i - 1], PCT[i]);
    }
    return 100;
}
