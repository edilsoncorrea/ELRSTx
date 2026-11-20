#include "ElrsParams.h"

using namespace ElrsParams;

namespace {

constexpr uint32_t SESSION_MS = 15000;
constexpr uint32_t RETRY_MS = 300;
constexpr uint32_t STATUS_MS = 1000;
constexpr uint32_t PING_MS = 1000;
constexpr uint8_t MAX_RETRIES = 3;

SemaphoreHandle_t mutex = nullptr;

struct Guard {
    Guard() { xSemaphoreTake(mutex, portMAX_DELAY); }
    ~Guard() { xSemaphoreGive(mutex); }
};

// Shared with the loop task (guarded)
Param params[MAX_PARAMS];
Status info = {};
uint32_t lastTouchMs = 0;
bool wantReload = true;
bool writePending = false;
uint8_t writeId = 0;
uint8_t writeValue = 0;
bool writeIsCommand = false;

// RC task only
uint8_t chunkBuf[256];
uint16_t chunkLen = 0;
uint8_t reqId = 0;          // field we are waiting for (0 = none)
uint8_t reqChunk = 0;
bool reqIsCommand = false;  // retry with a QUERY write: re-reading chunk 0 would reset the command
bool reqIsLoad = false;
uint32_t reqSentMs = 0;
uint8_t retries = 0;
uint8_t nextLoadId = 1;
uint8_t pollCmdId = 0;
uint32_t pollCmdAtMs = 0;
uint32_t lastPingMs = 0;
uint32_t lastStatusMs = 0;

uint16_t be16(const uint8_t* p) { return (uint16_t)p[0] << 8 | p[1]; }

// Copies a NUL-terminated string from a bounded buffer; returns bytes consumed including the NUL
size_t takeString(const uint8_t* src, size_t avail, char* dst, size_t dstSize) {
    const size_t n = strnlen(reinterpret_cast<const char*>(src), avail);
    const size_t copy = min(n, dstSize - 1);
    memcpy(dst, src, copy);
    dst[copy] = 0;
    return min(n + 1, avail);
}

int16_t readNumber(const uint8_t* p, uint8_t size, bool isSigned) {
    if (size == 1) return isSigned ? (int8_t)p[0] : p[0];
    return isSigned ? (int16_t)be16(p) : (int16_t)min<uint16_t>(be16(p), INT16_MAX);
}

void parseEntry(uint8_t id, const uint8_t* d, uint16_t len, uint32_t now) {
    if (len < 3 || id >= MAX_PARAMS) return;
    Param q = {};
    q.id = id;
    q.parent = d[0];
    q.type = d[1] & 0x7F;
    q.hidden = d[1] & 0x80;
    size_t pos = 2 + takeString(d + 2, len - 2, q.name, sizeof(q.name));

    switch (q.type) {
    case T_SELECT:
        pos += takeString(d + pos, len - pos, q.text, sizeof(q.text));
        if (pos + 4 <= len) {
            q.value = d[pos];
            q.min = d[pos + 1];
            q.max = d[pos + 2];
            takeString(d + pos + 4, len - pos - 4, q.units, sizeof(q.units));
        }
        break;
    case T_UINT8:
    case T_INT8:
    case T_UINT16:
    case T_INT16: {
        const uint8_t size = q.type >= T_UINT16 ? 2 : 1;
        const bool isSigned = q.type == T_INT8 || q.type == T_INT16;
        if (pos + 4 * size <= len) {
            q.value = readNumber(d + pos, size, isSigned);
            q.min = readNumber(d + pos + size, size, isSigned);
            q.max = readNumber(d + pos + 2 * size, size, isSigned);
            takeString(d + pos + 4 * size, len - pos - 4 * size, q.units, sizeof(q.units));
        }
        break;
    }
    case T_STRING:
    case T_INFO:
        takeString(d + pos, len - pos, q.text, sizeof(q.text));
        break;
    case T_COMMAND:
        if (pos + 2 <= len) {
            q.value = d[pos];
            q.timeout = d[pos + 1];
            takeString(d + pos + 2, len - pos - 2, q.text, sizeof(q.text));
        }
        break;
    default:
        break;  // folders carry only the name we use; float is not used by ELRS TX
    }
    q.loaded = true;

    if (q.type == T_COMMAND && q.value == CMD_EXECUTING) {
        pollCmdId = id;
        pollCmdAtMs = now + max<uint32_t>(q.timeout * 10u, 100u);
    } else if (pollCmdId == id) {
        pollCmdId = 0;
    }

    Guard g;
    params[id] = q;
}

void finishRequest() {
    if (reqIsLoad) {
        nextLoadId++;
        Guard g;
        info.loadedCount = nextLoadId - 1;
    }
    reqId = 0;
    reqChunk = 0;
    retries = 0;
    reqIsCommand = false;
    reqIsLoad = false;
}

void sendRead(CRSF& crsf, uint32_t now) {
    if (reqIsCommand && reqChunk == 0) {
        const uint8_t payload[2] = {reqId, CMD_QUERY};
        crsf.sendExtendedFrame(CRSF_FRAMETYPE_PARAMETER_WRITE, CRSF_ADDRESS_TRANSMITTER, payload, 2);
    } else {
        const uint8_t payload[2] = {reqId, reqChunk};
        crsf.sendExtendedFrame(CRSF_FRAMETYPE_PARAMETER_READ, CRSF_ADDRESS_TRANSMITTER, payload, 2);
    }
    reqSentMs = now;
}

void startRequest(CRSF& crsf, uint8_t id, bool isCommand, bool isLoad, uint32_t now) {
    reqId = id;
    reqChunk = 0;
    reqIsCommand = isCommand;
    reqIsLoad = isLoad;
    retries = 0;
    chunkLen = 0;
    sendRead(crsf, now);
}

}  // namespace

void ElrsParams::begin() {
    mutex = xSemaphoreCreateMutex();
}

void ElrsParams::onFrame(uint8_t type, const uint8_t* p, uint8_t len, uint32_t now) {
    if (len < 2 || p[1] != CRSF_ADDRESS_TRANSMITTER) return;  // TX module only
    const uint8_t* d = p + 2;
    const uint8_t n = len - 2;

    if (type == CRSF_FRAMETYPE_DEVICE_INFO) {
        char name[sizeof(info.device)];
        const size_t used = takeString(d, n, name, sizeof(name));
        if (used + 13 > n) return;  // serial, hw, sw (4 bytes each) + field count
        const uint8_t count = min<uint8_t>(d[used + 12], MAX_PARAMS - 1);
        Guard g;
        strlcpy(info.device, name, sizeof(info.device));
        if (count != info.fieldCount) {
            info.fieldCount = count;
            wantReload = true;
        }
    } else if (type == CRSF_FRAMETYPE_PARAMETER_ENTRY) {
        if (n < 2 || d[0] != reqId || reqId == 0) return;
        const uint8_t remain = d[1];
        const uint16_t take = min<uint16_t>(n - 2, sizeof(chunkBuf) - chunkLen);
        memcpy(chunkBuf + chunkLen, d + 2, take);
        chunkLen += take;
        if (remain > 0) {
            reqChunk++;
            reqSentMs = 0;  // ask for the next chunk on the next poll
            retries = 0;
            return;
        }
        parseEntry(reqId, chunkBuf, chunkLen, now);
        finishRequest();
    } else if (type == CRSF_FRAMETYPE_ELRS_STATUS) {
        if (n < 4) return;
        Guard g;
        info.badPackets = d[0];
        info.goodPackets = be16(d + 1);
        info.flags = d[3];
        takeString(d + 4, n - 4, info.message, sizeof(info.message));
        info.statusValid = true;
    }
}

void ElrsParams::poll(CRSF& crsf, uint32_t now) {
    bool active, reloadNow, doWrite = false, isCommand = false;
    uint8_t wId = 0, wValue = 0, fieldCount;
    {
        Guard g;
        active = lastTouchMs != 0 && now - lastTouchMs < SESSION_MS;
        fieldCount = info.fieldCount;
        reloadNow = wantReload;
        if (active && writePending && reqId == 0) {
            doWrite = true;
            wId = writeId;
            wValue = writeValue;
            isCommand = writeIsCommand;
            writePending = false;
        }
    }
    if (!active) return;

    // One request in flight at a time: the module answers parameter frames one by one
    if (reqId) {
        if (reqSentMs == 0) {
            sendRead(crsf, now);
        } else if (now - reqSentMs > RETRY_MS) {
            if (++retries > MAX_RETRIES) finishRequest();
            else sendRead(crsf, now);
        }
        return;
    }

    if (doWrite) {
        const uint8_t payload[2] = {wId, wValue};
        crsf.sendExtendedFrame(CRSF_FRAMETYPE_PARAMETER_WRITE, CRSF_ADDRESS_TRANSMITTER, payload, 2);
        if (isCommand) {
            // The module answers a command write with the command's new state
            reqId = wId;
            reqChunk = 0;
            reqIsCommand = true;
            reqIsLoad = false;
            retries = 0;
            chunkLen = 0;
            reqSentMs = now;
        } else {
            Guard g;
            wantReload = true;  // other fields may change (e.g. telemetry ratio options after a rate change)
        }
        return;
    }

    if (pollCmdId && (int32_t)(now - pollCmdAtMs) >= 0) {
        const uint8_t id = pollCmdId;
        pollCmdId = 0;
        startRequest(crsf, id, true, false, now);
        return;
    }

    if (fieldCount == 0) {
        if (now - lastPingMs >= PING_MS) {
            lastPingMs = now;
            crsf.sendDevicePing();
        }
        return;
    }

    if (reloadNow) {
        Guard g;
        wantReload = false;
        nextLoadId = 1;
        info.loadedCount = 0;
        info.loading = true;
    }
    if (nextLoadId <= fieldCount) {
        startRequest(crsf, nextLoadId, false, true, now);
        return;
    }
    {
        Guard g;
        info.loading = false;
    }

    if (now - lastStatusMs >= STATUS_MS) {
        lastStatusMs = now;
        const uint8_t payload[2] = {0, 0};  // field 0 write = ELRS status request
        crsf.sendExtendedFrame(CRSF_FRAMETYPE_PARAMETER_WRITE, CRSF_ADDRESS_TRANSMITTER, payload, 2);
    }
}

void ElrsParams::touch() {
    const uint32_t now = millis();
    Guard g;
    if (lastTouchMs == 0 || now - lastTouchMs >= SESSION_MS) wantReload = true;
    lastTouchMs = now ? now : 1;
}

void ElrsParams::reload() {
    Guard g;
    wantReload = true;
}

Status ElrsParams::status() {
    Guard g;
    return info;
}

bool ElrsParams::param(uint8_t id, Param& out) {
    if (id >= MAX_PARAMS) return false;
    Guard g;
    out = params[id];
    return out.loaded;
}

uint8_t ElrsParams::children(uint8_t folder, uint8_t* ids, uint8_t maxIds) {
    uint8_t n = 0;
    Guard g;
    for (uint8_t id = 1; id < MAX_PARAMS && n < maxIds; id++) {
        const Param& p = params[id];
        if (p.loaded && !p.hidden && p.parent == folder) ids[n++] = id;
    }
    return n;
}

void ElrsParams::setValue(uint8_t id, uint8_t value) {
    Guard g;
    writePending = true;
    writeId = id;
    writeValue = value;
    writeIsCommand = false;
}

void ElrsParams::command(uint8_t id, CmdStep step) {
    Guard g;
    writePending = true;
    writeId = id;
    writeValue = step;
    writeIsCommand = true;
}

String ElrsParams::valueText(const Param& p, int16_t value) {
    switch (p.type) {
    case T_SELECT: {
        const char* s = p.text;
        for (int16_t i = 0; i < value && s; i++) {
            s = strchr(s, ';');
            if (s) s++;
        }
        if (!s) return "?";
        const char* end = strchr(s, ';');
        String label = end ? String(s).substring(0, end - s) : String(s);
        return label + p.units;
    }
    case T_UINT8:
    case T_INT8:
    case T_UINT16:
    case T_INT16:
        return String(value) + p.units;
    case T_STRING:
    case T_INFO:
        return p.text;
    default:
        return "";
    }
}

int16_t ElrsParams::nextValue(const Param& p, int16_t from, int dir) {
    if (p.type != T_SELECT) return constrain(from + dir, p.min, p.max);
    const int16_t span = p.max - p.min + 1;
    if (span <= 0) return from;
    int16_t v = from;
    for (int16_t i = 0; i < span; i++) {
        v = p.min + ((v - p.min + dir) % span + span) % span;
        if (valueText(p, v).length() > strlen(p.units)) return v;  // skip blank (unavailable) options
    }
    return from;
}

int16_t ElrsParams::findOption(const Param& p, const String& label) {
    for (int16_t v = p.min; v <= p.max; v++) {
        String opt = valueText(p, v);
        if (opt.equalsIgnoreCase(label) || opt.equalsIgnoreCase(label + p.units)) return v;
    }
    return -1;
}
