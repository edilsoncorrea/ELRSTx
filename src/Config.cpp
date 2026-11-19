#include "Config.h"
#include <Preferences.h>

namespace {

constexpr uint32_t MAGIC = 0x454C5258;
constexpr uint16_t VERSION = 1;

template <typename T>
struct Blob {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    T data;
    uint32_t checksum;
};

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
Settings current;
Preferences prefs;

uint32_t fnv1a(const void* data, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t h = 2166136261u;
    while (len--) {
        h ^= *p++;
        h *= 16777619u;
    }
    return h;
}

template <typename T>
bool readBlob(const char* key, T& out) {
    Blob<T> b;
    if (prefs.getBytesLength(key) != sizeof(b)) return false;
    prefs.getBytes(key, &b, sizeof(b));
    if (b.magic != MAGIC || b.version != VERSION || b.size != sizeof(T)) return false;
    if (fnv1a(&b.data, sizeof(T)) != b.checksum) return false;
    memcpy(&out, &b.data, sizeof(T));
    return true;
}

template <typename T>
bool writeBlob(const char* key, const T& in) {
    Blob<T> b;
    memset(&b, 0, sizeof(b));
    b.magic = MAGIC;
    b.version = VERSION;
    b.size = sizeof(T);
    memcpy(&b.data, &in, sizeof(T));
    b.checksum = fnv1a(&b.data, sizeof(T));
    return prefs.putBytes(key, &b, sizeof(b)) == sizeof(b);
}

void modelKey(uint8_t index, char* buf) { snprintf(buf, 4, "m%u", index); }

HwSettings defaultHw() {
    HwSettings h;
    memset(&h, 0, sizeof(h));
    for (auto& c : h.cal) c = {200, 2048, 3895};
    h.battRatioX1000 = 3564;  // 100k / 39k
    h.battCells = 2;
    h.battWarnMv = 3500;
    h.battCritMv = 3300;
    strlcpy(h.wifiPass, "elrstx1234", sizeof(h.wifiPass));
    return h;
}

ModelSettings defaultModel(uint8_t index) {
    ModelSettings m;
    memset(&m, 0, sizeof(m));
    snprintf(m.name, sizeof(m.name), "Model %u", index + 1);
    m.deadband = 50;
    for (uint8_t a = 0; a < AXIS_COUNT; a++) m.channel[a] = a;  // AETR
    m.safeStart = true;
    m.linkAlarm = true;
    m.linkTimeoutMs = 1000;
    return m;
}

bool loadModel(uint8_t index, ModelSettings& out) {
    char key[4];
    modelKey(index, key);
    return readBlob(key, out);
}

}  // namespace

void Config::begin() {
    prefs.begin("elrstx", false);
    Settings s;
    memset(&s, 0, sizeof(s));
    if (!readBlob("hw", s.hw)) s.hw = defaultHw();
    s.profile = prefs.getUChar("active", 0);
    if (s.profile >= PROFILE_COUNT) s.profile = 0;
    if (!loadModel(s.profile, s.model)) s.model = defaultModel(s.profile);
    set(s);
}

Settings Config::get() {
    portENTER_CRITICAL(&mux);
    Settings s = current;
    portEXIT_CRITICAL(&mux);
    return s;
}

void Config::set(const Settings& s) {
    portENTER_CRITICAL(&mux);
    current = s;
    portEXIT_CRITICAL(&mux);
}

bool Config::save() {
    const Settings s = get();
    char key[4];
    modelKey(s.profile, key);
    const bool ok = writeBlob("hw", s.hw) && writeBlob(key, s.model);
    prefs.putUChar("active", s.profile);
    return ok;
}

bool Config::selectProfile(uint8_t index) {
    if (index >= PROFILE_COUNT) return false;
    Settings s = get();
    s.profile = index;
    if (!loadModel(index, s.model)) s.model = defaultModel(index);
    set(s);
    prefs.putUChar("active", index);
    return true;
}

String Config::profileName(uint8_t index) {
    const Settings s = get();
    if (index == s.profile) return s.model.name;
    ModelSettings m;
    if (loadModel(index, m)) return m.name;
    return defaultModel(index).name;
}

void Config::resetDefaults() {
    Settings s = get();
    s.hw = defaultHw();
    s.model = defaultModel(s.profile);
    set(s);
}

const char* Config::axisName(uint8_t axis) {
    static const char* const names[AXIS_COUNT] = {"roll", "pitch", "throttle", "yaw"};
    return axis < AXIS_COUNT ? names[axis] : "?";
}

int Config::axisFromName(const String& name) {
    for (uint8_t a = 0; a < AXIS_COUNT; a++) {
        if (name.equalsIgnoreCase(axisName(a))) return a;
    }
    return -1;
}
