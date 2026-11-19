#ifndef CRSF_H
#define CRSF_H

#include <Arduino.h>

#define CRSF_SYNC_BYTE                  0xC8
#define CRSF_ADDRESS_RADIO              0xEA
#define CRSF_ADDRESS_TRANSMITTER        0xEE
#define CRSF_FRAME_SIZE_MAX             64
#define CRSF_PAYLOAD_SIZE_MAX           62
#define CRSF_RC_FRAME_SIZE              26

#define CRSF_FRAMETYPE_GPS              0x02
#define CRSF_FRAMETYPE_BATTERY_SENSOR   0x08
#define CRSF_FRAMETYPE_LINK_STATISTICS  0x14
#define CRSF_FRAMETYPE_RC_CHANNELS      0x16
#define CRSF_FRAMETYPE_ATTITUDE         0x1E
#define CRSF_FRAMETYPE_FLIGHT_MODE      0x21
#define CRSF_FRAMETYPE_DEVICE_PING      0x28
#define CRSF_FRAMETYPE_DEVICE_INFO      0x29
#define CRSF_FRAMETYPE_HANDSET          0x3A
#define CRSF_HANDSET_SUBCMD_TIMING      0x10

#define CRSF_CHANNEL_VALUE_MIN          172
#define CRSF_CHANNEL_VALUE_MID          992
#define CRSF_CHANNEL_VALUE_MAX          1811
#define CRSF_MAX_CHANNELS               16

// Timestamps are millis(); 0 means never received
struct CRSFTelemetry {
    uint32_t lastFrameMs;
    uint32_t lastLinkMs;
    uint32_t lastSyncMs;
    uint32_t lastBatteryMs;
    uint32_t lastAttitudeMs;
    uint32_t lastGpsMs;

    uint8_t uplinkRssi1;       // -dBm
    uint8_t uplinkRssi2;
    uint8_t uplinkLq;
    int8_t uplinkSnr;
    uint8_t activeAntenna;
    uint8_t rfMode;
    uint8_t txPower;           // ELRS power index
    uint8_t downlinkRssi;
    uint8_t downlinkLq;
    int8_t downlinkSnr;

    uint16_t batteryDv;        // 0.1 V
    uint16_t currentDa;        // 0.1 A
    uint32_t capacityMah;
    uint8_t remainingPct;

    int16_t pitch;             // rad * 10000
    int16_t roll;
    int16_t yaw;

    int32_t latitude;          // deg * 1e7
    int32_t longitude;
    uint16_t groundSpeed;      // km/h * 10
    uint16_t heading;          // deg * 100
    uint16_t altitude;         // m + 1000
    uint8_t satellites;

    char flightMode[16];
    char deviceName[24];

    uint32_t syncPeriodUs;
    int32_t syncOffsetUs;

    uint32_t goodFrames;
    uint32_t badFrames;
};

class CRSF {
public:
    CRSF();

    void begin(HardwareSerial &serial);

    void setChannel(uint8_t channel, uint16_t value);
    uint16_t getChannel(uint8_t channel);
    uint16_t mapAnalogToCRSF(uint16_t analogValue);

    void buildRCFrame(uint8_t* frame);
    void sendRCFrame(uint8_t* captureBuffer = nullptr);
    void sendDevicePing();

    void processByte(uint8_t b, uint32_t nowMs);
    const CRSFTelemetry& telemetry() const { return _telem; }

    static uint8_t crc8(const uint8_t *data, uint8_t len);

private:
    HardwareSerial *_serial;
    uint16_t _channels[CRSF_MAX_CHANNELS];
    CRSFTelemetry _telem;
    uint8_t _rx[CRSF_FRAME_SIZE_MAX];
    uint8_t _rxPos;

    void packChannels(uint8_t *buffer);
    void handleFrame(uint32_t nowMs);
};

#endif // CRSF_H
