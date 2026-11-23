#include "Hardware.h"
#include <ArduinoJson.h>
#include <Preferences.h>

// hardware/<board>.json, linked in by board_build.embed_txtfiles (platformio.ini)
#if CONFIG_IDF_TARGET_ESP32C3
extern const char HW_DEFAULT[] asm("_binary_hardware_esp32c3_json_start");
#else
extern const char HW_DEFAULT[] asm("_binary_hardware_esp32dev_json_start");
#endif

namespace {

constexpr const char* NVS_NS = "hwcfg";
constexpr const char* NVS_KEY = "json";

HardwareConfig active;
String activeJson;
bool fromOverride = false;
bool ignored = false;

const char* const DISPLAY_NAMES[] = {"none", "ssd1306", "st7789"};
const char* const INPUT_NAMES[] = {"gpio", "pcf8574"};
const char* const BUTTON_MODES[] = {"inputs", "adc", "encoder"};
const char* const ADC_BUTTON_NAMES[] = {"up", "down", "left", "right", "enter", "idle"};

// ---------- pin rules ----------

bool reservedPin(int p) {
#if CONFIG_IDF_TARGET_ESP32C3
    return (p >= 12 && p <= 17) || p == 18 || p == 19;  // SPI flash, USB D-/D+
#else
    return p >= 6 && p <= 11;                            // SPI flash
#endif
}

bool checkPin(const char* name, int p, bool output, String& err) {
    if (p < 0) return true;
    if (!GPIO_IS_VALID_GPIO(p) || reservedPin(p)) {
        err = String(name) + ": GPIO" + p + " cannot be used";
        return false;
    }
    if (output && !GPIO_IS_VALID_OUTPUT_GPIO(p)) {
        err = String(name) + ": GPIO" + p + " is input only";
        return false;
    }
    return true;
}

// WiFi disables ADC2, so analog inputs must be on ADC1
bool checkAdc(const char* name, int p, String& err) {
    if (p < 0) return true;
    if (!checkPin(name, p, false, err)) return false;
    const int8_t ch = digitalPinToAnalogChannel(p);
    if (ch < 0 || ch >= SOC_ADC_MAX_CHANNEL_NUM) {
        err = String(name) + ": GPIO" + p + " is not an ADC1 pin";
        return false;
    }
    return true;
}

struct Use {
    const char* name;
    int pin;
};

bool validate(const HardwareConfig& c, String& err) {
    static const char* const JOY[] = {"joy_roll", "joy_pitch", "joy_throttle", "joy_yaw"};
    static const char* const BTN[] = {"btn_up", "btn_down", "btn_ok", "btn_back"};
    static const char* const AUX[] = {"aux1", "aux2", "aux3", "aux4"};

    Use used[40];
    uint8_t n = 0;
    auto use = [&](const char* name, int p) {
        if (p >= 0) used[n++] = {name, p};
    };

    for (uint8_t a = 0; a < 4; a++) {
        if (c.joy[a] < 0) {
            err = String(JOY[a]) + " is required";
            return false;
        }
        if (!checkAdc(JOY[a], c.joy[a], err)) return false;
        use(JOY[a], c.joy[a]);
    }
    if (!checkAdc("battery", c.battery, err)) return false;
    use("battery", c.battery);

    if (c.crsfRx < 0 || c.crsfTx < 0) {
        err = "crsf_rx and crsf_tx are required";
        return false;
    }
    if (!checkPin("crsf_rx", c.crsfRx, c.halfDuplex(), err) || !checkPin("crsf_tx", c.crsfTx, true, err)) return false;
    use("crsf_rx", c.crsfRx);
    if (!c.halfDuplex()) use("crsf_tx", c.crsfTx);
    if (c.crsfBaud < 115200 || c.crsfBaud > 5250000) {
        err = "crsf_baud: 115200..5250000 (ELRS: 400000)";
        return false;
    }

    const bool adcButtons = c.buttons == ButtonMode::Adc;
    if (adcButtons) {
        if (c.btnAdc < 0) {
            err = "btn_adc is required with buttons = adc";
            return false;
        }
        if (!checkAdc("btn_adc", c.btnAdc, err)) return false;
        use("btn_adc", c.btnAdc);
    }
    const bool encoder = c.buttons == ButtonMode::Encoder;
    if (encoder) {
        if (c.encA < 0 || c.encB < 0 || c.encSw < 0) {
            err = "enc_a, enc_b and enc_sw are required with buttons = encoder";
            return false;
        }
        // Pull-ups needed: input-only pins (no pull-up) are rejected by the output check
        if (!checkPin("enc_a", c.encA, true, err) || !checkPin("enc_b", c.encB, true, err) ||
            !checkPin("enc_sw", c.encSw, true, err))
            return false;
        use("enc_a", c.encA);
        use("enc_b", c.encB);
        use("enc_sw", c.encSw);
    }
    if (c.inputs == InputMode::Gpio) {
        // Pull-ups needed: input-only pins (no pull-up) are rejected by the output check
        for (uint8_t i = 0; i < 4; i++) {
            if (!adcButtons && !encoder) {
                if (c.button[i] < 0) {
                    err = String(BTN[i]) + " is required with inputs = gpio (or use buttons = adc / encoder)";
                    return false;
                }
                if (!checkPin(BTN[i], c.button[i], true, err)) return false;
                use(BTN[i], c.button[i]);
            }
            if (!checkPin(AUX[i], c.aux[i], true, err)) return false;
            use(AUX[i], c.aux[i]);
        }
    }

    if (c.usesI2c()) {
        if (c.i2cSda < 0 || c.i2cScl < 0) {
            err = "i2c_sda and i2c_scl are required for the OLED or PCF8574";
            return false;
        }
        if (!checkPin("i2c_sda", c.i2cSda, true, err) || !checkPin("i2c_scl", c.i2cScl, true, err)) return false;
        use("i2c_sda", c.i2cSda);
        use("i2c_scl", c.i2cScl);
    }

    if (c.display == DisplayType::St7789) {
        if (c.tftSclk < 0 || c.tftMosi < 0 || c.tftDc < 0) {
            err = "st7789 needs tft_sclk, tft_mosi and tft_dc";
            return false;
        }
        if (!checkPin("tft_sclk", c.tftSclk, true, err) || !checkPin("tft_mosi", c.tftMosi, true, err) ||
            !checkPin("tft_dc", c.tftDc, true, err) || !checkPin("tft_rst", c.tftRst, true, err))
            return false;
        use("tft_sclk", c.tftSclk);
        use("tft_mosi", c.tftMosi);
        use("tft_dc", c.tftDc);
        use("tft_rst", c.tftRst);
    }

    if (!checkPin("led", c.led, true, err) || !checkPin("buzzer", c.buzzer, true, err)) return false;
    use("led", c.led);
    use("buzzer", c.buzzer);

    for (uint8_t i = 0; i < n; i++) {
        for (uint8_t j = i + 1; j < n; j++) {
            if (used[i].pin == used[j].pin) {
                err = String(used[i].name) + " and " + used[j].name + " both use GPIO" + used[i].pin;
                return false;
            }
        }
    }
    return true;
}

// ---------- JSON ----------

template <size_t N>
int indexOf(const char* const (&names)[N], const char* s) {
    for (size_t i = 0; i < N; i++) {
        if (s && strcmp(names[i], s) == 0) return i;
    }
    return -1;
}

// Default document with the override's keys on top; unknown keys are an error
bool merge(const String& override, JsonDocument& doc, String& err) {
    if (deserializeJson(doc, HW_DEFAULT)) {
        err = "built-in hardware.json is invalid";
        return false;
    }
    if (!override.length()) return true;
    JsonDocument o;
    const DeserializationError e = deserializeJson(o, override);
    if (e) {
        err = String("JSON: ") + e.c_str();
        return false;
    }
    if (!o.is<JsonObject>()) {
        err = "JSON: expected an object";
        return false;
    }
    for (JsonPair kv : o.as<JsonObject>()) {
        if (doc[kv.key()].isNull()) {
            err = String("unknown key: ") + kv.key().c_str();
            return false;
        }
        doc[kv.key()] = kv.value();
    }
    return true;
}

bool parse(const JsonDocument& d, HardwareConfig& c, String& err) {
    c = {};
    static const char* const JOY[] = {"joy_roll", "joy_pitch", "joy_throttle", "joy_yaw"};
    static const char* const BTN[] = {"btn_up", "btn_down", "btn_ok", "btn_back"};
    static const char* const AUX[] = {"aux1", "aux2", "aux3", "aux4"};
    for (uint8_t i = 0; i < 4; i++) {
        c.joy[i] = d[JOY[i]] | -1;
        c.button[i] = d[BTN[i]] | -1;
        c.aux[i] = d[AUX[i]] | -1;
    }
    c.battery = d["battery"] | -1;
    c.crsfRx = d["crsf_rx"] | -1;
    c.crsfTx = d["crsf_tx"] | -1;
    c.crsfBaud = d["crsf_baud"] | 400000;

    const int in = indexOf(INPUT_NAMES, d["inputs"] | "");
    if (in < 0) {
        err = "inputs: gpio or pcf8574";
        return false;
    }
    c.inputs = (InputMode)in;
    c.expanderAddr = d["expander_addr"] | 0x20;

    const int bm = indexOf(BUTTON_MODES, d["buttons"] | "inputs");
    if (bm < 0) {
        err = "buttons: inputs, adc or encoder";
        return false;
    }
    c.buttons = (ButtonMode)bm;
    c.btnAdc = d["btn_adc"] | -1;
    JsonArrayConst values = d["btn_adc_values"].as<JsonArrayConst>();
    if (values.size() != ADC_BTN_COUNT) {
        err = "btn_adc_values: 6 readings (up, down, left, right, enter, idle, as ExpressLRS joystick_values)";
        return false;
    }
    for (uint8_t i = 0; i < ADC_BTN_COUNT; i++) c.btnAdcValues[i] = constrain((int)(values[i] | 0), 0, 4095);
    c.encA = d["enc_a"] | -1;
    c.encB = d["enc_b"] | -1;
    c.encSw = d["enc_sw"] | -1;
    c.encReverse = d["enc_reverse"] | false;
    c.i2cSda = d["i2c_sda"] | -1;
    c.i2cScl = d["i2c_scl"] | -1;

    const int disp = indexOf(DISPLAY_NAMES, d["display"] | "");
    if (disp < 0) {
        err = "display: none, ssd1306 or st7789";
        return false;
    }
    c.display = (DisplayType)disp;
    c.oledAddr = d["oled_addr"] | 0x3C;
    c.tftSclk = d["tft_sclk"] | -1;
    c.tftMosi = d["tft_mosi"] | -1;
    c.tftDc = d["tft_dc"] | -1;
    c.tftRst = d["tft_rst"] | -1;
    c.tftRotation = (d["tft_rotation"] | 0) & 3;

    c.led = d["led"] | -1;
    c.ledRgb = d["led_rgb"] | false;
    c.ledActiveLow = d["led_active_low"] | false;
    c.buzzer = d["buzzer"] | -1;
    return validate(c, err);
}

String loadSaved() {
    Preferences p;
    if (!p.begin(NVS_NS, true)) return "";
    const String s = p.isKey(NVS_KEY) ? p.getString(NVS_KEY) : "";
    p.end();
    return s;
}

// OK button of the built-in layout held at boot -> ignore the saved layout
bool rescueRequested(const JsonDocument& def) {
    if (strcmp(def["inputs"] | "", "gpio") != 0) return false;
    const int pin = def["btn_ok"] | -1;
    if (pin < 0) return false;
    pinMode(pin, INPUT_PULLUP);
    delay(5);
    return digitalRead(pin) == LOW;
}

}  // namespace

void Hardware::begin() {
    String err;
    JsonDocument def;
    merge("", def, err);
    ignored = rescueRequested(def);

    const String saved = ignored ? "" : loadSaved();
    JsonDocument doc;
    if (saved.length() && merge(saved, doc, err) && parse(doc, active, err)) {
        fromOverride = true;
    } else {
        if (saved.length()) Serial.printf("hardware.json saved but invalid (%s), using built-in\n", err.c_str());
        doc.clear();
        merge("", doc, err);
        if (!parse(doc, active, err)) Serial.printf("built-in hardware.json: %s\n", err.c_str());
        fromOverride = false;
    }
    activeJson = "";
    serializeJsonPretty(doc, activeJson);
}

const HardwareConfig& Hardware::get() { return active; }
bool Hardware::usingOverride() { return fromOverride; }
bool Hardware::overrideIgnored() { return ignored; }
String Hardware::json() { return activeJson; }

String Hardware::defaultJson() {
    String err, out;
    JsonDocument doc;
    merge("", doc, err);
    serializeJsonPretty(doc, out);
    return out;
}

String Hardware::savedJson() {
    const String saved = loadSaved();
    if (!saved.length()) return defaultJson();
    String err, out;
    JsonDocument doc;
    if (!merge(saved, doc, err)) return defaultJson();
    serializeJsonPretty(doc, out);
    return out;
}

bool Hardware::save(const String& json, String& err) {
    JsonDocument doc;
    HardwareConfig c;
    if (!merge(json, doc, err) || !parse(doc, c, err)) return false;
    String compact;
    serializeJson(doc, compact);
    Preferences p;
    if (!p.begin(NVS_NS, false) || !p.putString(NVS_KEY, compact)) {
        p.end();
        err = "flash write failed";
        return false;
    }
    p.end();
    return true;
}

bool Hardware::set(const String& key, const String& value, String& err) {
    JsonDocument doc;
    if (!merge(loadSaved(), doc, err)) return false;
    JsonVariant v = doc[key];
    if (v.isNull()) {
        err = "unknown key: " + key;
        return false;
    }
    if (v.is<bool>()) {
        if (value == "true" || value == "on" || value == "1") v.set(true);
        else if (value == "false" || value == "off" || value == "0") v.set(false);
        else {
            err = key + ": true or false";
            return false;
        }
    } else if (v.is<JsonArray>()) {
        // Comma-separated list: hw set btn_adc_values 0,740,1310,2050,2820,4095
        JsonArray a = v.to<JsonArray>();
        int from = 0;
        while (from <= (int)value.length()) {
            int comma = value.indexOf(',', from);
            if (comma < 0) comma = value.length();
            const String item = value.substring(from, comma);
            char* end;
            const long n = strtol(item.c_str(), &end, 0);
            if (!item.length() || *end) {
                err = key + ": comma-separated numbers expected";
                return false;
            }
            a.add(n);
            from = comma + 1;
        }
    } else if (v.is<long>()) {
        char* end;
        const long n = strtol(value.c_str(), &end, 0);  // accepts 0x3C
        if (!value.length() || *end) {
            err = key + ": number expected";
            return false;
        }
        v.set(n);
    } else {
        v.set(value);
    }
    String json;
    serializeJson(doc, json);
    return save(json, err);
}

bool Hardware::setButtonValue(AdcButton b, uint16_t value, String& err) {
    JsonDocument doc;
    if (!merge(loadSaved(), doc, err)) return false;
    doc["btn_adc_values"][(uint8_t)b] = value;
    String json;
    serializeJson(doc, json);
    if (!save(json, err)) return false;
    active.btnAdcValues[b] = value;  // learning takes effect without a reboot
    return true;
}

const char* Hardware::adcButtonName(AdcButton b) { return ADC_BUTTON_NAMES[b]; }

void Hardware::reset() {
    Preferences p;
    if (p.begin(NVS_NS, false)) {
        p.remove(NVS_KEY);
        p.end();
    }
}

const char* Hardware::displayName(DisplayType t) { return DISPLAY_NAMES[(uint8_t)t]; }
