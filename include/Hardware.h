#pragma once
#include <Arduino.h>

// Pin and peripheral layout, read at boot like ExpressLRS's hardware.json.
//
// Defaults come from hardware/<board>.json, built into the firmware. A saved
// override (serial 'hw set', web /hardware) lives in NVS and wins over it.
// Changes take effect after a reboot. Holding OK (default pin) while booting
// ignores the override; 'hw reset' deletes it.

enum class DisplayType : uint8_t { None, Ssd1306, St7789 };
enum class InputMode : uint8_t { Gpio, Pcf8574 };

struct HardwareConfig {
    int8_t joy[4];             // by Axis: roll, pitch, throttle, yaw (ADC1 pins)
    int8_t battery;            // -1 = not fitted

    int8_t crsfRx;             // same pin as crsfTx = one wire, half-duplex, inverted (JR bay / S.Port)
    int8_t crsfTx;
    uint32_t crsfBaud;

    InputMode inputs;
    int8_t button[4];          // up, down, ok, back (GPIO mode)
    int8_t aux[4];             // AUX1..AUX4 (GPIO mode)
    uint8_t expanderAddr;      // PCF8574 mode

    int8_t i2cSda;
    int8_t i2cScl;

    DisplayType display;
    uint8_t oledAddr;
    int8_t tftSclk, tftMosi, tftDc, tftRst;
    uint8_t tftRotation;

    int8_t led;
    bool ledRgb;
    bool ledActiveLow;
    int8_t buzzer;

    bool halfDuplex() const { return crsfRx == crsfTx; }
    bool usesI2c() const { return display == DisplayType::Ssd1306 || inputs == InputMode::Pcf8574; }
};

namespace Hardware {
void begin();
const HardwareConfig& get();
bool usingOverride();     // a saved hardware.json is active
bool overrideIgnored();   // OK was held at boot

String json();            // active configuration
String defaultJson();     // built into the firmware
String savedJson();       // pending: what applies after the next reboot

// Validate and store (applies after reboot). err explains a rejection.
bool save(const String& json, String& err);
bool set(const String& key, const String& value, String& err);
void reset();             // delete the override
const char* displayName(DisplayType t);
}
