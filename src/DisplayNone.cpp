// No display fitted ("display": "none"): status only through the LED, buzzer and serial.
#include "Display.h"

namespace Display {
namespace {

class NoDisplay : public Driver {
public:
    bool begin(const HardwareConfig&) override { return false; }
    void splash(const char*) override {}
    void home(const HomeView&) override {}
    void list(const ListView&) override {}
    void channels(const uint16_t*, uint8_t) override {}
    void telemetry(const TelemetryView&) override {}
    void message(const MessageView&) override {}
};

}  // namespace

Driver* createNone() {
    static NoDisplay d;
    return &d;
}

Driver* create(DisplayType type) {
    switch (type) {
    case DisplayType::Ssd1306: return createSsd1306();
    case DisplayType::St7789: return createSt7789();
    default: return createNone();
    }
}

}  // namespace Display
