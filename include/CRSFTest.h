#ifndef CRSF_TEST_H
#define CRSF_TEST_H

#include <Arduino.h>
#include "CRSF.h"

class CRSFTest {
public:
    static bool testPacketGeneration();
    static bool testChannelMapping();
    static bool testCRC();
    static bool testFrameRoundTrip();
    static bool testTelemetryParsing();
    static bool validateFrame(const uint8_t* frame, size_t length);
    static void runAllTests();
};

#endif // CRSF_TEST_H
