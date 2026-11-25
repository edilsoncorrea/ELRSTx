#include "Commands.h"
#include "Config.h"
#include "ElrsParams.h"
#include "Inputs.h"
#include "Hardware.h"
#include "Pins.h"
#include "Safety.h"
#include "State.h"

bool Commands::monitor = true;

namespace {

const char HELP[] =
    "Commands:\n"
    "  status                  module, link, battery, rate\n"
    "  config                  show settings (JSON)\n"
    "  set <key> <value>       change a setting (RAM only, then 'save')\n"
    "  save                    persist settings to flash\n"
    "  profile [0-4]           list or select a model profile\n"
    "  defaults                load default settings (not saved)\n"
    "  cal                     show calibration and live ADC\n"
    "  cal center|start|stop|cancel\n"
    "  sim [on|off]            stick simulation (t = toggle)\n"
    "  frame                   dump last CRSF frame (f)\n"
    "  wifi on|off             WiFi: home network if saved, else own AP; web + OTA\n"
    "  wifi home <ssid> <pw>   save the home network (http://elrstx.local)\n"
    "  wifi forget | status | scan\n"
    "  mon on|off              status line every second\n"
    "  elrs                    ExpressLRS module parameters (like the ELRS Lua)\n"
    "  elrs set <id> <value>   change a parameter (option text or index)\n"
    "  elrs cmd <id> [confirm|cancel]  run a command (Bind, WiFi...)\n"
    "  elrs reload\n"
    "  hw                      active hardware.json (pins, display, CRSF)\n"
    "  hw set <key> <value>    change one key (saved, applies after reboot)\n"
    "  hw saved | default      pending / built-in hardware.json\n"
    "  hw reset                delete the saved hardware.json\n"
    "  hw learn [up|down|left|right|enter|idle]  5-way button on one ADC pin: store a reading\n"
    "  reboot\n"
    "Keys: name, deadband, invert.<axis>, map.<axis> (ch 1-4), safestart,\n"
    "      linkalarm, linktimeout, batt.ratio, batt.cells, batt.warn,\n"
    "      batt.crit, cal.<axis> (min,center,max), wifipass\n"
    "Axis: roll pitch throttle yaw";

const uint16_t TX_POWER_MW[] = {0, 10, 25, 100, 500, 1000, 2000, 250, 50};

bool parseBool(const String& v, bool& out) {
    if (v == "1" || v == "on" || v == "true") { out = true; return true; }
    if (v == "0" || v == "off" || v == "false") { out = false; return true; }
    return false;
}

bool parseInt(const String& v, long lo, long hi, long& out) {
    if (!v.length()) return false;
    char* end;
    out = strtol(v.c_str(), &end, 10);
    return *end == 0 && out >= lo && out <= hi;
}

String onOff(bool v) { return v ? "on" : "off"; }

String usToString(uint16_t crsf) {
    return String(((int)crsf - CRSF_CHANNEL_VALUE_MID) * 5 / 8 + 1500);
}

String txPower(uint8_t index) {
    return index < sizeof(TX_POWER_MW) / sizeof(TX_POWER_MW[0]) ? String(TX_POWER_MW[index]) + "mW" : "?";
}

String setValue(const String& arg) {
    const int sp = arg.indexOf(' ');
    if (sp < 0) return "ERR usage: set <key> <value>";
    String key = arg.substring(0, sp);
    key.toLowerCase();
    String val = arg.substring(sp + 1);
    val.trim();

    const int dot = key.indexOf('.');
    const String base = dot < 0 ? key : key.substring(0, dot);
    const int axis = dot < 0 ? -1 : Config::axisFromName(key.substring(dot + 1));

    Settings s = Config::get();
    long n;
    bool b;

    if (key == "name") {
        if (!val.length() || val.length() >= sizeof(s.model.name) || val.indexOf('"') >= 0) return "ERR name: 1-12 chars, no quotes";
        strlcpy(s.model.name, val.c_str(), sizeof(s.model.name));
    } else if (key == "deadband") {
        if (!parseInt(val, 0, 200, n)) return "ERR deadband: 0-200";
        s.model.deadband = n;
    } else if (base == "invert" && axis >= 0) {
        if (!parseBool(val, b)) return "ERR value: on/off";
        s.model.invert[axis] = b;
    } else if (base == "map" && axis >= 0) {
        if (!parseInt(val, 1, 4, n)) return "ERR channel: 1-4";
        s.model.channel[axis] = n - 1;
    } else if (key == "safestart") {
        if (!parseBool(val, b)) return "ERR value: on/off";
        s.model.safeStart = b;
    } else if (key == "linkalarm") {
        if (!parseBool(val, b)) return "ERR value: on/off";
        s.model.linkAlarm = b;
    } else if (key == "linktimeout") {
        if (!parseInt(val, 200, 5000, n)) return "ERR linktimeout: 200-5000 ms";
        s.model.linkTimeoutMs = n;
    } else if (key == "batt.ratio") {
        if (!parseInt(val, 1000, 20000, n)) return "ERR batt.ratio: 1000-20000 ((R1+R2)/R2 x1000)";
        s.hw.battRatioX1000 = n;
    } else if (key == "batt.cells") {
        if (!parseInt(val, 1, 6, n)) return "ERR batt.cells: 1-6";
        s.hw.battCells = n;
    } else if (key == "batt.warn") {
        if (!parseInt(val, 3000, 4200, n)) return "ERR batt.warn: 3000-4200 mV/cell";
        s.hw.battWarnMv = n;
    } else if (key == "batt.crit") {
        if (!parseInt(val, 3000, 4200, n)) return "ERR batt.crit: 3000-4200 mV/cell";
        s.hw.battCritMv = n;
    } else if (base == "cal" && axis >= 0) {
        long lo, mid, hi;
        if (sscanf(val.c_str(), "%ld,%ld,%ld", &lo, &mid, &hi) != 3 || lo < 0 || lo >= mid || mid >= hi || hi > 4095)
            return "ERR cal: min,center,max with 0 <= min < center < max <= 4095";
        s.hw.cal[axis] = {(uint16_t)lo, (uint16_t)mid, (uint16_t)hi};
    } else if (key == "wifipass") {
        if (val.length() < 8 || val.length() >= sizeof(s.hw.wifiPass)) return "ERR wifipass: 8-16 chars";
        strlcpy(s.hw.wifiPass, val.c_str(), sizeof(s.hw.wifiPass));
    } else {
        return "ERR unknown key '" + key + "'";
    }
    Config::set(s);
    return "OK " + key + " = " + val + " (use 'save' to persist)";
}

String configJson() {
    const Settings s = Config::get();
    String j = "{\"profile\":" + String(s.profile) + ",\"name\":\"" + s.model.name + "\"";
    j += ",\"deadband\":" + String(s.model.deadband);
    j += ",\"safestart\":" + String(s.model.safeStart ? "true" : "false");
    j += ",\"linkalarm\":" + String(s.model.linkAlarm ? "true" : "false");
    j += ",\"linktimeout\":" + String(s.model.linkTimeoutMs);
    j += ",\"simulation\":" + String(s.model.simulation ? "true" : "false");
    j += ",\"axes\":{";
    for (uint8_t a = 0; a < AXIS_COUNT; a++) {
        const AxisCal& c = s.hw.cal[a];
        if (a) j += ",";
        j += "\"" + String(Config::axisName(a)) + "\":{\"ch\":" + String(s.model.channel[a] + 1) +
             ",\"invert\":" + (s.model.invert[a] ? "true" : "false") + ",\"cal\":[" + c.min + "," + c.center + "," +
             c.max + "]}";
    }
    j += "},\"battery\":{\"ratio\":" + String(s.hw.battRatioX1000) + ",\"cells\":" + s.hw.battCells +
         ",\"warn\":" + s.hw.battWarnMv + ",\"crit\":" + s.hw.battCritMv + "}}";
    return j;
}

String calibrate(const String& arg) {
    const RcState st = State::snapshot();
    if (arg == "center") {
        Calibration::captureCenter(st.raw);
        return "OK centers captured. Now 'cal start', move every stick to its limits, then 'cal stop'";
    }
    if (arg == "start") {
        Calibration::startRange(st.raw);
        return "OK move every stick to all its limits, then 'cal stop'";
    }
    if (arg == "stop") {
        String err;
        return Calibration::finish(err) ? "OK calibration saved" : "ERR " + err;
    }
    if (arg == "cancel") {
        Calibration::cancel();
        return "OK calibration cancelled";
    }
    if (arg.length()) return "ERR usage: cal [center|start|stop|cancel]";

    const Settings s = Config::get();
    String o = Calibration::active() ? "Calibrating (range capture running)\n" : "";
    o += "axis      raw   min center  max\n";
    for (uint8_t a = 0; a < AXIS_COUNT; a++) {
        const AxisCal c = Calibration::active() ? Calibration::range(a) : s.hw.cal[a];
        char line[48];
        snprintf(line, sizeof(line), "%-8s %4u  %4u  %4u  %4u\n", Config::axisName(a), st.raw[a], c.min,
                 Calibration::active() ? s.hw.cal[a].center : c.center, c.max);
        o += line;
    }
    return o;
}

const char* commandStepText(int16_t step) {
    switch (step) {
    case ElrsParams::CMD_EXECUTING: return "running";
    case ElrsParams::CMD_ASK_CONFIRM: return "needs confirm";
    default: return "";
    }
}

void elrsTree(String& o, uint8_t folder, uint8_t depth) {
    using namespace ElrsParams;
    uint8_t ids[MAX_PARAMS];
    const uint8_t n = children(folder, ids, sizeof(ids));
    for (uint8_t i = 0; i < n; i++) {
        Param p;
        if (!param(ids[i], p)) continue;
        String line = String(p.id);
        while (line.length() < 4) line += ' ';
        for (uint8_t d = 0; d < depth; d++) line += "  ";
        switch (p.type) {
        case T_FOLDER:
            o += line + p.name + "/\n";
            elrsTree(o, p.id, depth + 1);
            continue;
        case T_COMMAND:
            line += String("[") + p.name + "]";
            if (commandStepText(p.value)[0]) line += String("  ") + commandStepText(p.value);
            if (p.text[0]) line += String("  ") + p.text;
            break;
        case T_SELECT: {
            line += p.name;
            while (line.length() < 30) line += ' ';
            String opts = p.text;
            opts.replace(";", ", ");
            line += "= " + valueText(p, p.value) + "   (" + opts + ")";
            break;
        }
        case T_UINT8:
        case T_INT8:
        case T_UINT16:
        case T_INT16:
            line += p.name;
            while (line.length() < 30) line += ' ';
            line += "= " + valueText(p, p.value) + "   (" + p.min + ".." + p.max + ")";
            break;
        default:
            line += String(p.name) + ": " + p.text;
            break;
        }
        o += line + "\n";
    }
}

String elrsCommand(const String& arg) {
    using namespace ElrsParams;
    touch();
    const Status s = status();
    const int sp = arg.indexOf(' ');
    String sub = sp < 0 ? arg : arg.substring(0, sp);
    sub.toLowerCase();
    String rest = sp < 0 ? "" : arg.substring(sp + 1);
    rest.trim();

    if (sub == "" || sub == "list") {
        if (!s.fieldCount) return "Waiting for the ELRS module (no device info yet). Check the CRSF wiring and retry 'elrs'.";
        String o = String("ExpressLRS: ") + s.device + " (" + s.fieldCount + " params)";
        if (s.statusValid) {
            o += String("  bad/good ") + s.badPackets + "/" + s.goodPackets;
            if (s.message[0]) o += String("  [") + s.message + "]";
        }
        o += "\n";
        if (s.loading) o += String("Loading ") + s.loadedCount + "/" + s.fieldCount + ", run 'elrs' again shortly\n";
        elrsTree(o, 0, 0);
        return o;
    }
    if (sub == "reload") {
        reload();
        return "OK reloading ExpressLRS parameters";
    }

    const int sp2 = rest.indexOf(' ');
    long id;
    if (!parseInt(sp2 < 0 ? rest : rest.substring(0, sp2), 1, MAX_PARAMS - 1, id)) return "ERR usage: elrs set|cmd <id> ...";
    String value = sp2 < 0 ? "" : rest.substring(sp2 + 1);
    value.trim();
    Param p;
    if (!param(id, p)) return "ERR unknown parameter id (run 'elrs' first)";

    if (sub == "set") {
        long v;
        if (p.type == T_SELECT) {
            if (!parseInt(value, p.min, p.max, v)) v = findOption(p, value);
            if (v < 0 || valueText(p, v).length() <= strlen(p.units)) {
                String opts = p.text;
                opts.replace(";", ", ");
                return "ERR options: " + opts;
            }
        } else if (p.type <= T_INT16) {
            if (!parseInt(value, p.min, p.max, v)) return String("ERR range ") + p.min + ".." + p.max;
        } else {
            return "ERR this parameter cannot be set";
        }
        ElrsParams::setValue(id, v);
        return String("OK ") + p.name + " = " + valueText(p, v);
    }
    if (sub == "cmd") {
        if (p.type != T_COMMAND) return "ERR not a command";
        value.toLowerCase();
        const CmdStep step = value == "confirm" ? CMD_CONFIRM : value == "cancel" ? CMD_CANCEL : CMD_CLICK;
        command(id, step);
        return String("OK sent to ") + p.name + ", run 'elrs' to see the result";
    }
    return "ERR usage: elrs [list|reload|set <id> <value>|cmd <id> [confirm|cancel]]";
}

String wifiState() {
    switch (Remote::mode()) {
    case Remote::Mode::Off: return "WiFi off";
    case Remote::Mode::Connecting: return "WiFi connecting to '" + Remote::homeNetwork() + "'...";
    case Remote::Mode::Home:
        return "WiFi on '" + Remote::ssid() + "', IP " + Remote::ip() + " -> " + Remote::url() + " (or http://" +
               Remote::ip() + ")";
    case Remote::Mode::Ap:
        return "WiFi AP '" + Remote::apName() + "' pass '" + Config::get().hw.wifiPass + "' -> " + Remote::url();
    }
    return "";
}

String wifiCommand(const String& arg) {
    const int sp = arg.indexOf(' ');
    String sub = sp < 0 ? arg : arg.substring(0, sp);
    String rest = sp < 0 ? "" : arg.substring(sp + 1);
    rest.trim();
    bool b;
    if (!sub.length() || sub == "status") {
        const String home = Remote::homeNetwork();
        String o = wifiState() + "\nHome network: " + (home.length() ? "'" + home + "'" : String("none (wifi home <ssid> <password>)"));
        if (Remote::lastFailure().length()) o += "\nLast home connection failed: " + Remote::lastFailure();
        return o;
    }
    if (sub == "scan") return Remote::scan();
    if (parseBool(sub, b)) {
        Remote::setWifi(b);
        return "OK " + wifiState();
    }
    if (sub == "home") {
        // Password is the last word, so SSIDs with spaces work: wifi home My Network secret123
        const int last = rest.lastIndexOf(' ');
        if (last < 0) return "ERR usage: wifi home <ssid> <password>";
        String ssid = rest.substring(0, last);
        ssid.trim();
        const String pass = rest.substring(last + 1);
        if (!ssid.length() || ssid.length() > 32 || pass.length() < 8 || pass.length() > 63)
            return "ERR ssid 1-32 chars, password 8-63 chars";
        if (!Remote::setHome(ssid, pass)) return "ERR flash write failed";
        return "OK home network '" + ssid + "' saved; used next time WiFi is turned on";
    }
    if (sub == "forget") {
        if (!Remote::setHome("", "")) return "ERR flash write failed";
        return "OK home network forgotten; WiFi will open its own AP";
    }
    return "ERR usage: wifi [on|off|status] | wifi home <ssid> <password> | wifi forget";
}

// Resistor-ladder menu buttons: hold a direction and store its reading
String learnButton(const String& name) {
    const HardwareConfig& hw = Hardware::get();
    if (hw.buttons != ButtonMode::Adc) return "ERR menu buttons are not on the ADC: hw set buttons adc, then reboot";

    // Average a few readings; the RC task refreshes the value every cycle
    uint32_t sum = 0;
    for (uint8_t i = 0; i < 8; i++) {
        sum += State::buttonAdc.load(std::memory_order_relaxed);
        delay(10);
    }
    const uint16_t now = sum / 8;

    if (!name.length()) {
        String o = "Ladder on GPIO" + String(hw.btnAdc) + ", reading now: " + now + "\nLearned:";
        for (uint8_t i = 0; i < ADC_BTN_COUNT; i++) {
            o += String("  ") + Hardware::adcButtonName((AdcButton)i) + " " + hw.btnAdcValues[i];
        }
        return o + "\nHold a direction and send: hw learn up|down|left|right|enter (idle = released)";
    }
    for (uint8_t i = 0; i < ADC_BTN_COUNT; i++) {
        if (name == Hardware::adcButtonName((AdcButton)i)) {
            String err;
            if (!Hardware::setButtonValue((AdcButton)i, now, err)) return "ERR " + err;
            return "OK " + name + " = " + now + " (saved, already active)";
        }
    }
    return "ERR direction: up, down, left, right, enter or idle";
}

String hwCommand(const String& arg) {
    const int sp = arg.indexOf(' ');
    String sub = sp < 0 ? arg : arg.substring(0, sp);
    String rest = sp < 0 ? "" : arg.substring(sp + 1);
    rest.trim();
    if (!sub.length()) {
        return String("hardware.json (") + (Hardware::usingOverride() ? "saved" : "built-in") + "):\n" + Hardware::json();
    }
    if (sub == "saved") return Hardware::savedJson();
    if (sub == "default") return Hardware::defaultJson();
    if (sub == "learn") return learnButton(rest);
    if (sub == "reset") {
        Hardware::reset();
        return "OK saved hardware.json deleted, built-in applies after 'reboot'";
    }
    if (sub == "set") {
        const int s2 = rest.indexOf(' ');
        if (s2 < 0) return "ERR usage: hw set <key> <value>";
        String err;
        String value = rest.substring(s2 + 1);
        value.trim();
        if (!Hardware::set(rest.substring(0, s2), value, err)) return "ERR " + err;
        return "OK saved, applies after 'reboot'";
    }
    return "ERR usage: hw [set <key> <value> | saved | default | reset | learn]";
}

String frameText() {
    const RcState st = State::snapshot();
    String o = "Frame:";
    char hex[4];
    for (uint8_t i = 0; i < CRSF_RC_FRAME_SIZE; i++) {
        snprintf(hex, sizeof(hex), " %02X", st.lastFrame[i]);
        o += hex;
    }
    o += "\nCRC " + String(CRSF::crc8(&st.lastFrame[2], 23) == st.lastFrame[25] ? "ok" : "BAD") + "\n";
    for (uint8_t i = 0; i < 8; i++) {
        o += "CH" + String(i + 1) + " " + st.channels[i] + " (" + usToString(st.channels[i]) + "us)";
        o += (i % 2) ? "\n" : "   ";
    }
    return o;
}

}  // namespace

String Commands::statusText() {
    const RcState st = State::snapshot();
    const uint32_t now = millis();
    const Settings s = Config::get();
    const CRSFTelemetry& t = st.telem;
    String o;

    o += "Profile " + String(s.profile) + ": " + s.model.name + (s.model.simulation ? "  [SIMULATION]" : "") + "\n";

    o += "ELRS module: ";
    if (State::moduleAlive(st, now)) o += t.deviceName[0] ? String(t.deviceName) : String("responding");
    else if (Hardware::get().halfDuplex()) o += "NOT RESPONDING (one wire: check the wire to the module's CRSF pin, GND, module serial_rx = serial_tx)";
    else o += "NOT RESPONDING (two wires: check RX/TX crossed, GND, module serial_rx != serial_tx)";
    o += "\n";

    o += "Packet rate: " + String(1000000UL / st.periodUs) + " Hz" +
         (State::syncActive(st, now) ? " (synced to module)" : " (default, no sync)") + "\n";

    if (State::linkUp(st, now, s.model.linkTimeoutMs)) {
        o += "Link: LQ " + String(t.uplinkLq) + "%  RSSI -" + t.uplinkRssi1 + "/-" + t.uplinkRssi2 + " dBm  SNR " +
             t.uplinkSnr + "  " + txPower(t.txPower) + "\n";
        o += "Downlink: LQ " + String(t.downlinkLq) + "%  RSSI -" + t.downlinkRssi + " dBm\n";
    } else {
        o += "Link: no receiver connected\n";
    }

    if (st.txBatteryMv > 1000) {
        const uint16_t cell = st.txBatteryMv / s.hw.battCells;
        o += "TX battery: " + String(st.txBatteryMv / 1000.0f, 2) + " V (" + Battery::percent(cell) + "%, " +
             s.hw.battCells + "S)\n";
    } else {
        o += "TX battery: not connected\n";
    }

    if (State::fresh(t.lastBatteryMs, now, 5000)) {
        o += "Model battery: " + String(t.batteryDv / 10.0f, 1) + " V  " + String(t.currentDa / 10.0f, 1) + " A  " +
             t.capacityMah + " mAh  " + t.remainingPct + "%\n";
    }
    if (t.flightMode[0]) o += "Flight mode: " + String(t.flightMode) + "\n";
    if (State::fresh(t.lastGpsMs, now, 5000)) o += "GPS: " + String(t.satellites) + " sats\n";

    o += "Channels:";
    for (uint8_t i = 0; i < 8; i++) o += " " + String(st.channels[i]);
    o += "\nFrames sent: " + String(st.frames) + "  telemetry ok/bad: " + t.goodFrames + "/" + t.badFrames + "  RX bytes: " + t.rxBytes + "\n";
    if (st.safeStartHold) o += "SAFE START: move throttle to minimum and switch AUX1 (arm) off\n";
    const HardwareConfig& hw = Hardware::get();
    o += "CRSF: " + (hw.halfDuplex() ? "one wire GPIO" + String(hw.crsfRx) : "RX GPIO" + String(hw.crsfRx) + " TX GPIO" + hw.crsfTx) +
         "  " + hw.crsfBaud + " baud  (hardware.json " + (Hardware::usingOverride() ? "saved" : "built-in") + ")\n";
    o += wifiState();
    return o;
}

String Commands::monitorLine() {
    const RcState st = State::snapshot();
    const uint32_t now = millis();
    const Settings s = Config::get();
    char buf[160];
    snprintf(buf, sizeof(buf), "[%s] %s | %luHz | LQ %u | CH %u %u %u %u | AUX %u %u %u %u | TX %.2fV%s",
             s.model.simulation ? "SIM" : "HW",
             State::moduleAlive(st, now) ? "module ok" : "NO MODULE",
             1000000UL / st.periodUs,
             State::linkUp(st, now, s.model.linkTimeoutMs) ? st.telem.uplinkLq : 0,
             st.channels[0], st.channels[1], st.channels[2], st.channels[3],
             st.channels[4], st.channels[5], st.channels[6], st.channels[7],
             st.txBatteryMv / 1000.0f, st.safeStartHold ? " | SAFE START" : "");
    return buf;
}

String Commands::execute(const String& input) {
    String line = input;
    line.trim();
    if (line == "t") line = "sim";
    else if (line == "f") line = "frame";
    else if (line == "h" || line == "?") line = "help";

    const int sp = line.indexOf(' ');
    String cmd = sp < 0 ? line : line.substring(0, sp);
    cmd.toLowerCase();
    String arg = sp < 0 ? "" : line.substring(sp + 1);
    arg.trim();

    if (cmd == "help") return HELP;
    if (cmd == "status") return statusText();
    if (cmd == "config") return configJson();
    if (cmd == "set") return setValue(arg);
    if (cmd == "save") return Config::save() ? "OK saved" : "ERR flash write failed";
    if (cmd == "defaults") {
        Config::resetDefaults();
        return "OK defaults loaded (use 'save' to persist)";
    }
    if (cmd == "profile") {
        if (!arg.length()) {
            const uint8_t active = Config::get().profile;
            String o;
            for (uint8_t i = 0; i < PROFILE_COUNT; i++) o += String(i) + ": " + Config::profileName(i) + (i == active ? "  *\n" : "\n");
            return o;
        }
        long n;
        if (!parseInt(arg, 0, PROFILE_COUNT - 1, n)) return "ERR profile: 0-4";
        Config::selectProfile(n);
        return "OK profile " + String(n) + ": " + Config::get().model.name;
    }
    if (cmd == "cal") return calibrate(arg);
    if (cmd == "sim") {
        Settings s = Config::get();
        bool b;
        s.model.simulation = parseBool(arg, b) ? b : !s.model.simulation;
        Config::set(s);
        return "OK simulation " + onOff(s.model.simulation);
    }
    if (cmd == "frame") return frameText();
    if (cmd == "elrs") return elrsCommand(arg);
    if (cmd == "wifi") return wifiCommand(arg);
    if (cmd == "mon") {
        bool b;
        if (!parseBool(arg, b)) return "ERR usage: mon on|off";
        monitor = b;
        return "OK monitor " + onOff(b);
    }
    if (cmd == "hw") return hwCommand(arg);
    if (cmd == "reboot") {
        Serial.println("Rebooting...");
        delay(100);
        ESP.restart();
    }
    return "ERR unknown command '" + cmd + "', try 'help'";
}
