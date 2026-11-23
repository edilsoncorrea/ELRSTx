#pragma once
#include <atomic>
#include "CRSF.h"
#include "Config.h"

// Written by the RC task each cycle, read by the UI/command side as a snapshot
struct RcState {
    uint16_t raw[AXIS_COUNT];
    uint16_t channels[CRSF_MAX_CHANNELS];
    uint8_t lastFrame[CRSF_RC_FRAME_SIZE];
    uint32_t frames;
    uint32_t periodUs;
    bool safeStartHold;
    uint16_t txBatteryMv;
    CRSFTelemetry telem;
};

namespace State {
void publish(const RcState& s);
RcState snapshot();

// Take the snapshot before reading millis(), otherwise ages can underflow
bool moduleAlive(const RcState& s, uint32_t nowMs);
bool linkUp(const RcState& s, uint32_t nowMs, uint16_t timeoutMs);
bool syncActive(const RcState& s, uint32_t nowMs);
bool fresh(uint32_t lastMs, uint32_t nowMs, uint32_t maxAgeMs);

// Bitmask of AUX switches from the I2C expander, written by the UI loop
extern std::atomic<uint8_t> auxSwitches;

// Raw ADC of the resistor-ladder menu buttons ("buttons": "adc"), written by the RC task,
// which does all analog reads (the ADC driver must not be used from two tasks at once)
extern std::atomic<uint16_t> buttonAdc;
}
