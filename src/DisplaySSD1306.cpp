// OLED 0.96" SSD1306 128x64, I2C, 1 bit. 21 columns x 8 rows of the 6x8 font.
#include "Pins.h"
#if DISPLAY_TYPE == DISPLAY_SSD1306
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Display.h"

namespace Display {
namespace {

constexpr uint16_t ON = 1;
constexpr uint16_t OFF = 0;
constexpr uint8_t COLS = 20;      // list rows: 2 px margin + 20 chars
constexpr uint8_t LIST_ROWS = 6;

Adafruit_SSD1306 oled(128, 64, &Wire, -1, 400000UL, 400000UL);
uint8_t scroll = 0;

void text(int16_t x, int16_t y, const char* s) {
    oled.setCursor(x, y);
    oled.print(s);
}

void header(const char* title) {
    text(0, 0, title);
    oled.drawFastHLine(0, 9, 128, ON);
}

void bar(int16_t x, int16_t y, int16_t w, int16_t h, float f) {
    oled.drawRect(x, y, w, h, ON);
    oled.fillRect(x + 1, y + 1, (int16_t)((w - 2) * f), h - 2, ON);
}

// Square with a dot at the stick position (EdgeTX style), 24x24
void gimbal(int16_t x, int16_t y, const float* pos) {
    constexpr int16_t S = 24;
    oled.drawRect(x, y, S, S, ON);
    oled.drawPixel(x + S / 2, y + S / 2, ON);
    const int16_t px = x + 3 + (int16_t)(pos[0] * (S - 7));
    const int16_t py = y + 3 + (int16_t)((1 - pos[1]) * (S - 7));
    oled.fillCircle(px, py, 2, ON);
}

void inverted(int16_t y, const char* s) {
    oled.fillRect(0, y - 1, 128, 9, ON);
    oled.setTextColor(OFF);
    text(2, y, s);
    oled.setTextColor(ON);
}

void start() {
    oled.fillScreen(OFF);
    oled.setTextColor(ON);
    oled.setTextWrap(true);
}

// "Label        value" in COLS characters; the value wins when space runs out
String formatRow(const Row& r, bool edited) {
    if (r.command) return "[" + String(r.label).substring(0, COLS - 2) + "]";
    String v = r.folder ? String(">") : String(r.value);
    if (v.length() > 11) v = v.substring(0, 11);
    if (edited) v = "<" + v + ">";
    String l = r.label;
    const int room = v.length() ? COLS - (int)v.length() - 1 : COLS;
    if ((int)l.length() > room) l = l.substring(0, max(room, 0));
    while (l.length() + v.length() < COLS) l += ' ';
    return l + v;
}

}  // namespace

bool begin() {
    Wire.beginTransmission(I2C_ADDR_OLED);
    if (Wire.endTransmission() != 0) return false;
    if (!oled.begin(SSD1306_SWITCHCAPVCC, I2C_ADDR_OLED, true, false)) return false;
    oled.setTextSize(1);
    return true;
}

void splash(const char* version) {
    start();
    text(28, 20, "ELRSTx ");
    oled.print(version);
    text(28, 34, "starting...");
    oled.display();
}

void home(const HomeView& v) {
    char buf[24];
    start();
    text(0, 0, v.title);
    if (v.txBatteryMv) {
        snprintf(buf, sizeof(buf), "%.1fV", v.txBatteryMv / 1000.0f);
        text(128 - strlen(buf) * 6, 0, buf);
    }
    oled.drawFastHLine(0, 9, 128, ON);

    // An alarm takes the module line, inverted
    if (v.alarm != Level::None) inverted(12, v.alarmText);
    else text(0, 12, !v.moduleAlive ? "No ELRS module" : v.deviceName[0] ? v.deviceName : "ELRS module OK");

    if (v.link) snprintf(buf, sizeof(buf), "LQ %3u%%  -%udBm", v.lq, v.rssi);
    else snprintf(buf, sizeof(buf), "No receiver link");
    text(0, 21, buf);

    String rate = String(v.rateHz) + "Hz";
    if (!v.synced) rate += " nosync";
    if (v.rxBatteryValid) rate += "  " + String(v.rxBatteryDv / 10.0f, 1) + "V";
    text(0, 30, rate.c_str());

    // Gimbals in the bottom corners, flight mode and AUX switches between them
    gimbal(0, 40, v.gimbal[0]);
    gimbal(104, 40, v.gimbal[1]);
    if (v.flightMode[0]) {
        String m = String(v.flightMode).substring(0, 12);
        text(64 - m.length() * 3, 42, m.c_str());
    }
    for (uint8_t i = 0; i < 4; i++) {
        const int16_t x = 33 + i * 17;
        if (v.aux[i]) oled.fillRect(x, 56, 11, 7, ON);
        else oled.drawRect(x, 56, 11, 7, ON);
    }
    oled.display();
}

void list(const ListView& v) {
    start();
    header(v.title);
    if (v.busy) text(122, 0, "*");
    if (v.cursor < scroll) scroll = v.cursor;
    if (v.cursor >= scroll + LIST_ROWS) scroll = v.cursor - (LIST_ROWS - 1);
    if (scroll + LIST_ROWS > v.count) scroll = v.count > LIST_ROWS ? v.count - LIST_ROWS : 0;
    oled.setTextWrap(false);
    for (uint8_t row = 0; row < LIST_ROWS && scroll + row < v.count; row++) {
        const uint8_t i = scroll + row;
        Row r = {};
        v.row(i, r);
        const String s = formatRow(r, v.editing && i == v.cursor);
        const int16_t y = 12 + row * 9;
        if (i == v.cursor) inverted(y, s.c_str());
        else text(2, y, s.c_str());
    }
    oled.display();
}

void channels(const uint16_t* ch, uint8_t count) {
    char buf[8];
    start();
    for (uint8_t i = 0; i < 8 && i < count; i++) {
        const int16_t y = i * 8;
        snprintf(buf, sizeof(buf), "%u", i + 1);
        text(0, y, buf);
        bar(10, y + 1, 84, 6, channelFraction(ch[i]));
        snprintf(buf, sizeof(buf), "%4d", channelUs(ch[i]));
        text(100, y, buf);
    }
    oled.display();
}

void telemetry(const TelemetryView& v) {
    char buf[24];
    start();
    header("Telemetry");
    if (v.link) {
        snprintf(buf, sizeof(buf), "Up -%u/-%u LQ%u", v.rssi1, v.rssi2, v.lq);
        text(0, 12, buf);
        snprintf(buf, sizeof(buf), "SNR %d  Dn -%u LQ%u", v.snr, v.downRssi, v.downLq);
        text(0, 21, buf);
    } else {
        text(0, 12, "No receiver link");
    }
    if (v.battery) {
        snprintf(buf, sizeof(buf), "%.1fV %.1fA %lumAh", v.batteryDv / 10.0f, v.currentDa / 10.0f,
                 (unsigned long)v.capacityMah);
        text(0, 30, buf);
    }
    if (v.attitude) {
        snprintf(buf, sizeof(buf), "P%d R%d Y%d", v.pitchDeg, v.rollDeg, v.yawDeg);
        text(0, 39, buf);
    }
    if (v.gps) {
        snprintf(buf, sizeof(buf), "GPS %u sats %dm", v.satellites, v.altitudeM);
        text(0, 48, buf);
    }
    if (v.flightMode[0]) text(0, 57, v.flightMode);
    oled.display();
}

void message(const MessageView& v) {
    start();
    header(v.title);
    for (uint8_t i = 0; i < MESSAGE_LINES; i++) {
        const int16_t y = 12 + i * 9;
        if (i == v.highlight) {
            inverted(y, v.line[i] ? v.line[i] : "");
        } else if (v.line[i]) {
            text(0, y, v.line[i]);
        }
    }
    if (v.footer) {
        oled.fillRect(0, 55, 128, 9, OFF);
        text(0, 56, v.footer);
    }
    oled.display();
}

}  // namespace Display
#endif
