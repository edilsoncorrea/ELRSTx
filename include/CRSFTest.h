#ifndef CRSF_TEST_H
#define CRSF_TEST_H

#include <Arduino.h>
#include "CRSF.h"

class CRSFTest {
public:
    // Test CRSF packet generation
    static bool testPacketGeneration();
    
    // Test channel value mapping
    static bool testChannelMapping();
    
    // Test CRC calculation
    static bool testCRC();
    
    // Validate CRSF frame structure
    static bool validateFrame(const uint8_t* frame, size_t length);
    
    // Run all tests
    static void runAllTests();
    
    // Print frame in hex format for debugging
    static void printFrame(const uint8_t* frame, size_t length);
    
private:
    static bool compareFrames(const uint8_t* frame1, const uint8_t* frame2, size_t length);
};

#endif // CRSF_TEST_H
