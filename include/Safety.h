#pragma once
#include "Config.h"

// Handset-side safety. Real failsafe (what the aircraft does on link loss) is configured
// on the receiver / flight controller: the radio cannot reach the aircraft once the link is gone.
class Safety {
public:
    // Safe start: throttle and arm (AUX1) are forced low until the pilot has
    // shown throttle low + arm switch off at least once after boot.
    void apply(uint16_t channels[], uint8_t throttleChannel, const ModelSettings& model);
    bool holding() const { return _holding; }

private:
    bool _released = false;
    bool _holding = false;
};

namespace Calibration {
void captureCenter(const uint16_t raw[AXIS_COUNT]);
void startRange(const uint16_t raw[AXIS_COUNT]);
void update(const uint16_t raw[AXIS_COUNT]);
bool finish(String& error);
void cancel();
bool active();
AxisCal range(uint8_t axis);
}
