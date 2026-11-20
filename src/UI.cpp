#include "UI.h"
#include <Wire.h>
#include "Commands.h"
#include "Config.h"
#include "Display.h"
#include "Inputs.h"
#include "Pins.h"
#include "Safety.h"
#include "State.h"
#include "ElrsParams.h"

namespace {

bool displayFound = false;
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

// ---------- views (what to show; Display::* decides how) ----------

void copy(char* dst, size_t size, const String& s) { strlcpy(dst, s.c_str(), size); }

void showHome() {
    using namespace Display;
    String title = cfg.model.name;
    if (cfg.model.simulation) title += " SIM";

    HomeView v = {};
    v.title = title.c_str();
    if (st.txBatteryMv > 1000) {
        v.txBatteryMv = st.txBatteryMv;
        const uint16_t cellMv = st.txBatteryMv / cfg.hw.battCells;
        v.txBatteryLevel = cellMv < cfg.hw.battCritMv ? Level::Crit : cellMv < cfg.hw.battWarnMv ? Level::Warn : Level::None;
    }
    v.moduleAlive = State::moduleAlive(st, nowMs);
    v.deviceName = st.telem.deviceName;
    v.link = linkUp();
    v.lq = st.telem.uplinkLq;
    v.rssi = st.telem.uplinkRssi1;
    v.rateHz = 1000000UL / st.periodUs;
    v.synced = State::syncActive(st, nowMs);
    v.rxBatteryValid = State::fresh(st.telem.lastBatteryMs, nowMs, 5000);
    v.rxBatteryDv = st.telem.batteryDv;
    v.alarm = alarmLevel == ALARM_CRIT ? Level::Crit : alarmLevel == ALARM_WARN ? Level::Warn : Level::None;
    v.alarmText = alarmText;
    v.flightMode = st.telem.flightMode;
    for (uint8_t a = 0; a < AXIS_COUNT; a++) {
        v.axisLetter[a] = toupper(Config::axisName(a)[0]);
        v.stick[a] = Display::channelFraction(st.channels[cfg.model.channel[a]]);
    }
    for (uint8_t i = 0; i < AUX_COUNT; i++) v.aux[i] = st.channels[AUX_FIRST_CHANNEL + i] > CRSF_CHANNEL_VALUE_MID;
    Display::home(v);
}

void showList(const char* title, uint8_t count, void (*row)(uint8_t, Display::Row&), bool busy = false) {
    Display::ListView v = {title, count, cursor, editing, busy, row};
    Display::list(v);
}

void menuRow(uint8_t i, Display::Row& r) { copy(r.label, sizeof(r.label), MENU_LABELS[i]); }

void settingRow(uint8_t i, Display::Row& r) {
    copy(r.label, sizeof(r.label), SETTING_LABELS[i]);
    copy(r.value, sizeof(r.value), settingValue(i));
}

void profileRow(uint8_t i, Display::Row& r) {
    copy(r.label, sizeof(r.label), String(i) + " " + profileNames[i] + (i == cfg.profile ? " *" : ""));
}

void elrsRow(uint8_t i, Display::Row& r) {
    using namespace ElrsParams;
    Param p;
    if (!param(elrsIds[i], p)) return;
    copy(r.label, sizeof(r.label), p.name);
    r.folder = p.type == T_FOLDER;
    r.command = p.type == T_COMMAND;
    if (!r.folder && !r.command) {
        const bool edited = editing && i == cursor;
        copy(r.value, sizeof(r.value), valueText(p, edited ? elrsEditValue : p.value));
    }
}

void showMessage(const char* title, std::initializer_list<const char*> lines, const char* footer = nullptr,
                 int8_t highlight = -1) {
    Display::MessageView v = {};
    v.title = title;
    uint8_t n = 0;
    for (const char* l : lines) {
        if (n < Display::MESSAGE_LINES) v.line[n++] = l;
    }
    v.highlight = highlight;
    v.footer = footer;
    Display::message(v);
}

void showElrs() {
    using namespace ElrsParams;
    const Status s = status();
    if (elrsCmdId) {
        Param p;
        param(elrsCmdId, p);
        const char* hint = p.value == CMD_ASK_CONFIRM ? "OK=yes BACK=no"
                         : p.value == CMD_EXECUTING   ? "BACK=stop"
                                                      : "OK=close";
        showMessage(p.name, {p.text[0] ? p.text : "..."}, hint);
        return;
    }
    String title = "ExpressLRS";
    Param folder;
    if (elrsFolder && param(elrsFolder, folder)) title = folder.name;
    if (!elrsFolder && s.message[0]) title = s.message;
    if (title.length() > 15) title = title.substring(0, 15);
    if (!s.fieldCount) {
        showMessage(title.c_str(), {"Waiting for module", "check CRSF wiring"});
        return;
    }
    if (!elrsCount) {
        const String loading = String("Loading ") + s.loadedCount + "/" + s.fieldCount;
        showMessage(title.c_str(), {loading.c_str()});
        return;
    }
    showList(title.c_str(), elrsCount, elrsRow, s.loading);
}

void showTelemetry() {
    const CRSFTelemetry& t = st.telem;
    Display::TelemetryView v = {};
    v.link = linkUp();
    v.snr = t.uplinkSnr;
    v.rssi1 = t.uplinkRssi1;
    v.rssi2 = t.uplinkRssi2;
    v.lq = t.uplinkLq;
    v.downRssi = t.downlinkRssi;
    v.downLq = t.downlinkLq;
    v.battery = State::fresh(t.lastBatteryMs, nowMs, 5000);
    v.batteryDv = t.batteryDv;
    v.currentDa = t.currentDa;
    v.capacityMah = t.capacityMah;
    v.attitude = State::fresh(t.lastAttitudeMs, nowMs, 5000);
    v.pitchDeg = t.pitch * 0.0057296f;  // 1/10000 rad -> degrees
    v.rollDeg = t.roll * 0.0057296f;
    v.yawDeg = t.yaw * 0.0057296f;
    v.gps = State::fresh(t.lastGpsMs, nowMs, 5000);
    v.satellites = t.satellites;
    v.altitudeM = (int)t.altitude - 1000;
    v.flightMode = t.flightMode;
    Display::telemetry(v);
}

void showCalibrate() {
    if (calStep == 0) {
        showMessage("Calibrate", {"1. Center sticks", "   then press OK"}, "BACK = cancel");
        return;
    }
    if (calStep == 1) {
        char axis[AXIS_COUNT][20];
        for (uint8_t a = 0; a < AXIS_COUNT; a++) {
            const AxisCal c = Calibration::range(a);
            snprintf(axis[a], sizeof(axis[a]), "%c %4u %4u %4u", toupper(Config::axisName(a)[0]), c.min, st.raw[a], c.max);
        }
        showMessage("Calibrate", {"2. Move to limits", "   then press OK", axis[0], axis[1], axis[2], axis[3]});
        return;
    }
    showMessage("Calibrate", {calResult.c_str()}, "Press any key");
}

void showRemote() {
    const String state = String("WiFi AP: ") + (Remote::wifiActive() ? "ON" : "off");
    if (Remote::wifiActive()) {
        const String ssid = Remote::ssid();
        const String pass = String("pw ") + cfg.hw.wifiPass;
        showMessage("WiFi / OTA", {state.c_str(), nullptr, ssid.c_str(), pass.c_str(), "http://192.168.4.1"}, nullptr, 0);
    } else {
        showMessage("WiFi / OTA", {state.c_str(), nullptr, "OK = turn WiFi on"}, nullptr, 0);
    }
}

void showInfo() {
    char frames[24], telem[24], heap[24], up[24];
    snprintf(frames, sizeof(frames), "Frames %lu", (unsigned long)st.frames);
    snprintf(telem, sizeof(telem), "Telem %lu/%lu bad", (unsigned long)st.telem.goodFrames, (unsigned long)st.telem.badFrames);
    snprintf(heap, sizeof(heap), "Heap %lu", (unsigned long)ESP.getFreeHeap());
    snprintf(up, sizeof(up), "Up %lus", (unsigned long)(nowMs / 1000));
#if INPUTS_VIA_EXPANDER
    const char* inputs = expOk ? "Expander ok" : "Expander MISSING";
#else
    const char* inputs = "Inputs: GPIO";
#endif
    showMessage("Info", {"FW " FW_VERSION, frames, telem, heap, inputs, up});
}

void draw() {
    switch (screen) {
    case HOME: showHome(); break;
    case MENU: showList("Menu", MENU_COUNT, menuRow); break;
    case ELRS: showElrs(); break;
    case CHANNELS: Display::channels(st.channels, CRSF_MAX_CHANNELS); break;
    case TELEMETRY: showTelemetry(); break;
    case CALIBRATE: showCalibrate(); break;
    case SETTINGS: showList(settingsDirty ? "Settings *" : "Settings", S_COUNT, settingRow); break;
    case PROFILES: showList("Profiles", PROFILE_COUNT, profileRow); break;
    case REMOTE: showRemote(); break;
    case INFO: showInfo(); break;
    }
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

    displayFound = Display::begin();
    if (displayFound) Display::splash(FW_VERSION);
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
