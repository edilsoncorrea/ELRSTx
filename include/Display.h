#pragma once
#include <Arduino.h>

// Display HAL. UI.cpp decides WHAT is shown and fills these views; each driver
// (DisplaySSD1306.cpp, DisplayST7789.cpp, DisplayNone.cpp) decides HOW: layout,
// fonts, colours and how much fits. Only the driver chosen by DISPLAY_TYPE is built.
namespace Display {

enum class Level : uint8_t { None, Warn, Crit };

struct HomeView {
    const char* title;        // model name (+ " SIM")
    uint16_t txBatteryMv;     // 0 = not measured
    Level txBatteryLevel;
    bool moduleAlive;
    const char* deviceName;   // "" until the module answers the ping
    bool link;
    uint8_t lq;               // uplink LQ %
    uint8_t rssi;             // uplink RSSI, -dBm
    uint16_t rateHz;
    bool synced;
    bool rxBatteryValid;
    uint16_t rxBatteryDv;     // 0.1 V
    Level alarm;
    const char* alarmText;
    const char* flightMode;
    char axisLetter[4];       // R P T Y
    float stick[4];           // 0..1
    bool aux[4];
};

struct Row {
    char label[32];
    char value[24];           // "" = label only
    bool folder;              // opens a sub-list
    bool command;             // action, no value
};

struct ListView {
    const char* title;
    uint8_t count;
    uint8_t cursor;
    bool editing;             // cursor row value is being changed
    bool busy;                // still loading in the background
    void (*row)(uint8_t index, Row& out);
};

struct TelemetryView {
    bool link;
    int8_t snr;
    uint8_t rssi1, rssi2, lq, downRssi, downLq;
    bool battery;
    uint16_t batteryDv, currentDa;
    uint32_t capacityMah;
    bool attitude;
    int16_t pitchDeg, rollDeg, yawDeg;
    bool gps;
    uint8_t satellites;
    int16_t altitudeM;
    const char* flightMode;
};

constexpr uint8_t MESSAGE_LINES = 6;

// Generic text screen: calibration, WiFi, info, ELRS loading and command popups
struct MessageView {
    const char* title;
    const char* line[MESSAGE_LINES];  // nullptr = empty line
    int8_t highlight;                 // line drawn inverted/highlighted, -1 = none
    const char* footer;               // bottom hint, nullptr = none
};

bool begin();  // false when no display is fitted or detected
void splash(const char* version);
void home(const HomeView& v);
void list(const ListView& v);
void channels(const uint16_t* ch, uint8_t count);  // CRSF values 172..1811
void telemetry(const TelemetryView& v);
void message(const MessageView& v);

// Helpers shared by the drivers
inline float channelFraction(uint16_t v) { return constrain((v - 172) / 1639.0f, 0.0f, 1.0f); }
inline int channelUs(uint16_t v) { return ((int)v - 992) * 5 / 8 + 1500; }

}  // namespace Display
