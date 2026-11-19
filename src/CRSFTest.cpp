#include "CRSFTest.h"

namespace {

uint16_t unpackChannel(const uint8_t* payload, uint8_t ch) {
    const uint32_t bitPos = ch * 11;
    const uint32_t byte = bitPos / 8;
    uint32_t v = payload[byte] | (uint32_t)payload[byte + 1] << 8;
    if (byte + 2 < 22) v |= (uint32_t)payload[byte + 2] << 16;
    return (v >> (bitPos % 8)) & 0x7FF;
}

void feed(CRSF& crsf, const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) crsf.processByte(data[i], 1000);
}

bool check(bool ok, const char* what) {
    Serial.printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    return ok;
}

}  // namespace

bool CRSFTest::testPacketGeneration() {
    Serial.println("\n=== Packet generation ===");
    CRSF crsf;
    bool ok = true;
    for (int i = 0; i < CRSF_MAX_CHANNELS; i++) crsf.setChannel(i, CRSF_CHANNEL_VALUE_MID);
    for (int i = 0; i < CRSF_MAX_CHANNELS; i++) ok &= crsf.getChannel(i) == CRSF_CHANNEL_VALUE_MID;
    return check(ok, "all channels hold the middle value");
}

bool CRSFTest::testChannelMapping() {
    Serial.println("\n=== Channel mapping ===");
    CRSF crsf;
    bool ok = true;
    ok &= check(crsf.mapAnalogToCRSF(0) == CRSF_CHANNEL_VALUE_MIN, "ADC 0 -> 172");
    ok &= check(abs((int)crsf.mapAnalogToCRSF(2048) - CRSF_CHANNEL_VALUE_MID) <= 5, "ADC 2048 -> ~992");
    ok &= check(crsf.mapAnalogToCRSF(4095) == CRSF_CHANNEL_VALUE_MAX, "ADC 4095 -> 1811");
    crsf.setChannel(0, 50);
    ok &= check(crsf.getChannel(0) == CRSF_CHANNEL_VALUE_MIN, "values below range are clamped");
    crsf.setChannel(0, 2000);
    ok &= check(crsf.getChannel(0) == CRSF_CHANNEL_VALUE_MAX, "values above range are clamped");
    return ok;
}

bool CRSFTest::testCRC() {
    Serial.println("\n=== CRC8 DVB-S2 ===");
    const uint8_t data[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    return check(CRSF::crc8(data, sizeof(data)) == 0xBC, "check value of \"123456789\" is 0xBC");
}

bool CRSFTest::testFrameRoundTrip() {
    Serial.println("\n=== RC frame round trip ===");
    CRSF crsf;
    for (int i = 0; i < CRSF_MAX_CHANNELS; i++) crsf.setChannel(i, CRSF_CHANNEL_VALUE_MIN + i * 100);
    uint8_t frame[CRSF_RC_FRAME_SIZE];
    crsf.buildRCFrame(frame);

    bool ok = validateFrame(frame, sizeof(frame));
    ok &= check(CRSF::crc8(&frame[2], 23) == frame[25], "frame CRC matches");
    bool values = true;
    for (int i = 0; i < CRSF_MAX_CHANNELS; i++) values &= unpackChannel(&frame[3], i) == crsf.getChannel(i);
    ok &= check(values, "all 16 channels unpack to the values set");
    return ok;
}

bool CRSFTest::testTelemetryParsing() {
    Serial.println("\n=== Telemetry parsing ===");
    CRSF crsf;
    bool ok = true;

    // link statistics: RSSI 45/47, LQ 100, SNR 9, ant 0, mode 7, power 3, dl RSSI 50, dl LQ 98, dl SNR 8
    uint8_t link[] = {CRSF_ADDRESS_RADIO, 12, CRSF_FRAMETYPE_LINK_STATISTICS, 45, 47, 100, 9, 0, 7, 3, 50, 98, 8, 0};
    link[13] = CRSF::crc8(&link[2], 11);
    const uint8_t garbage[] = {0x12, 0x00, 0xFF};
    feed(crsf, garbage, sizeof(garbage));
    feed(crsf, link, sizeof(link));
    const CRSFTelemetry& t = crsf.telemetry();
    ok &= check(t.uplinkLq == 100 && t.uplinkRssi1 == 45 && t.downlinkLq == 98, "link statistics parsed after garbage");

    // handset timing sync: 250 Hz = 4000 us = 40000 in 0.1 us units
    uint8_t sync[] = {CRSF_ADDRESS_RADIO, 13, CRSF_FRAMETYPE_HANDSET, CRSF_ADDRESS_RADIO, CRSF_ADDRESS_TRANSMITTER,
                      CRSF_HANDSET_SUBCMD_TIMING, 0x00, 0x00, 0x9C, 0x40, 0x00, 0x00, 0x00, 0x64, 0};
    sync[14] = CRSF::crc8(&sync[2], 12);
    feed(crsf, sync, sizeof(sync));
    ok &= check(t.syncPeriodUs == 4000 && t.syncOffsetUs == 10, "timing sync parsed (4000 us)");

    const uint32_t badBefore = t.badFrames;
    link[13] ^= 0xFF;
    feed(crsf, link, sizeof(link));
    ok &= check(t.badFrames == badBefore + 1, "corrupted frame rejected");
    return ok;
}

bool CRSFTest::validateFrame(const uint8_t* frame, size_t length) {
    bool ok = check(length == CRSF_RC_FRAME_SIZE, "frame is 26 bytes");
    ok &= check(frame[0] == CRSF_ADDRESS_TRANSMITTER, "address byte 0xEE");
    ok &= check(frame[1] == 24, "length byte 24");
    ok &= check(frame[2] == CRSF_FRAMETYPE_RC_CHANNELS, "type 0x16");
    return ok;
}

void CRSFTest::runAllTests() {
    Serial.println("\n##### CRSF unit tests #####");
    int passed = 0;
    const int total = 5;
    passed += testPacketGeneration();
    passed += testChannelMapping();
    passed += testCRC();
    passed += testFrameRoundTrip();
    passed += testTelemetryParsing();
    Serial.printf("\n##### %d/%d test groups passed #####\n\n", passed, total);
}
