#include "UI.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Commands.h"
#include "Config.h"
#include "Inputs.h"
#include "Pins.h"
#include "Safety.h"
#include "State.h"

namespace {

Adafruit_SSD1306 oled(128, 64, &Wire, -1, 400000UL, 400000UL);
bool oledOk = false;
bool expOk = false;

enum Screen : uint8_t { HOME, MENU, CHANNELS, TELEMETRY, CALIBRATE, SETTINGS, PROFILES, REMOTE, INFO };
const char* const MENU_LABELS[] = {"Channels", "Telemetry", "Calibrate", "Settings", "Profiles", "WiFi/BLE/OTA", "Info"};
const Screen MENU_TARGETS[] = {CHANNELS, TELEMETRY, CALIBRATE, SETTINGS, PROFILES, REMOTE, INFO};
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
    const bool remote = Remote::wifiActive() || Remote::bleActive();
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
}

void moveCursor(bool up, bool down, uint8_t count) {
    if (up) cursor = (cursor + count - 1) % count;
    if (down) cursor = (cursor + 1) % count;
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
        moveCursor(up, down, 2);
        if (ok) {
            if (cursor == 0) Remote::setWifi(!Remote::wifiActive());
            else Remote::setBle(!Remote::bleActive());
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
    oled.setCursor(x, y);
    oled.print(s);
}

void header(const char* title) {
    text(0, 0, title);
    oled.drawFastHLine(0, 9, 128, SSD1306_WHITE);
}

float channelFraction(uint16_t v) {
    return constrain((v - CRSF_CHANNEL_VALUE_MIN) / float(CRSF_CHANNEL_VALUE_MAX - CRSF_CHANNEL_VALUE_MIN), 0.0f, 1.0f);
}

void bar(int16_t x, int16_t y, int16_t w, int16_t h, float f) {
    oled.drawRect(x, y, w, h, SSD1306_WHITE);
    oled.fillRect(x + 1, y + 1, (int16_t)((w - 2) * f), h - 2, SSD1306_WHITE);
}

void drawList(const char* title, uint8_t count, String (*item)(uint8_t)) {
    header(title);
    if (cursor < scroll) scroll = cursor;
    if (cursor >= scroll + 6) scroll = cursor - 5;
    for (uint8_t row = 0; row < 6 && scroll + row < count; row++) {
        const uint8_t i = scroll + row;
        const int16_t y = 12 + row * 9;
        if (i == cursor) {
            oled.fillRect(0, y - 1, 128, 9, SSD1306_WHITE);
            oled.setTextColor(SSD1306_BLACK);
        }
        text(2, y, item(i));
        oled.setTextColor(SSD1306_WHITE);
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
    oled.drawFastHLine(0, 9, 128, SSD1306_WHITE);

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
        oled.fillRect(0, 38, 128, 10, SSD1306_WHITE);
        oled.setTextColor(SSD1306_BLACK);
        text(2, 39, alarmText);
        oled.setTextColor(SSD1306_WHITE);
    } else if (st.telem.flightMode[0]) {
        text(0, 39, st.telem.flightMode);
    }

    for (uint8_t a = 0; a < AXIS_COUNT; a++) bar(a * 32, 50, 30, 6, channelFraction(st.channels[cfg.model.channel[a]]));
    for (uint8_t i = 0; i < AUX_COUNT; i++) {
        const bool on = st.channels[AUX_FIRST_CHANNEL + i] > CRSF_CHANNEL_VALUE_MID;
        if (on) oled.fillRect(i * 32 + 11, 58, 8, 6, SSD1306_WHITE);
        else oled.drawRect(i * 32 + 11, 58, 8, 6, SSD1306_WHITE);
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
    header("WiFi / BLE / OTA");
    String w = String("WiFi AP: ") + (Remote::wifiActive() ? "ON" : "off");
    String b = String("BLE:     ") + (Remote::bleActive() ? (Remote::bleConnected() ? "CONNECTED" : "ON") : "off");
    for (uint8_t i = 0; i < 2; i++) {
        const int16_t y = 12 + i * 9;
        if (i == cursor) {
            oled.fillRect(0, y - 1, 128, 9, SSD1306_WHITE);
            oled.setTextColor(SSD1306_BLACK);
        }
        text(2, y, i == 0 ? w : b);
        oled.setTextColor(SSD1306_WHITE);
    }
    if (Remote::wifiActive()) {
        text(0, 34, Remote::ssid());
        text(0, 43, String("pw ") + cfg.hw.wifiPass);
        text(0, 52, "http://192.168.4.1");
    } else if (Remote::bleActive()) {
        text(0, 34, "Name: ELRSTx");
        text(0, 43, "Nordic UART service");
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

void draw() {
    oled.clearDisplay();
    switch (screen) {
    case HOME: drawHome(); break;
    case MENU: drawList("Menu", MENU_COUNT, menuItem); break;
    case CHANNELS: drawChannels(); break;
    case TELEMETRY: drawTelemetry(); break;
    case CALIBRATE: drawCalibrate(); break;
    case SETTINGS: drawList(settingsDirty ? "Settings *" : "Settings", S_COUNT, settingItem); break;
    case PROFILES: drawList("Profiles", PROFILE_COUNT, profileItem); break;
    case REMOTE: drawRemote(); break;
    case INFO: drawInfo(); break;
    }
    oled.display();
}

}  // namespace

void UI::begin() {
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
#if !LED_IS_RGB
    pinMode(PIN_LED, OUTPUT);
#endif
    setLed(0, 0, 30);

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
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

    Wire.beginTransmission(I2C_ADDR_OLED);
    oledOk = Wire.endTransmission() == 0 && oled.begin(SSD1306_SWITCHCAPVCC, I2C_ADDR_OLED, true, false);
    if (oledOk) {
        oled.setTextSize(1);
        oled.setTextColor(SSD1306_WHITE);
        oled.clearDisplay();
        text(28, 20, "ELRSTx " FW_VERSION);
        text(28, 34, "starting...");
        oled.display();
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
    handleButtons(edges(inputs & 0x0F));
    Calibration::update(st.raw);

    updateAlarms();
    updateIndicators();

    if (oledOk && nowMs - lastDrawMs >= 100) {
        lastDrawMs = nowMs;
        draw();
    }
}

bool UI::displayOk() { return oledOk; }
bool UI::expanderOk() { return expOk; }
