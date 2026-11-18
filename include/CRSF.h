#ifndef CRSF_H
#define CRSF_H

#include <Arduino.h>

// CRSF Protocol Constants
#define CRSF_SYNC_BYTE                  0xC8
#define CRSF_FRAME_SIZE_MAX             64
#define CRSF_PAYLOAD_SIZE_MAX           62

// CRSF Frame Types
#define CRSF_FRAMETYPE_RC_CHANNELS      0x16

// CRSF Channel Limits
#define CRSF_CHANNEL_VALUE_MIN          172
#define CRSF_CHANNEL_VALUE_MID          992
#define CRSF_CHANNEL_VALUE_MAX          1811
#define CRSF_DIGITAL_CHANNEL_MIN        172
#define CRSF_DIGITAL_CHANNEL_MAX        1811

// RC Channels (11 bits per channel)
#define CRSF_MAX_CHANNELS               16

class CRSF {
public:
    CRSF();

    // Initialize CRSF with Serial port - returns false if initialization failed
    bool begin(HardwareSerial &serial);
    
    // Set channel value (0-15, value 172-1811)
    void setChannel(uint8_t channel, uint16_t value);
    
    // Map analog value (0-4095) to CRSF range
    uint16_t mapAnalogToCRSF(uint16_t analogValue);
    
    // Send RC channels packet
    void sendRCFrame();
    
    // Send RC channels packet and capture to buffer
    void sendRCFrame(uint8_t* captureBuffer);
    
    // Get channel value
    uint16_t getChannel(uint8_t channel);

private:
    HardwareSerial *_serial;
    uint16_t _channels[CRSF_MAX_CHANNELS];
    
    // Calculate CRC8
    uint8_t crc8(const uint8_t *data, uint8_t len);
    
    // Pack channels into CRSF format (11 bits per channel)
    void packChannels(uint8_t *buffer);
};

#endif // CRSF_H
