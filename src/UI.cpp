#include "UI.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include "Commands.h"
#include "Config.h"
#include "Inputs.h"
#include "Pins.h"
#include "Safety.h"
#include "State.h"
#include "ElrsParams.h"

#if DISPLAY_TYPE == DISPLAY_SSD1306
#include <Adafruit_SSD1306.h>
#elif DISPLAY_TYPE == DISPLAY_ST7789
#include <SPI.h>
#include <Adafruit_ST7789.h>
#endif

namespace {

constexpr uint16_t COLOR_ON = 1;
constexpr uint16_t COLOR_OFF = 0;

#if DISPLAY_TYPE == DISPLAY_SSD1306
Adafruit_SSD1306 oled(128, 64, &Wire, -1, 400000UL, 400000UL);
Adafruit_GFX& gfx = oled;
#else
// The UI is laid out for 128x64; other panels get this 1-bit canvas scaled up
GFXcanvas1 canvas(128, 64);
Adafruit_GFX& gfx = canvas;
#endif

#if DISPLAY_TYPE == DISPLAY_ST7789
Adafruit_ST7789 tft(&SPI, -1, PIN_TFT_DC, PIN_TFT_RST);
constexpr int16_t TFT_SIZE = 240;
constexpr int16_t TFT_VIEW_H = 120;  // 64 * 240 / 128, centred vertically
constexpr uint16_t TFT_FG = ST77XX_WHITE;
constexpr uint16_t TFT_BG = ST77XX_BLACK;
#endif

bool displayFound = false;

bool displayBegin() {
#if DISPLAY_TYPE == DISPLAY_SSD1306
    Wire.beginTransmission(I2C_ADDR_OLED);
    return Wire.endTransmission() == 0 && oled.begin(SSD1306_SWITCHCAPVCC, I2C_ADDR_OLED, true, false);
#elif DISPLAY_TYPE == DISPLAY_ST7789
    SPI.begin(PIN_TFT_SCLK, -1, PIN_TFT_MOSI, -1);
    tft.init(TFT_SIZE, TFT_SIZE, SPI_MODE3);  // 240x240 modules without a CS pin need mode 3
    tft.setSPISpeed(40000000);
    tft.setRotation(TFT_ROTATION);
    tft.fillScreen(TFT_BG);
    return true;  // write-only bus, presence cannot be detected
#else
    return false;
#endif
}

void present() {
#if DISPLAY_TYPE == DISPLAY_SSD1306
    oled.display();
#elif DISPLAY_TYPE == DISPLAY_ST7789
    static uint16_t line[TFT_SIZE];
    static uint8_t srcX[TFT_SIZE];
    static bool mapped = false;
    if (!mapped) {
        for (int16_t x = 0; x < TFT_SIZE; x++) srcX[x] = x * 128 / TFT_SIZE;
        mapped = true;
    }
    tft.startWrite();
    tft.setAddrWindow(0, (TFT_SIZE - TFT_VIEW_H) / 2, TFT_SIZE, TFT_VIEW_H);
    for (int16_t y = 0; y < TFT_VIEW_H; y++) {
        const int16_t sy = y * 64 / TFT_VIEW_H;
        for (int16_t x = 0; x < TFT_SIZE; x++) line[x] = canvas.getPixel(srcX[x], sy) ? TFT_FG : TFT_BG;
        tft.writePixels(line, TFT_SIZE);
    }
    tft.endWrite();
#endif
}
bool expOk = false;

enum Screen : uint8_t { HOME, MENU, ELRS, CHANNELS, TELEMETRY, CALIBRATE, SETTINGS, PROFILES, REMOTE, INFO };
const char* const MENU_LABELS[] = {"ExpressLRS", "Channels", "Telemetry", "Calibrate", "Settings", "Profiles", "WiFi / OTA", "Info"};
const Screen MENU_TARGETS[] = {ELRS, CHANNELS, TELEMETRY, CALIBRATE, SETTINGS, PROFILES, REMOTE, INFO};
constexpr uint8_t MENU_COUNT = sizeof(MENU_TARGETS) / sizeof(MENU_TARGETS[0]);

enum SettingId : uint8_t {
    S_DEADBAND, S_INV_ROLL, S_INV_PITCH, S_INV_THR, S_INV_YAW, S_SAFESTART,
    S_LINKALARM, S_LINKTIMEOUT, S_CELLS, S_WARN, S_CRIT, S_SIM, S_COUNT
};
const char* const SETTING_LABELS[S_COUNT] = {
    "Deadband", "Invert roll", "Invert pitch", "Invert thr", "Invert yaw", "Safe start",
    "Link alarm", "Link timeout", "TX cells", "Warn mV/cell", "Crit mV/cell", "Simulation"};

enum AlarmLevel : uint8_t { ALARM_NONE, ALARM_WARN, ALARM_CRIT };

Screen screen = HOME;
uint8_t cursor = 0;
uint8_t scroll = 0;
bool editing = false;
bool settingsDirty = false;
uint8_t calStep = 0;
String calResult;
String profileNames[PROFILE_COUNT];

// ExpressLRS screen: current folder, its visible parameters, value being edited, open command popup
uint8_t elrsFolder = 0;
uint8_t elrsIds[ElrsParams::MAX_PARAMS];
uint8_t elrsCount = 0;
int16_t elrsEditValue = 0;
uint8_t elrsCmdId = 0;

const uint8_t REPEAT_BUTTONS[] = {EXP_BTN_UP, EXP_BTN_DOWN};
uint8_t buttonsPrev = 0;
uint32_t heldSince[4] = {};
uint32_t lastRepeat[4] = {};
uint32_t lastTickMs = 0;
uint32_t lastDrawMs = 0;

RcState st;
Settings cfg;
uint32_t nowMs = 0;

AlarmLevel alarmLevel = ALARM_NONE;
const char* alarmText = "";
bool alarmBeeps = false;
bool hadLink = false;
uint32_t ledColor = 0xFFFFFFFF;

#if !INPUTS_VIA_EXPANDER
const uint8_t INPUT_PINS[8] = {PIN_BTN_UP,  PIN_BTN_DOWN, PIN_BTN_OK,  PIN_BTN_BACK,
                               PIN_SW_AUX1, PIN_SW_AUX2,  PIN_SW_AUX3, PIN_SW_AUX4};
#endif

// Returns the input mask (bit set = pressed / switch on), see EXP_* in Pins.h
uint8_t readInputs() {
#if INPUTS_VIA_EXPANDER
    if (!expOk) return 0;
    if (Wire.requestFrom(I2C_ADDR_EXPANDER, 1) != 1) return 0;
    return ~Wire.read();
#else
    uint8_t mask = 0;
    for (uint8_t i = 0; i < 8; i++) {
        if (digitalRead(INPUT_PINS[i]) == LOW) mask |= 1 << i;
    }
    return mask;
#endif
}

void setLed(uint8_t r, uint8_t g, uint8_t b) {
    const uint32_t c = (uint32_t)r << 16 | (uint32_t)g << 8 | b;
    if (c == ledColor) return;
    ledColor = c;
#if LED_IS_RGB
    rgbLedWrite(PIN_LED, r, g, b);
#else
    digitalWrite(PIN_LED, (c != 0) == (LED_ACTIVE_LOW == 0) ? HIGH : LOW);
#endif
}

void setLed(bool on) { setLed(0, 0, on ? 1 : 0); }

uint8_t edges(uint8_t buttons) {
    uint8_t pressed = buttons & ~buttonsPrev;
    for (uint8_t b : REPEAT_BUTTONS) {
        if (!(buttons & bit(b))) continue;
        if (!(buttonsPrev & bit(b))) {
            heldSince[b] = nowMs;
            lastRepeat[b] = nowMs;
        } else if (nowMs - heldSince[b] > 500 && nowMs - lastRepeat[b] >= 120) {
            pressed |= bit(b);
            lastRepeat[b] = nowMs;
        }
    }
    buttonsPrev = buttons;
    return pressed;
}

bool linkUp() { return State::linkUp(st, nowMs, cfg.model.linkTimeoutMs); }

void updateAlarms() {
    const bool link = linkUp();
    if (link) hadLink = true;
    const bool battPresent = st.txBatteryMv > 1000;
    const uint16_t cellMv = battPresent ? st.txBatteryMv / cfg.hw.battCells : 0;

    alarmLevel = ALARM_NONE;
    alarmText = "";
    alarmBeeps = false;
    if (cfg.model.linkAlarm && hadLink && !link) {
        alarmLevel = ALARM_CRIT;
        alarmText = "LINK LOST";
        alarmBeeps = true;
    } else if (battPresent && cellMv < cfg.hw.battCritMv) {
        alarmLevel = ALARM_CRIT;
        alarmText = "TX BATTERY CRITICAL";
        alarmBeeps = true;
    } else if (battPresent && cellMv < cfg.hw.battWarnMv) {
        alarmLevel = ALARM_WARN;
        alarmText = "TX BATTERY LOW";
        alarmBeeps = true;
    } else if (st.safeStartHold) {
        alarmLevel = ALARM_WARN;
        alarmText = "THR DOWN + DISARM";
    } else if (!State::moduleAlive(st, nowMs)) {
        alarmLevel = ALARM_WARN;
        alarmText = "NO ELRS MODULE";
    }
}

void updateIndicators() {
    const bool fast = (nowMs / 125) & 1;
    const bool slow = (nowMs / 500) & 1;
    const bool remote = Remote::wifiActive();
#if LED_IS_RGB
    if (alarmLevel == ALARM_CRIT) setLed(fast ? 40 : 0, 0, 0);
    else if (alarmLevel == ALARM_WARN) setLed(slow ? 30 : 0, slow ? 20 : 0, 0);
    else if (remote) setLed(20, 0, 30);
    else if (linkUp()) setLed(0, 30, 0);
    else setLed(0, 0, slow ? 30 : 0);
#else
    const uint32_t phase = nowMs % 1000;
    if (alarmLevel == ALARM_CRIT) setLed(fast);                          // fast blink
    else if (alarmLevel == ALARM_WARN) setLed(phase < 100 || (phase >= 200 && phase < 300));  // double blip
    else if (remote) setLed(phase >= 100);                               // on with a short gap
    else if (linkUp()) setLed(true);                                     // solid
    else setLed(slow);                                                   // slow blink
#endif

    bool buzz = false;
    if (alarmBeeps && alarmLevel == ALARM_CRIT) buzz = nowMs % 500 < 100;
    else if (alarmBeeps) buzz = nowMs % 3000 < 60;
    digitalWrite(PIN_BUZZER, buzz ? HIGH : LOW);
}

// ---------- settings ----------

bool settingIsToggle(uint8_t i) { return (i >= S_INV_ROLL && i <= S_LINKALARM) || i == S_SIM; }

String settingValue(uint8_t i) {
    switch (i) {
    case S_DEADBAND: return String(cfg.model.deadband);
    case S_INV_ROLL:
    case S_INV_PITCH:
    case S_INV_THR:
    case S_INV_YAW: return cfg.model.invert[i - S_INV_ROLL] ? "ON" : "off";
    case S_SAFESTART: return cfg.model.safeStart ? "ON" : "off";
    case S_LINKALARM: return cfg.model.linkAlarm ? "ON" : "off";
    case S_LINKTIMEOUT: return String(cfg.model.linkTimeoutMs) + "ms";
    case S_CELLS: return String(cfg.hw.battCells) + "S";
    case S_WARN: return String(cfg.hw.battWarnMv);
    case S_CRIT: return String(cfg.hw.battCritMv);
    case S_SIM: return cfg.model.simulation ? "ON" : "off";
    }
    return "";
}

void adjustSetting(uint8_t i, int dir) {
    Settings s = Config::get();
    switch (i) {
    case S_DEADBAND: s.model.deadband = constrain((int)s.model.deadband + dir * 5, 0, 200); break;
    case S_INV_ROLL:
    case S_INV_PITCH:
    case S_INV_THR:
    case S_INV_YAW: s.model.invert[i - S_INV_ROLL] = !s.model.invert[i - S_INV_ROLL]; break;
    case S_SAFESTART: s.model.safeStart = !s.model.safeStart; break;
    case S_LINKALARM: s.model.linkAlarm = !s.model.linkAlarm; break;
    case S_LINKTIMEOUT: s.model.linkTimeoutMs = constrain((int)s.model.linkTimeoutMs + dir * 100, 200, 5000); break;
    case S_CELLS: s.hw.battCells = constrain((int)s.hw.battCells + dir, 1, 6); break;
    case S_WARN: s.hw.battWarnMv = constrain((int)s.hw.battWarnMv + dir * 50, 3000, 4200); break;
    case S_CRIT: s.hw.battCritMv = constrain((int)s.hw.battCritMv + dir * 50, 3000, 4200); break;
    case S_SIM: s.model.simulation = !s.model.simulation; break;
    }
    Config::set(s);
    cfg = s;
    settingsDirty = true;
}

// ---------- navigation ----------

void enter(Screen s) {
    screen = s;
    cursor = 0;
    scroll = 0;
    editing = false;
    calStep = 0;
    if (s == PROFILES) {
        for (uint8_t i = 0; i < PROFILE_COUNT; i++) profileNames[i] = Config::profileName(i);
        cursor = cfg.profile;
    }
    if (s == ELRS) {
        elrsFolder = 0;
        elrsCmdId = 0;
        ElrsParams::touch();
    }
}

void moveCursor(bool up, bool down, uint8_t count) {
    if (up) cursor = (cursor + count - 1) % count;
    if (down) cursor = (cursor + 1) % count;
}

void elrsButtons(bool up, bool down, bool ok, bool back) {
    using namespace ElrsParams;
    if (elrsCmdId) {
        Param p;
        const bool known = param(elrsCmdId, p);
        if (known && p.value == CMD_ASK_CONFIRM) {
            if (ok) {
                command(elrsCmdId, CMD_CONFIRM);
            } else if (back) {
                command(elrsCmdId, CMD_CANCEL);
                elrsCmdId = 0;
            }
        } else if (ok || back) {
            if (known && p.value == CMD_EXECUTING && back) command(elrsCmdId, CMD_CANCEL);
            elrsCmdId = 0;
        }
        return;
    }
    if (editing) {
        Param p;
        if (cursor >= elrsCount || !param(elrsIds[cursor], p)) {
            editing = false;
            return;
        }
        if (up) elrsEditValue = nextValue(p, elrsEditValue, +1);
        if (down) elrsEditValue = nextValue(p, elrsEditValue, -1);
        if (ok) {
            setValue(p.id, elrsEditValue);
            editing = false;
        } else if (back) {
            editing = false;
        }
        return;
    }
    if (elrsCount) moveCursor(up, down, elrsCount);
    if (ok && cursor < elrsCount) {
        Param p;
        if (!param(elrsIds[cursor], p)) return;
        if (p.type == T_FOLDER) {
            elrsFolder = p.id;
            cursor = 0;
            scroll = 0;
        } else if (p.type == T_SELECT || p.type <= T_INT16) {
            editing = true;
            elrsEditValue = p.value;
        } else if (p.type == T_COMMAND) {
            command(p.id, CMD_CLICK);
            elrsCmdId = p.id;
        }
    } else if (back) {
        if (elrsFolder) {
            Param f;
            elrsFolder = param(elrsFolder, f) ? f.parent : 0;
            cursor = 0;
            scroll = 0;
        } else {
            enter(MENU);
        }
    }
}

void handleButtons(uint8_t pressed) {
    if (!pressed) return;
    const bool up = pressed & bit(EXP_BTN_UP);
    const bool down = pressed & bit(EXP_BTN_DOWN);
    const bool ok = pressed & bit(EXP_BTN_OK);
    const bool back = pressed & bit(EXP_BTN_BACK);

    switch (screen) {
    case HOME:
        if (ok) enter(MENU);
        break;
    case MENU:
        moveCursor(up, down, MENU_COUNT);
        if (ok) enter(MENU_TARGETS[cursor]);
        else if (back) enter(HOME);
        break;
    case ELRS:
        elrsButtons(up, down, ok, back);
        break;
    case CHANNELS:
    case TELEMETRY:
    case INFO:
        if (ok || back) enter(MENU);
        break;
    case SETTINGS:
        if (editing) {
            if (up) adjustSetting(cursor, +1);
            if (down) adjustSetting(cursor, -1);
            if (ok || back) editing = false;
        } else {
            moveCursor(up, down, S_COUNT);
            if (ok) {
                if (settingIsToggle(cursor)) adjustSetting(cursor, 1);
                else editing = true;
            } else if (back) {
                if (settingsDirty) Config::save();
                settingsDirty = false;
                enter(MENU);
            }
        }
        break;
    case PROFILES:
        moveCursor(up, down, PROFILE_COUNT);
        if (ok) {
            Config::selectProfile(cursor);
            cfg = Config::get();
            enter(HOME);
        } else if (back) {
            enter(MENU);
        }
        break;
    case REMOTE:
        if (ok) {
            Remote::setWifi(!Remote::wifiActive());
        } else if (back) {
            enter(MENU);
        }
        break;
    case CALIBRATE:
        if (calStep == 0) {
            if (ok) {
                Calibration::captureCenter(st.raw);
                Calibration::startRange(st.raw);
                calStep = 1;
            } else if (back) {
                enter(MENU);
            }
        } else if (calStep == 1) {
            if (ok) {
                String err;
                calResult = Calibration::finish(err) ? "Saved!" : "Failed: " + err;
                calStep = 2;
            } else if (back) {
                Calibration::cancel();
                enter(MENU);
            }
        } else {
            enter(MENU);
        }
        break;
    }
}

// ---------- drawing ----------

void text(int16_t x, int16_t y, const String& s) {
    gfx.setCursor(x, y);
    gfx.print(s);
}

void header(const char* title) {
    text(0, 0, title);
    gfx.drawFastHLine(0, 9, 128, COLOR_ON);
}

float channelFraction(uint16_t v) {
    return constrain((v - CRSF_CHANNEL_VALUE_MIN) / float(CRSF_CHANNEL_VALUE_MAX - CRSF_CHANNEL_VALUE_MIN), 0.0f, 1.0f);
}

void bar(int16_t x, int16_t y, int16_t w, int16_t h, float f) {
    gfx.drawRect(x, y, w, h, COLOR_ON);
    gfx.fillRect(x + 1, y + 1, (int16_t)((w - 2) * f), h - 2, COLOR_ON);
}

void drawList(const char* title, uint8_t count, String (*item)(uint8_t)) {
    header(title);
    if (cursor < scroll) scroll = cursor;
    if (cursor >= scroll + 6) scroll = cursor - 5;
    for (uint8_t row = 0; row < 6 && scroll + row < count; row++) {
        const uint8_t i = scroll + row;
        const int16_t y = 12 + row * 9;
        if (i == cursor) {
            gfx.fillRect(0, y - 1, 128, 9, COLOR_ON);
            gfx.setTextColor(COLOR_OFF);
        }
        text(2, y, item(i));
        gfx.setTextColor(COLOR_ON);
    }
}

void drawHome() {
    char buf[24];
    String title = cfg.model.name;
    if (cfg.model.simulation) title += " SIM";
    text(0, 0, title);
    if (st.txBatteryMv > 1000) {
        snprintf(buf, sizeof(buf), "%.1fV", st.txBatteryMv / 1000.0f);
        text(128 - strlen(buf) * 6, 0, buf);
    }
    gfx.drawFastHLine(0, 9, 128, COLOR_ON);

    if (!State::moduleAlive(st, nowMs)) text(0, 12, "No ELRS module");
    else text(0, 12, st.telem.deviceName[0] ? st.telem.deviceName : "ELRS module OK");

    if (linkUp()) snprintf(buf, sizeof(buf), "LQ %3u%%  -%udBm", st.telem.uplinkLq, st.telem.uplinkRssi1);
    else snprintf(buf, sizeof(buf), "No receiver link");
    text(0, 21, buf);

    String rate = String(1000000UL / st.periodUs) + "Hz";
    if (!State::syncActive(st, nowMs)) rate += " nosync";
    if (State::fresh(st.telem.lastBatteryMs, nowMs, 5000)) rate += "  " + String(st.telem.batteryDv / 10.0f, 1) + "V";
    text(0, 30, rate);

    if (alarmLevel != ALARM_NONE) {
        gfx.fillRect(0, 38, 128, 10, COLOR_ON);
        gfx.setTextColor(COLOR_OFF);
        text(2, 39, alarmText);
        gfx.setTextColor(COLOR_ON);
    } else if (st.telem.flightMode[0]) {
        text(0, 39, st.telem.flightMode);
    }

    for (uint8_t a = 0; a < AXIS_COUNT; a++) bar(a * 32, 50, 30, 6, channelFraction(st.channels[cfg.model.channel[a]]));
    for (uint8_t i = 0; i < AUX_COUNT; i++) {
        const bool on = st.channels[AUX_FIRST_CHANNEL + i] > CRSF_CHANNEL_VALUE_MID;
        if (on) gfx.fillRect(i * 32 + 11, 58, 8, 6, COLOR_ON);
        else gfx.drawRect(i * 32 + 11, 58, 8, 6, COLOR_ON);
    }
}

void drawChannels() {
    char buf[8];
    for (uint8_t i = 0; i < 8; i++) {
        const int16_t y = i * 8;
        snprintf(buf, sizeof(buf), "%u", i + 1);
        text(0, y, buf);
        bar(10, y + 1, 84, 6, channelFraction(st.channels[i]));
        snprintf(buf, sizeof(buf), "%4d", ((int)st.channels[i] - CRSF_CHANNEL_VALUE_MID) * 5 / 8 + 1500);
        text(100, y, buf);
    }
}

void drawTelemetry() {
    const CRSFTelemetry& t = st.telem;
    char buf[24];
    header("Telemetry");
    if (linkUp()) {
        snprintf(buf, sizeof(buf), "Up -%u/-%u LQ%u", t.uplinkRssi1, t.uplinkRssi2, t.uplinkLq);
        text(0, 12, buf);
        snprintf(buf, sizeof(buf), "SNR %d  Dn -%u LQ%u", t.uplinkSnr, t.downlinkRssi, t.downlinkLq);
        text(0, 21, buf);
    } else {
        text(0, 12, "No receiver link");
    }
    if (State::fresh(t.lastBatteryMs, nowMs, 5000)) {
        snprintf(buf, sizeof(buf), "%.1fV %.1fA %lumAh", t.batteryDv / 10.0f, t.currentDa / 10.0f, (unsigned long)t.capacityMah);
        text(0, 30, buf);
    }
    if (State::fresh(t.lastAttitudeMs, nowMs, 5000)) {
        snprintf(buf, sizeof(buf), "P%d R%d Y%d", (int)(t.pitch * 0.0057296f), (int)(t.roll * 0.0057296f),
                 (int)(t.yaw * 0.0057296f));
        text(0, 39, buf);
    }
    if (State::fresh(t.lastGpsMs, nowMs, 5000)) {
        snprintf(buf, sizeof(buf), "GPS %u sats %dm", t.satellites, (int)t.altitude - 1000);
        text(0, 48, buf);
    }
    if (t.flightMode[0]) text(0, 57, t.flightMode);
}

void drawCalibrate() {
    header("Calibrate");
    if (calStep == 0) {
        text(0, 14, "1. Center all sticks");
        text(0, 23, "   then press OK");
        text(0, 50, "BACK = cancel");
        return;
    }
    if (calStep == 1) {
        text(0, 12, "2. Move all sticks to");
        text(0, 21, "their limits, then OK");
        char buf[24];
        for (uint8_t a = 0; a < AXIS_COUNT; a++) {
            const AxisCal c = Calibration::range(a);
            snprintf(buf, sizeof(buf), "%c %4u %4u %4u", toupper(Config::axisName(a)[0]), c.min, st.raw[a], c.max);
            text(0, 30 + a * 8, buf);
        }
        return;
    }
    text(0, 20, calResult);
    text(0, 50, "Press any key");
}

void drawRemote() {
    header("WiFi / OTA");
    gfx.fillRect(0, 11, 128, 9, COLOR_ON);
    gfx.setTextColor(COLOR_OFF);
    text(2, 12, String("WiFi AP: ") + (Remote::wifiActive() ? "ON" : "off"));
    gfx.setTextColor(COLOR_ON);
    if (Remote::wifiActive()) {
        text(0, 30, Remote::ssid());
        text(0, 39, String("pw ") + cfg.hw.wifiPass);
        text(0, 48, "http://192.168.4.1");
    } else {
        text(0, 30, "OK = turn WiFi on");
    }
}

void drawInfo() {
    char buf[24];
    header("Info");
    text(0, 12, "FW " FW_VERSION);
    snprintf(buf, sizeof(buf), "Frames %lu", (unsigned long)st.frames);
    text(0, 21, buf);
    snprintf(buf, sizeof(buf), "Telem %lu/%lu bad", (unsigned long)st.telem.goodFrames, (unsigned long)st.telem.badFrames);
    text(0, 30, buf);
    snprintf(buf, sizeof(buf), "Heap %lu", (unsigned long)ESP.getFreeHeap());
    text(0, 39, buf);
#if INPUTS_VIA_EXPANDER
    text(0, 48, String("Expander ") + (expOk ? "ok" : "MISSING"));
#else
    text(0, 48, "Inputs: GPIO");
#endif
    snprintf(buf, sizeof(buf), "Up %lus", (unsigned long)(nowMs / 1000));
    text(0, 57, buf);
}

String menuItem(uint8_t i) { return MENU_LABELS[i]; }

String settingItem(uint8_t i) {
    String v = settingValue(i);
    if (editing && i == cursor) v = "<" + v + ">";
    String l = SETTING_LABELS[i];
    while (l.length() + v.length() < 20) l += ' ';
    return l + v;
}

String profileItem(uint8_t i) { return String(i) + " " + profileNames[i] + (i == cfg.profile ? " *" : ""); }

String elrsItem(uint8_t i) {
    using namespace ElrsParams;
    Param p;
    if (!param(elrsIds[i], p)) return "";
    String name = p.name;
    if (p.type == T_COMMAND) return "[" + name.substring(0, 18) + "]";
    const bool edited = editing && i == cursor;
    String v = p.type == T_FOLDER ? String(">") : valueText(p, edited ? elrsEditValue : p.value);
    if (v.length() > 11) v = v.substring(0, 11);
    if (edited) v = "<" + v + ">";
    const int room = 20 - (int)v.length() - 1;
    if ((int)name.length() > room) name = name.substring(0, max(room, 0));
    while (name.length() + v.length() < 20) name += ' ';
    return name + v;
}

void drawElrs() {
    using namespace ElrsParams;
    const Status s = status();
    if (elrsCmdId) {
        Param p;
        param(elrsCmdId, p);
        header(p.name);
        text(0, 14, p.text[0] ? p.text : "...");
        const char* hint = p.value == CMD_ASK_CONFIRM ? "OK=confirm BACK=no"
                         : p.value == CMD_EXECUTING   ? "Running.. BACK=stop"
                                                      : "OK=close";
        text(0, 56, hint);
        return;
    }
    String title = "ExpressLRS";
    Param folder;
    if (elrsFolder && param(elrsFolder, folder)) title = folder.name;
    if (!elrsFolder && s.message[0]) title = s.message;
    if (title.length() > 15) title = title.substring(0, 15);
    if (!s.fieldCount) {
        header(title.c_str());
        text(0, 14, "Waiting for module");
        text(0, 23, "check CRSF wiring");
        return;
    }
    if (!elrsCount) {
        header(title.c_str());
        text(0, 14, String("Loading ") + s.loadedCount + "/" + s.fieldCount);
        return;
    }
    drawList(title.c_str(), elrsCount, elrsItem);
    if (s.loading) text(122, 0, "*");
}

void draw() {
    gfx.fillScreen(COLOR_OFF);
    switch (screen) {
    case HOME: drawHome(); break;
    case MENU: drawList("Menu", MENU_COUNT, menuItem); break;
    case ELRS: drawElrs(); break;
    case CHANNELS: drawChannels(); break;
    case TELEMETRY: drawTelemetry(); break;
    case CALIBRATE: drawCalibrate(); break;
    case SETTINGS: drawList(settingsDirty ? "Settings *" : "Settings", S_COUNT, settingItem); break;
    case PROFILES: drawList("Profiles", PROFILE_COUNT, profileItem); break;
    case REMOTE: drawRemote(); break;
    case INFO: drawInfo(); break;
    }
    present();
}

}  // namespace

void UI::begin() {
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
#if !LED_IS_RGB
    pinMode(PIN_LED, OUTPUT);
#endif
    setLed(0, 0, 30);

#if DISPLAY_TYPE == DISPLAY_SSD1306 || INPUTS_VIA_EXPANDER
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);  // the ST7789 reuses these pins on the DevKit
#endif
#if INPUTS_VIA_EXPANDER
    Wire.beginTransmission(I2C_ADDR_EXPANDER);
    expOk = Wire.endTransmission() == 0;
    if (expOk) {
        Wire.beginTransmission(I2C_ADDR_EXPANDER);
        Wire.write(0xFF);  // all pins as inputs (quasi-bidirectional pull-ups)
        Wire.endTransmission();
    }
#else
    for (uint8_t pin : INPUT_PINS) pinMode(pin, INPUT_PULLUP);
    expOk = true;
#endif

    displayFound = displayBegin();
    if (displayFound) {
        gfx.setTextSize(1);
        gfx.setTextColor(COLOR_ON);
        gfx.fillScreen(COLOR_OFF);
        text(28, 20, "ELRSTx " FW_VERSION);
        text(28, 34, "starting...");
        present();
    }
    cfg = Config::get();
}

void UI::loop() {
    if (millis() - lastTickMs < 20) return;
    lastTickMs = millis();

    st = State::snapshot();
    cfg = Config::get();
    nowMs = millis();

    const uint8_t inputs = readInputs();
    State::auxSwitches.store((inputs >> EXP_SW_FIRST) & 0x0F, std::memory_order_relaxed);
    if (screen == ELRS) {
        ElrsParams::touch();
        elrsCount = ElrsParams::children(elrsFolder, elrsIds, sizeof(elrsIds));
        if (cursor >= elrsCount) cursor = elrsCount ? elrsCount - 1 : 0;
    }
    handleButtons(edges(inputs & 0x0F));
    Calibration::update(st.raw);

    updateAlarms();
    updateIndicators();

    if (displayFound && nowMs - lastDrawMs >= 100) {
        lastDrawMs = nowMs;
        draw();
    }
}

bool UI::displayOk() { return displayFound; }
bool UI::expanderOk() { return expOk; }
