// IPS ST7789 240x240, SPI, RGB565. Own colour layout; text size 2 (12x16 px) = 20 columns.
//
// A full 240x240x16-bit frame (115 KB) does not reliably fit in one heap block next to
// WiFi, so each frame is drawn in horizontal bands: the screen function runs once per
// band into a small buffer, which is then pushed to the panel. No flicker, 19 KB RAM.
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "Display.h"

namespace Display {
namespace st7789 {
namespace {

constexpr int16_t SIZE = 240;
constexpr int16_t BAND_H = 40;

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
constexpr uint16_t BG = rgb(0, 0, 0);
constexpr uint16_t FG = rgb(235, 235, 235);
constexpr uint16_t DIM = rgb(130, 135, 150);
constexpr uint16_t HEADER_BG = rgb(28, 32, 48);
constexpr uint16_t ACCENT = rgb(0, 200, 230);
constexpr uint16_t SELECT_BG = rgb(0, 70, 140);
constexpr uint16_t GOOD = rgb(40, 210, 90);
constexpr uint16_t WARN = rgb(255, 190, 0);
constexpr uint16_t CRIT = rgb(240, 50, 50);
constexpr uint16_t OUTLINE = rgb(90, 95, 110);

// Layout (also used by docs/screens/render_screens.py)
constexpr int16_t HEADER_H = 28;
constexpr int16_t ROW_H = 22;
constexpr int16_t LIST_TOP = 32;
constexpr uint8_t LIST_ROWS = 9;
constexpr uint8_t COLS = 19;  // list text columns, leaves room for the scrollbar

// Adafruit_GFX target that only keeps rows [top, top + BAND_H) of the full screen
class Band : public Adafruit_GFX {
public:
    Band() : Adafruit_GFX(SIZE, SIZE) {}
    uint16_t buf[SIZE * BAND_H];
    int16_t top = 0;

    void drawPixel(int16_t x, int16_t y, uint16_t c) override {
        y -= top;
        if ((uint16_t)x < SIZE && (uint16_t)y < BAND_H) buf[y * SIZE + x] = c;
    }
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) override {
        int16_t y0 = max<int16_t>(y, top), y1 = min<int16_t>(y + h, top + BAND_H);
        int16_t x0 = max<int16_t>(x, 0), x1 = min<int16_t>(x + w, SIZE);
        for (int16_t yy = y0; yy < y1; yy++) {
            uint16_t* p = &buf[(yy - top) * SIZE + x0];
            for (int16_t xx = x0; xx < x1; xx++) *p++ = c;
        }
    }
    void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) override { fillRect(x, y, w, 1, c); }
    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) override { fillRect(x, y, 1, h, c); }
    void fillScreen(uint16_t c) override {
        for (auto& p : buf) p = c;
    }
};

Adafruit_ST7789* tft = nullptr;
Band* band = nullptr;
Adafruit_GFX* g = nullptr;
uint8_t scroll = 0;

template <typename F>
void render(F draw) {
    tft->startWrite();
    for (int16_t top = 0; top < SIZE; top += BAND_H) {
        band->top = top;
        band->fillScreen(BG);
        g->setTextWrap(false);
        draw();
        tft->setAddrWindow(0, top, SIZE, BAND_H);
        tft->writePixels(band->buf, SIZE * BAND_H);
    }
    tft->endWrite();
}

void text(int16_t x, int16_t y, const char* s, uint16_t color = FG, uint8_t size = 2) {
    g->setTextSize(size);
    g->setTextColor(color);
    g->setCursor(x, y);
    g->print(s);
}

int16_t textWidth(const char* s, uint8_t size = 2) { return strlen(s) * 6 * size; }

void textRight(int16_t right, int16_t y, const char* s, uint16_t color = FG, uint8_t size = 2) {
    text(right - textWidth(s, size), y, s, color, size);
}

void header(const char* title, const char* right = nullptr, uint16_t rightColor = FG) {
    g->fillRect(0, 0, SIZE, HEADER_H, HEADER_BG);
    g->drawFastHLine(0, HEADER_H - 1, SIZE, ACCENT);
    text(8, 6, title, ACCENT);
    if (right) textRight(SIZE - 8, 6, right, rightColor);
}

uint16_t levelColor(Level l) { return l == Level::Crit ? CRIT : l == Level::Warn ? WARN : GOOD; }
uint16_t lqColor(uint8_t lq) { return lq >= 80 ? GOOD : lq >= 50 ? WARN : CRIT; }

void bar(int16_t x, int16_t y, int16_t w, int16_t h, float f, uint16_t color) {
    g->drawRect(x, y, w, h, OUTLINE);
    g->fillRect(x + 2, y + 2, (int16_t)((w - 4) * f), h - 4, color);
}

// Square with a dot at the stick position (EdgeTX style)
constexpr int16_t GIMBAL = 70;
void gimbal(int16_t x, int16_t y, const float* pos) {
    constexpr int16_t R = 6;
    g->drawRect(x, y, GIMBAL, GIMBAL, OUTLINE);
    g->drawFastHLine(x + 1, y + GIMBAL / 2, GIMBAL - 2, HEADER_BG);
    g->drawFastVLine(x + GIMBAL / 2, y + 1, GIMBAL - 2, HEADER_BG);
    const int16_t px = x + R + 2 + (int16_t)(pos[0] * (GIMBAL - 2 * R - 4));
    const int16_t py = y + R + 2 + (int16_t)((1 - pos[1]) * (GIMBAL - 2 * R - 4));
    g->fillCircle(px, py, R, ACCENT);
}

}  // namespace

bool begin(const HardwareConfig& hw) {
    // Allocated only when this display is selected (19 KB band + driver)
    band = new Band();
    g = band;
    tft = new Adafruit_ST7789(&SPI, -1, hw.tftDc, hw.tftRst);
    SPI.begin(hw.tftSclk, -1, hw.tftMosi, -1);
    tft->init(SIZE, SIZE, SPI_MODE3);  // 240x240 modules without a CS pin need mode 3
    tft->setSPISpeed(40000000);
    tft->setRotation(hw.tftRotation);
    tft->fillScreen(BG);
    return true;  // write-only bus, presence cannot be detected
}

void splash(const char* version) {
    render([&] {
        text(48, 84, "ELRSTx", ACCENT, 4);
        textRight(192, 124, version, DIM);
        text(54, 160, "starting...", FG);
    });
}

void home(const HomeView& v) {
    char batt[12] = "";
    if (v.txBatteryMv) snprintf(batt, sizeof(batt), "%.1fV", v.txBatteryMv / 1000.0f);
    render([&] {
        header(v.title, batt, levelColor(v.txBatteryLevel));

        // Module
        if (!v.moduleAlive) text(8, 36, "No ELRS module", WARN);
        else text(8, 36, v.deviceName[0] ? v.deviceName : "ELRS module OK", FG);

        // Link quality, big
        char buf[24];
        if (v.link) {
            text(8, 66, "LQ", DIM);
            snprintf(buf, sizeof(buf), "%u%%", v.lq);
            text(40, 58, buf, lqColor(v.lq), 4);
            snprintf(buf, sizeof(buf), "-%udBm", v.rssi);
            textRight(SIZE - 8, 66, buf, FG);
        } else {
            text(8, 66, "No receiver link", CRIT);
        }

        // Rate and receiver battery
        snprintf(buf, sizeof(buf), "%uHz", v.rateHz);
        text(8, 100, buf, v.synced ? FG : WARN);
        if (!v.synced) text(8 + textWidth(buf) + 8, 100, "nosync", WARN);
        if (v.rxBatteryValid) {
            snprintf(buf, sizeof(buf), "RX %.1fV", v.rxBatteryDv / 10.0f);
            textRight(SIZE - 8, 100, buf, FG);
        }

        // Alarm banner or flight mode
        if (v.alarm != Level::None) {
            g->fillRoundRect(4, 124, SIZE - 8, 26, 4, levelColor(v.alarm));
            const int16_t w = textWidth(v.alarmText);
            text((SIZE - w) / 2, 129, v.alarmText, BG);
        } else if (v.flightMode[0]) {
            text(8, 129, v.flightMode, ACCENT);
        }

        // Gimbals in the bottom corners, AUX switches 2x2 between them
        gimbal(8, 160, v.gimbal[0]);
        gimbal(SIZE - 8 - GIMBAL, 160, v.gimbal[1]);
        for (uint8_t i = 0; i < 4; i++) {
            const int16_t x = 86 + (i % 2) * 36;
            const int16_t y = 162 + (i / 2) * 36;
            char l[4];
            snprintf(l, sizeof(l), "A%u", i + 1);
            if (v.aux[i]) {
                g->fillRoundRect(x, y, 32, 32, 4, GOOD);
                text(x + 4, y + 9, l, BG);
            } else {
                g->drawRoundRect(x, y, 32, 32, 4, OUTLINE);
                text(x + 4, y + 9, l, DIM);
            }
        }
    });
}

void list(const ListView& v) {
    if (v.cursor < scroll) scroll = v.cursor;
    if (v.cursor >= scroll + LIST_ROWS) scroll = v.cursor - (LIST_ROWS - 1);
    if (scroll + LIST_ROWS > v.count) scroll = v.count > LIST_ROWS ? v.count - LIST_ROWS : 0;

    Row rows[LIST_ROWS];
    const uint8_t shown = min<uint8_t>(LIST_ROWS, v.count - scroll);
    for (uint8_t r = 0; r < shown; r++) {
        rows[r] = {};
        v.row(scroll + r, rows[r]);
    }

    render([&] {
        header(v.title, v.busy ? "..." : nullptr, DIM);
        for (uint8_t r = 0; r < shown; r++) {
            const uint8_t i = scroll + r;
            const Row& row = rows[r];
            const int16_t y = LIST_TOP + r * ROW_H;
            const bool selected = i == v.cursor;
            const bool edited = selected && v.editing;
            if (selected) g->fillRect(0, y, SIZE - 6, ROW_H, edited ? rgb(90, 60, 0) : SELECT_BG);

            String value = row.folder ? String(">") : String(row.value);
            if (value.length() > 11) value = value.substring(0, 11);
            if (edited) value = "<" + value + ">";
            String label = row.command ? "[" + String(row.label) + "]" : String(row.label);
            const int room = value.length() ? COLS - (int)value.length() - 1 : COLS;
            if ((int)label.length() > room) label = label.substring(0, max(room, 0));

            text(4, y + 3, label.c_str(), row.command ? ACCENT : FG);
            if (value.length()) textRight(SIZE - 10, y + 3, value.c_str(), edited ? WARN : row.folder ? DIM : ACCENT);
        }
        // Scrollbar
        if (v.count > LIST_ROWS) {
            const int16_t trackH = LIST_ROWS * ROW_H;
            const int16_t h = max<int16_t>(trackH * LIST_ROWS / v.count, 10);
            const int16_t y = LIST_TOP + (trackH - h) * scroll / (v.count - LIST_ROWS);
            g->fillRect(SIZE - 4, LIST_TOP, 4, trackH, HEADER_BG);
            g->fillRect(SIZE - 4, y, 4, h, OUTLINE);
        }
    });
}

void channels(const uint16_t* ch, uint8_t count) {
    const uint8_t n = min<uint8_t>(count, 12);
    render([&] {
        header("Channels");
        char buf[8];
        for (uint8_t i = 0; i < n; i++) {
            const int16_t y = 34 + i * 17;
            snprintf(buf, sizeof(buf), "%u", i + 1);
            textRight(22, y + 1, buf, DIM, 1);
            bar(30, y, 150, 13, channelFraction(ch[i]), i < 4 ? ACCENT : GOOD);
            snprintf(buf, sizeof(buf), "%d", channelUs(ch[i]));
            textRight(SIZE - 8, y + 3, buf, FG, 1);
        }
    });
}

void telemetry(const TelemetryView& v) {
    render([&] {
        header("Telemetry");
        char buf[24];
        int16_t y = 34;
        auto line = [&](const char* label, const char* value, uint16_t color) {
            text(8, y, label, DIM);
            textRight(SIZE - 8, y, value, color);
            y += ROW_H;
        };
        if (v.link) {
            snprintf(buf, sizeof(buf), "-%u/-%udBm", v.rssi1, v.rssi2);
            line("RSSI", buf, FG);
            snprintf(buf, sizeof(buf), "%u%%", v.lq);
            line("LQ up", buf, lqColor(v.lq));
            snprintf(buf, sizeof(buf), "%u%% -%u", v.downLq, v.downRssi);
            line("LQ down", buf, lqColor(v.downLq));
            snprintf(buf, sizeof(buf), "%d dB", v.snr);
            line("SNR", buf, FG);
        } else {
            text(8, y, "No receiver link", CRIT);
            y += ROW_H;
        }
        if (v.battery) {
            snprintf(buf, sizeof(buf), "%.1fV %.1fA", v.batteryDv / 10.0f, v.currentDa / 10.0f);
            line("Batt", buf, FG);
            snprintf(buf, sizeof(buf), "%lumAh", (unsigned long)v.capacityMah);
            line("Used", buf, FG);
        }
        if (v.attitude) {
            snprintf(buf, sizeof(buf), "%d %d %d", v.pitchDeg, v.rollDeg, v.yawDeg);
            line("P R Y", buf, FG);
        }
        if (v.gps) {
            snprintf(buf, sizeof(buf), "%u sat %dm", v.satellites, v.altitudeM);
            line("GPS", buf, FG);
        }
        if (v.flightMode[0]) line("Mode", v.flightMode, ACCENT);
    });
}

void message(const MessageView& v) {
    render([&] {
        header(v.title);
        g->setTextWrap(true);
        for (uint8_t i = 0; i < MESSAGE_LINES; i++) {
            const int16_t y = LIST_TOP + i * ROW_H;
            if (i == v.highlight) g->fillRect(0, y, SIZE, ROW_H, SELECT_BG);
            if (v.line[i]) text(8, y + 3, v.line[i], FG);
        }
        if (v.footer) {
            g->fillRect(0, SIZE - 26, SIZE, 26, HEADER_BG);
            text(8, SIZE - 21, v.footer, DIM);
        }
    });
}

}  // namespace st7789

class St7789 : public Driver {
public:
    bool begin(const HardwareConfig& hw) override { return st7789::begin(hw); }
    void splash(const char* version) override { st7789::splash(version); }
    void home(const HomeView& v) override { st7789::home(v); }
    void list(const ListView& v) override { st7789::list(v); }
    void channels(const uint16_t* ch, uint8_t count) override { st7789::channels(ch, count); }
    void telemetry(const TelemetryView& v) override { st7789::telemetry(v); }
    void message(const MessageView& v) override { st7789::message(v); }
};

Driver* createSt7789() {
    static St7789 d;
    return &d;
}

}  // namespace Display
