#include "Safety.h"
#include "CRSF.h"

void Safety::apply(uint16_t channels[], uint8_t throttleChannel, const ModelSettings& model) {
    if (!model.safeStart) {
        _holding = false;
        return;
    }
    if (!_released) {
        const bool throttleLow = channels[throttleChannel] < CRSF_CHANNEL_VALUE_MIN + 82;  // < ~5%
        const bool armOff = channels[AUX_FIRST_CHANNEL] < CRSF_CHANNEL_VALUE_MID;
        _released = throttleLow && armOff;
    }
    _holding = !_released;
    if (_holding) {
        channels[throttleChannel] = CRSF_CHANNEL_VALUE_MIN;
        channels[AUX_FIRST_CHANNEL] = CRSF_CHANNEL_VALUE_MIN;
    }
}

namespace {
bool ranging = false;
AxisCal working[AXIS_COUNT];
constexpr uint16_t MIN_TRAVEL = 300;
}

void Calibration::captureCenter(const uint16_t raw[AXIS_COUNT]) {
    Settings s = Config::get();
    for (uint8_t a = 0; a < AXIS_COUNT; a++) s.hw.cal[a].center = raw[a];
    Config::set(s);
}

void Calibration::startRange(const uint16_t raw[AXIS_COUNT]) {
    const Settings s = Config::get();
    for (uint8_t a = 0; a < AXIS_COUNT; a++) working[a] = {raw[a], s.hw.cal[a].center, raw[a]};
    ranging = true;
}

void Calibration::update(const uint16_t raw[AXIS_COUNT]) {
    if (!ranging) return;
    for (uint8_t a = 0; a < AXIS_COUNT; a++) {
        working[a].min = min(working[a].min, raw[a]);
        working[a].max = max(working[a].max, raw[a]);
    }
}

bool Calibration::finish(String& error) {
    if (!ranging) {
        error = "run 'cal start' first";
        return false;
    }
    ranging = false;
    Settings s = Config::get();
    for (uint8_t a = 0; a < AXIS_COUNT; a++) {
        const AxisCal& w = working[a];
        if (w.center < w.min + MIN_TRAVEL || w.max < w.center + MIN_TRAVEL) {
            error = String("too little travel on ") + Config::axisName(a);
            return false;
        }
        s.hw.cal[a].min = w.min;
        s.hw.cal[a].max = w.max;
    }
    Config::set(s);
    if (!Config::save()) {
        error = "flash write failed";
        return false;
    }
    return true;
}

void Calibration::cancel() { ranging = false; }

bool Calibration::active() { return ranging; }

AxisCal Calibration::range(uint8_t axis) { return working[axis]; }
