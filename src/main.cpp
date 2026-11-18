#include <Arduino.h>
#include "CRSF.h"
#include "CRSFTest.h"

// ========== CONFIGURAÇÕES DE TESTE ==========
// Uncomment to enable unit tests on startup
// #define RUN_UNIT_TESTS

// Uncomment to enable SIMULATION MODE (without physical joysticks)
// #define SIMULATION_MODE

// Uncomment to show CRSF frame data in hex
// #define SHOW_CRSF_FRAMES

// Simulation parameters
#define SIM_SPEED       1.0f    // Speed multiplier for simulation
#define SIM_AMPLITUDE   0.8f    // Amplitude (0.0 to 1.0)

// Pin definitions for joysticks - ESP32-C3 Compatible
// ESP32-C3 ADC1: GPIO0, GPIO1, GPIO2, GPIO3, GPIO4
// Joystick 1 (Left) - Roll and Throttle
#define JOY1_X_PIN      0   // GPIO0 (ADC1_CH0) - Roll
#define JOY1_Y_PIN      1   // GPIO1 (ADC1_CH1) - Throttle

// Joystick 2 (Right) - Pitch and Yaw
#define JOY2_X_PIN      2   // GPIO2 (ADC1_CH2) - Yaw
#define JOY2_Y_PIN      3   // GPIO3 (ADC1_CH3) - Pitch

// UART pins for CRSF communication
// ExpressLRS TX module só precisa receber dados (nosso TX -> RX do módulo)
#define CRSF_TX_PIN     21  // GPIO21 (U1TXD) -> conecta ao RX do ExpressLRS
#define CRSF_BAUDRATE   420000

// LED pin for status indication
#define LED_PIN         8   // GPIO8 - LED interno do ESP32-C3

// Channel mapping (standard RC layout)
#define CH_ROLL         0   // Aileron
#define CH_PITCH        1   // Elevator
#define CH_THROTTLE     2   // Throttle
#define CH_YAW          3   // Rudder

// Update rate (Hz)
#define UPDATE_RATE     50
#define UPDATE_INTERVAL (1000 / UPDATE_RATE)

// CRSF object
CRSF crsf;

// Timing variables
unsigned long lastUpdate = 0;
unsigned long lastBlink = 0;
bool ledState = false;
unsigned long lastCRSFFrame = 0;

// Simulation variables
float simTime = 0.0f;
bool simulationEnabled = false;

// Communication monitoring
#define CRSF_TIMEOUT_MS 1000  // Alert if no frame sent for 1 second
bool crsfTimeout = false;

// Buffer to capture CRSF frames
uint8_t lastCrsfFrame[26];

// Calibration values (can be adjusted based on your joysticks)
// Most joysticks don't go full 0-4095 range
#define ADC_MIN         200
#define ADC_MAX         3895
#define ADC_CENTER      2048

// Deadband for center stick (helps with jitter)
#define DEADBAND        50

// Simulate smooth joystick movement
uint16_t simulateJoystick(int channel, float time) {
    // Create different patterns for each axis
    float value = 0.5f; // Default center
    
    switch(channel) {
        case 0: // Roll - Sine wave
            value = 0.5f + SIM_AMPLITUDE * 0.5f * sin(time * SIM_SPEED);
            break;
        case 1: // Pitch - Slower sine wave
            value = 0.5f + SIM_AMPLITUDE * 0.5f * sin(time * SIM_SPEED * 0.7f);
            break;
        case 2: // Throttle - Slow ramp up and down
            value = 0.3f + 0.4f * (0.5f + 0.5f * sin(time * SIM_SPEED * 0.3f));
            break;
        case 3: // Yaw - Triangle wave
            value = 0.5f + SIM_AMPLITUDE * 0.5f * sin(time * SIM_SPEED * 1.3f);
            break;
    }
    
    // Convert to ADC range (0-4095)
    return (uint16_t)(value * 4095.0f);
}

// Function to read joystick with deadband and calibration
uint16_t readJoystick(uint8_t pin) {
    int raw = analogRead(pin);
    
    // Apply deadband around center
    int centered = raw - ADC_CENTER;
    if (abs(centered) < DEADBAND) {
        return ADC_CENTER;
    }
    
    // Constrain and return
    return constrain(raw, ADC_MIN, ADC_MAX);
}

// Print CRSF frame in hex format
void printCRSFFrame(const uint8_t* frame) {
    Serial.println("\n╔════════════════════════════════════════════════════╗");
    Serial.println("║           CRSF Frame Analysis                      ║");
    Serial.println("╠════════════════════════════════════════════════════╣");
    
    // Header
    Serial.printf("║ [00] Sync Byte:    0x%02X (%s)                   ║\n", 
                  frame[0], frame[0] == 0xC8 ? "✓" : "✗");
    Serial.printf("║ [01] Frame Length: %3d bytes                       ║\n", frame[1]);
    Serial.printf("║ [02] Frame Type:   0x%02X (RC Channels)            ║\n", frame[2]);
    
    Serial.println("║                                                    ║");
    Serial.println("║ Payload (22 bytes - packed channels):             ║");
    Serial.print("║ ");
    for (int i = 3; i < 25; i++) {
        Serial.printf("%02X ", frame[i]);
        if (i == 13) Serial.print("║\n║ ");
    }
    Serial.println("║");
    
    Serial.printf("║ [25] CRC8:         0x%02X                           ║\n", frame[25]);
    Serial.println("╚════════════════════════════════════════════════════╝");
    
    // Decode channels
    Serial.println("\nChannel Values:");
    Serial.println("┌────┬─────────┬──────────┬─────────┐");
    Serial.println("│ CH │  Value  │   μs     │  Label  │");
    Serial.println("├────┼─────────┼──────────┼─────────┤");
    
    const char* labels[] = {"Roll", "Pitch", "Throt", "Yaw", "AUX1", "AUX2", "AUX3", "AUX4"};
    for (int i = 0; i < 8; i++) {
        uint16_t val = crsf.getChannel(i);
        // Convert CRSF value (172-1811) to microseconds (1000-2000)
        uint16_t us = map(val, CRSF_CHANNEL_VALUE_MIN, CRSF_CHANNEL_VALUE_MAX, 1000, 2000);
        Serial.printf("│ %2d │  %4d   │  %4d μs │ %-7s │\n", i+1, val, us, labels[i]);
    }
    Serial.println("└────┴─────────┴──────────┴─────────┘\n");
}

void setup() {
    // Configure LED first to show we're alive
    pinMode(LED_PIN, OUTPUT);

    // Blink 3 times to show boot
    for(int i = 0; i < 3; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(200);
        digitalWrite(LED_PIN, LOW);
        delay(200);
    }

    // Initialize Serial for debugging - ESP32-C3 USB CDC
    Serial.begin(115200);
    delay(3000); // Wait for USB CDC to be ready

    Serial.println();
    Serial.println();
    Serial.println("===========================================");
    Serial.println("   ESP32-C3 ELRS Radio Controller");
    Serial.println("===========================================");
    Serial.println();
    Serial.print("Boot OK! Free heap: ");
    Serial.println(ESP.getFreeHeap());
    Serial.println();

#ifdef RUN_UNIT_TESTS
    // Run unit tests
    CRSFTest::runAllTests();
    Serial.println("\nPress any key to continue to normal operation...");
    while (!Serial.available()) {
        delay(100);
    }
    while (Serial.available()) Serial.read(); // Clear buffer
#endif

    // LED is already configured above, just ensure it's set to on
    digitalWrite(LED_PIN, HIGH);
    
    // Configure ADC resolution (ESP32 default is 12-bit = 0-4095)
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db); // Full range ~0-3.3V
    delay(100); // Wait for ADC to be ready

    // Configure joystick pins
    pinMode(JOY1_X_PIN, INPUT);
    pinMode(JOY1_Y_PIN, INPUT);
    pinMode(JOY2_X_PIN, INPUT);
    pinMode(JOY2_Y_PIN, INPUT);

    // Test ADC with dummy reads to ensure it's working
    for (int i = 0; i < 4; i++) {
        analogRead(JOY1_X_PIN);
        delay(10);
    }
    
    // Initialize CRSF on Serial1 (TX only - ExpressLRS only needs to receive)
    // ESP32-C3 has Serial (USB CDC) and Serial1 (UART1)
    // Serial1 uses GPIO21 as default TX, GPIO20 as default RX
    Serial1.begin(CRSF_BAUDRATE, SERIAL_8N1, -1, CRSF_TX_PIN);
    delay(100); // Wait for Serial1 to initialize

    // Initialize CRSF with validation
    if (!crsf.begin(Serial1)) {
        Serial.println("ERROR: CRSF initialization failed!");
        Serial.println("System halted - cannot communicate with ExpressLRS");
        while(1) {
            digitalWrite(LED_PIN, HIGH);
            delay(100);
            digitalWrite(LED_PIN, LOW);
            delay(100);
        }
    }

    Serial.println("✓ CRSF initialized on Serial1");
    Serial.printf("  Baudrate: %d bps\n", CRSF_BAUDRATE);
    Serial.printf("  TX Pin: GPIO%d (connects to ExpressLRS RX)\n", CRSF_TX_PIN);
    Serial.println();
    
#ifdef SIMULATION_MODE
    simulationEnabled = true;
    Serial.println("\n╔════════════════════════════════════════════════════╗");
    Serial.println("║  SIMULATION MODE ACTIVE                            ║");
    Serial.println("║  Physical joysticks will be ignored                ║");
    Serial.println("║  Smooth simulated movements will be generated      ║");
    Serial.println("╚════════════════════════════════════════════════════╝");
#else
    Serial.println("\n--- Hardware Mode: Using physical joysticks ---");
#endif
    
    Serial.println("\nJoystick mapping:");
    Serial.println("  Left Stick X  -> Roll (CH1)");
    Serial.println("  Left Stick Y  -> Throttle (CH3)");
    Serial.println("  Right Stick X -> Yaw (CH4)");
    Serial.println("  Right Stick Y -> Pitch (CH2)");
    Serial.printf("\nUpdate rate: %d Hz\n", UPDATE_RATE);
    Serial.println("\nStarting main loop...\n");
    
    delay(100);
}

void loop() {
    unsigned long currentTime = millis();
    
    // Update RC channels at specified rate
    if (currentTime - lastUpdate >= UPDATE_INTERVAL) {
        lastUpdate = currentTime;
        
        uint16_t joy1_x, joy1_y, joy2_x, joy2_y;
        
        // Read joystick values (real or simulated)
        if (simulationEnabled) {
            // Simulation mode - generate smooth movements
            simTime = currentTime / 1000.0f; // Convert to seconds
            joy1_x = simulateJoystick(0, simTime); // Roll
            joy1_y = simulateJoystick(2, simTime); // Throttle
            joy2_x = simulateJoystick(3, simTime); // Yaw
            joy2_y = simulateJoystick(1, simTime); // Pitch
        } else {
            // Hardware mode - read real joysticks
            joy1_x = readJoystick(JOY1_X_PIN);  // Roll
            joy1_y = readJoystick(JOY1_Y_PIN);  // Throttle
            joy2_x = readJoystick(JOY2_X_PIN);  // Yaw
            joy2_y = readJoystick(JOY2_Y_PIN);  // Pitch
        }
        
        // Map analog values to CRSF channel values
        crsf.setChannel(CH_ROLL, crsf.mapAnalogToCRSF(joy1_x));
        crsf.setChannel(CH_PITCH, crsf.mapAnalogToCRSF(joy2_y));
        crsf.setChannel(CH_THROTTLE, crsf.mapAnalogToCRSF(joy1_y));
        crsf.setChannel(CH_YAW, crsf.mapAnalogToCRSF(joy2_x));
        
        // Send CRSF frame with capture
        lastCRSFFrame = currentTime;
        crsfTimeout = false;  // Reset timeout flag

#ifdef SHOW_CRSF_FRAMES
        crsf.sendRCFrame(lastCrsfFrame);
#else
        crsf.sendRCFrame();
#endif
        
        // Debug output every 500ms (non-blocking debug)
        static unsigned long lastDebug = 0;
        static unsigned long frameCount = 0;
        frameCount++;

        if (currentTime - lastDebug >= 500) {
            lastDebug = currentTime;

            // Use compact output to minimize Serial printf blocking
            char debugBuf[128];

            if (simulationEnabled) {
                snprintf(debugBuf, sizeof(debugBuf),
                    "[SIM] T:%.1fs | ADC:L(%u,%u) R(%u,%u) | CRSF:R%u P%u T%u Y%u | F:%lu\n",
                    simTime, joy1_x, joy1_y, joy2_x, joy2_y,
                    crsf.getChannel(CH_ROLL),
                    crsf.getChannel(CH_PITCH),
                    crsf.getChannel(CH_THROTTLE),
                    crsf.getChannel(CH_YAW),
                    frameCount);
            } else {
                snprintf(debugBuf, sizeof(debugBuf),
                    "[HW] ADC:L(%u,%u) R(%u,%u) | CRSF:R%u P%u T%u Y%u | F:%lu\n",
                    joy1_x, joy1_y, joy2_x, joy2_y,
                    crsf.getChannel(CH_ROLL),
                    crsf.getChannel(CH_PITCH),
                    crsf.getChannel(CH_THROTTLE),
                    crsf.getChannel(CH_YAW),
                    frameCount);
            }

            // Send debug output (should not block, but keep it concise)
            Serial.print(debugBuf);

#ifdef SHOW_CRSF_FRAMES
            printCRSFFrame(lastCrsfFrame);
#endif
        }
    }
    
    // Check for CRSF timeout
    if (currentTime - lastCRSFFrame > CRSF_TIMEOUT_MS) {
        if (!crsfTimeout) {
            crsfTimeout = true;
            Serial.println("⚠️  WARNING: CRSF frame timeout - check ExpressLRS connection");
        }
    }

    // Blink LED to show activity (fast if timeout)
    unsigned long blinkInterval = crsfTimeout ? 200 : 500;
    if (currentTime - lastBlink >= blinkInterval) {
        lastBlink = currentTime;
        ledState = !ledState;
        digitalWrite(LED_PIN, ledState);
    }
    
    // Check for serial commands
    if (Serial.available()) {
        char cmd = Serial.read();
        switch(cmd) {
            case 't': // Toggle simulation mode
                simulationEnabled = !simulationEnabled;
                Serial.printf("\n>>> Simulation mode: %s\n\n", 
                             simulationEnabled ? "ENABLED" : "DISABLED");
                break;
                
            case 'f': // Show frame analysis
                Serial.println("\n>>> Analyzing current CRSF frame...");
                crsf.sendRCFrame(lastCrsfFrame);
                printCRSFFrame(lastCrsfFrame);
                break;
                
            case 'r': // Reset simulation time
                simTime = 0;
                Serial.println("\n>>> Simulation time reset\n");
                break;
                
            case 'h': // Help
                Serial.println("\n╔════════════════════════════════════════════════════╗");
                Serial.println("║              Available Commands                    ║");
                Serial.println("╠════════════════════════════════════════════════════╣");
                Serial.println("║ t - Toggle simulation mode                         ║");
                Serial.println("║ f - Show detailed CRSF frame analysis              ║");
                Serial.println("║ r - Reset simulation timer                         ║");
                Serial.println("║ h - Show this help                                 ║");
                Serial.println("╚════════════════════════════════════════════════════╝\n");
                break;
        }
        // Clear remaining serial buffer
        while (Serial.available()) Serial.read();
    }
}
