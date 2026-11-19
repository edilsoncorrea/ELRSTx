#pragma once
#include <Arduino.h>

enum Axis : uint8_t { AXIS_ROLL, AXIS_PITCH, AXIS_THROTTLE, AXIS_YAW, AXIS_COUNT };
constexpr uint8_t AUX_COUNT = 4;
constexpr uint8_t AUX_FIRST_CHANNEL = 4;
constexpr uint8_t PROFILE_COUNT = 5;

struct AxisCal {
    uint16_t min;
    uint16_t center;
    uint16_t max;
};

// Shared by all profiles: belongs to the physical radio
struct HwSettings {
    AxisCal cal[AXIS_COUNT];
    uint16_t battRatioX1000;   // divider (R1+R2)/R2 * 1000
    uint8_t battCells;
    uint16_t battWarnMv;       // per cell
    uint16_t battCritMv;       // per cell
    char wifiPass[17];
};

// One per model profile
struct ModelSettings {
    char name[13];
    uint8_t deadband;          // ADC counts around center
    uint8_t channel[AXIS_COUNT];
    bool invert[AXIS_COUNT];
    bool safeStart;
    bool linkAlarm;
    uint16_t linkTimeoutMs;
    bool simulation;
};

struct Settings {
    HwSettings hw;
    ModelSettings model;
    uint8_t profile;
};

namespace Config {
void begin();
Settings get();
void set(const Settings& s);
bool save();
bool selectProfile(uint8_t index);
String profileName(uint8_t index);
void resetDefaults();
const char* axisName(uint8_t axis);
int axisFromName(const String& name);
}
