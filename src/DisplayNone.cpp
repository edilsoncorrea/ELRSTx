// No display fitted (DISPLAY_TYPE == DISPLAY_NONE): status only through the LED, buzzer and serial.
#include "Pins.h"
#if DISPLAY_TYPE == DISPLAY_NONE
#include "Display.h"

namespace Display {
bool begin() { return false; }
void splash(const char*) {}
void home(const HomeView&) {}
void list(const ListView&) {}
void channels(const uint16_t*, uint8_t) {}
void telemetry(const TelemetryView&) {}
void message(const MessageView&) {}
}  // namespace Display
#endif
