#include "CRSF.h"

// CRC8 DVB-S2 (poly 0xD5)
static const uint8_t crc8_dvb_s2_table[256] = {
    0x00, 0xD5, 0x7F, 0xAA, 0xFE, 0x2B, 0x81, 0x54, 0x29, 0xFC, 0x56, 0x83, 0xD7, 0x02, 0xA8, 0x7D,
    0x52, 0x87, 0x2D, 0xF8, 0xAC, 0x79, 0xD3, 0x06, 0x7B, 0xAE, 0x04, 0xD1, 0x85, 0x50, 0xFA, 0x2F,
    0xA4, 0x71, 0xDB, 0x0E, 0x5A, 0x8F, 0x25, 0xF0, 0x8D, 0x58, 0xF2, 0x27, 0x73, 0xA6, 0x0C, 0xD9,
    0xF6, 0x23, 0x89, 0x5C, 0x08, 0xDD, 0x77, 0xA2, 0xDF, 0x0A, 0xA0, 0x75, 0x21, 0xF4, 0x5E, 0x8B,
    0x9D, 0x48, 0xE2, 0x37, 0x63, 0xB6, 0x1C, 0xC9, 0xB4, 0x61, 0xCB, 0x1E, 0x4A, 0x9F, 0x35, 0xE0,
    0xCF, 0x1A, 0xB0, 0x65, 0x31, 0xE4, 0x4E, 0x9B, 0xE6, 0x33, 0x99, 0x4C, 0x18, 0xCD, 0x67, 0xB2,
    0x39, 0xEC, 0x46, 0x93, 0xC7, 0x12, 0xB8, 0x6D, 0x10, 0xC5, 0x6F, 0xBA, 0xEE, 0x3B, 0x91, 0x44,
    0x6B, 0xBE, 0x14, 0xC1, 0x95, 0x40, 0xEA, 0x3F, 0x42, 0x97, 0x3D, 0xE8, 0xBC, 0x69, 0xC3, 0x16,
    0xEF, 0x3A, 0x90, 0x45, 0x11, 0xC4, 0x6E, 0xBB, 0xC6, 0x13, 0xB9, 0x6C, 0x38, 0xED, 0x47, 0x92,
    0xBD, 0x68, 0xC2, 0x17, 0x43, 0x96, 0x3C, 0xE9, 0x94, 0x41, 0xEB, 0x3E, 0x6A, 0xBF, 0x15, 0xC0,
    0x4B, 0x9E, 0x34, 0xE1, 0xB5, 0x60, 0xCA, 0x1F, 0x62, 0xB7, 0x1D, 0xC8, 0x9C, 0x49, 0xE3, 0x36,
    0x19, 0xCC, 0x66, 0xB3, 0xE7, 0x32, 0x98, 0x4D, 0x30, 0xE5, 0x4F, 0x9A, 0xCE, 0x1B, 0xB1, 0x64,
    0x72, 0xA7, 0x0D, 0xD8, 0x8C, 0x59, 0xF3, 0x26, 0x5B, 0x8E, 0x24, 0xF1, 0xA5, 0x70, 0xDA, 0x0F,
    0x20, 0xF5, 0x5F, 0x8A, 0xDE, 0x0B, 0xA1, 0x74, 0x09, 0xDC, 0x76, 0xA3, 0xF7, 0x22, 0x88, 0x5D,
    0xD6, 0x03, 0xA9, 0x7C, 0x28, 0xFD, 0x57, 0x82, 0xFF, 0x2A, 0x80, 0x55, 0x01, 0xD4, 0x7E, 0xAB,
    0x84, 0x51, 0xFB, 0x2E, 0x7A, 0xAF, 0x05, 0xD0, 0xAD, 0x78, 0xD2, 0x07, 0x53, 0x86, 0x2C, 0xF9
};

static uint16_t be16(const uint8_t* p) { return (uint16_t)p[0] << 8 | p[1]; }
static uint32_t be32(const uint8_t* p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

CRSF::CRSF() : _serial(nullptr), _rxPos(0) {
    for (int i = 0; i < CRSF_MAX_CHANNELS; i++) _channels[i] = CRSF_CHANNEL_VALUE_MID;
    memset(&_telem, 0, sizeof(_telem));
}

void CRSF::begin(HardwareSerial &serial) {
    _serial = &serial;
}

void CRSF::setChannel(uint8_t channel, uint16_t value) {
    if (channel >= CRSF_MAX_CHANNELS) return;
    _channels[channel] = constrain(value, CRSF_CHANNEL_VALUE_MIN, CRSF_CHANNEL_VALUE_MAX);
}

uint16_t CRSF::getChannel(uint8_t channel) {
    return channel < CRSF_MAX_CHANNELS ? _channels[channel] : CRSF_CHANNEL_VALUE_MID;
}

uint16_t CRSF::mapAnalogToCRSF(uint16_t analogValue) {
    return map(analogValue, 0, 4095, CRSF_CHANNEL_VALUE_MIN, CRSF_CHANNEL_VALUE_MAX);
}

uint8_t CRSF::crc8(const uint8_t *data, uint8_t len) {
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++) crc = crc8_dvb_s2_table[crc ^ data[i]];
    return crc;
}

void CRSF::packChannels(uint8_t *buffer) {
    // 16 channels x 11 bits, little-endian bit order = 22 bytes
    uint32_t acc = 0;
    uint8_t bits = 0;
    uint8_t pos = 0;
    for (int i = 0; i < CRSF_MAX_CHANNELS; i++) {
        acc |= (uint32_t)(_channels[i] & 0x07FF) << bits;
        bits += 11;
        while (bits >= 8) {
            buffer[pos++] = acc & 0xFF;
            acc >>= 8;
            bits -= 8;
        }
    }
}

void CRSF::buildRCFrame(uint8_t* frame) {
    frame[0] = CRSF_ADDRESS_TRANSMITTER;
    frame[1] = 24;  // type + 22 payload + crc
    frame[2] = CRSF_FRAMETYPE_RC_CHANNELS;
    packChannels(&frame[3]);
    frame[25] = crc8(&frame[2], 23);
}

void CRSF::sendRCFrame(uint8_t* captureBuffer) {
    if (_serial == nullptr) return;
    uint8_t frame[CRSF_RC_FRAME_SIZE];
    buildRCFrame(frame);
    if (captureBuffer != nullptr) memcpy(captureBuffer, frame, sizeof(frame));
    _serial->write(frame, sizeof(frame));
}

void CRSF::sendDevicePing() {
    if (_serial == nullptr) return;
    uint8_t frame[6] = {CRSF_ADDRESS_TRANSMITTER, 4, CRSF_FRAMETYPE_DEVICE_PING,
                        CRSF_ADDRESS_TRANSMITTER, CRSF_ADDRESS_RADIO, 0};
    frame[5] = crc8(&frame[2], 3);
    _serial->write(frame, sizeof(frame));
}

void CRSF::processByte(uint8_t b, uint32_t nowMs) {
    if (_rxPos == 0) {
        if (b == CRSF_ADDRESS_RADIO || b == CRSF_SYNC_BYTE || b == CRSF_ADDRESS_TRANSMITTER) _rx[_rxPos++] = b;
        return;
    }
    if (_rxPos == 1) {
        if (b < 2 || b > CRSF_FRAME_SIZE_MAX - 2) {
            _rxPos = 0;
            return;
        }
        _rx[_rxPos++] = b;
        return;
    }
    _rx[_rxPos++] = b;
    const uint8_t len = _rx[1];
    if (_rxPos < len + 2) return;
    if (crc8(&_rx[2], len - 1) == _rx[len + 1]) {
        _telem.goodFrames++;
        handleFrame(nowMs);
    } else {
        _telem.badFrames++;
    }
    _rxPos = 0;
}

void CRSF::handleFrame(uint32_t nowMs) {
    const uint8_t type = _rx[2];
    const uint8_t* p = &_rx[3];
    const uint8_t len = _rx[1] - 2;
    CRSFTelemetry& t = _telem;
    t.lastFrameMs = nowMs;

    switch (type) {
    case CRSF_FRAMETYPE_LINK_STATISTICS:
        if (len < 10) return;
        t.uplinkRssi1 = p[0];
        t.uplinkRssi2 = p[1];
        t.uplinkLq = p[2];
        t.uplinkSnr = (int8_t)p[3];
        t.activeAntenna = p[4];
        t.rfMode = p[5];
        t.txPower = p[6];
        t.downlinkRssi = p[7];
        t.downlinkLq = p[8];
        t.downlinkSnr = (int8_t)p[9];
        t.lastLinkMs = nowMs;
        break;

    case CRSF_FRAMETYPE_BATTERY_SENSOR:
        if (len < 8) return;
        t.batteryDv = be16(p);
        t.currentDa = be16(p + 2);
        t.capacityMah = (uint32_t)p[4] << 16 | (uint32_t)p[5] << 8 | p[6];
        t.remainingPct = p[7];
        t.lastBatteryMs = nowMs;
        break;

    case CRSF_FRAMETYPE_ATTITUDE:
        if (len < 6) return;
        t.pitch = (int16_t)be16(p);
        t.roll = (int16_t)be16(p + 2);
        t.yaw = (int16_t)be16(p + 4);
        t.lastAttitudeMs = nowMs;
        break;

    case CRSF_FRAMETYPE_GPS:
        if (len < 15) return;
        t.latitude = (int32_t)be32(p);
        t.longitude = (int32_t)be32(p + 4);
        t.groundSpeed = be16(p + 8);
        t.heading = be16(p + 10);
        t.altitude = be16(p + 12);
        t.satellites = p[14];
        t.lastGpsMs = nowMs;
        break;

    case CRSF_FRAMETYPE_FLIGHT_MODE: {
        const size_t n = min((size_t)len, sizeof(t.flightMode) - 1);
        memcpy(t.flightMode, p, n);
        t.flightMode[n] = 0;
        break;
    }

    case CRSF_FRAMETYPE_DEVICE_INFO: {
        // extended frame: dest, origin, null-terminated name, ...
        if (len < 3) return;
        const char* name = reinterpret_cast<const char*>(p + 2);
        const size_t n = min(strnlen(name, len - 2), sizeof(t.deviceName) - 1);
        memcpy(t.deviceName, name, n);
        t.deviceName[n] = 0;
        break;
    }

    case CRSF_FRAMETYPE_HANDSET:
        // extended frame: dest, origin, subtype, rate (BE, 0.1us), offset (BE, 0.1us)
        if (len < 11 || p[2] != CRSF_HANDSET_SUBCMD_TIMING) return;
        t.syncPeriodUs = be32(p + 3) / 10;
        t.syncOffsetUs = (int32_t)be32(p + 7) / 10;
        t.lastSyncMs = nowMs;
        break;
    }
}
