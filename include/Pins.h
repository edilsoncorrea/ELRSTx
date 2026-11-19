#pragma once
#include <Arduino.h>

#define FW_VERSION          "2.1.0"

#if defined(CONFIG_IDF_TARGET_ESP32C3)
// ---------- ESP32-C3 (alternative): few GPIOs, buttons/switches through a PCF8574 ----------
#define PIN_JOY1_X          0   // Roll
#define PIN_JOY1_Y          1   // Throttle
#define PIN_JOY2_X          2   // Yaw
#define PIN_JOY2_Y          3   // Pitch
#define PIN_BATTERY         4   // ADC1_CH4, through a resistor divider
#define PIN_I2C_SDA         5
#define PIN_I2C_SCL         6
#define PIN_LED             8
#define PIN_BUZZER          10
#define PIN_CRSF_RX         20  // <- ELRS module TX
#define PIN_CRSF_TX         21  // -> ELRS module RX

#define INPUTS_VIA_EXPANDER 1
#define LED_IS_RGB          0   // 1 = WS2812 (ESP32-C3-DevKitM-1)
#define LED_ACTIVE_LOW      1   // C3 SuperMini LED lights when the pin is LOW

#else
// ---------- ESP32 DevKit (ESP32-WROOM-32): default ----------
// Analog inputs must be on ADC1 (GPIO32-39): ADC2 stops working while WiFi is on
#define PIN_JOY1_X          36  // Roll      (VP, input only)
#define PIN_JOY1_Y          39  // Throttle  (VN, input only)
#define PIN_JOY2_X          34  // Yaw       (input only)
#define PIN_JOY2_Y          35  // Pitch     (input only)
#define PIN_BATTERY         32
#define PIN_I2C_SDA         21
#define PIN_I2C_SCL         22
#define PIN_LED             2   // on-board blue LED
#define PIN_BUZZER          25
#define PIN_CRSF_RX         16  // <- ELRS module TX
#define PIN_CRSF_TX         17  // -> ELRS module RX

// Buttons and switches to GND, internal pull-ups; strapping pins 0/2/5/12/15 avoided
#define PIN_BTN_UP          4
#define PIN_BTN_DOWN        13
#define PIN_BTN_OK          14
#define PIN_BTN_BACK        27
#define PIN_SW_AUX1         18  // arm
#define PIN_SW_AUX2         19
#define PIN_SW_AUX3         23
#define PIN_SW_AUX4         26

#define INPUTS_VIA_EXPANDER 0
#define LED_IS_RGB          0
#define LED_ACTIVE_LOW      0
#endif

#define I2C_ADDR_OLED       0x3C
#define I2C_ADDR_EXPANDER   0x20   // PCF8574A uses 0x38

// Bit layout of the input mask, same for expander pins P0..P7 and direct GPIOs
#define EXP_BTN_UP          0
#define EXP_BTN_DOWN        1
#define EXP_BTN_OK          2
#define EXP_BTN_BACK        3
#define EXP_SW_FIRST        4   // bits 4..7 -> AUX1..AUX4 (CH5..CH8), AUX1 = arm

// ELRS TX modules autobaud only to 400000/115200/921600/1870000/...; 420000 is the RX->FC rate
#define CRSF_BAUDRATE       400000
// Two-wire UART modules are not inverted; single-pin S.Port modules would need true (and half-duplex wiring)
#define CRSF_INVERTED       false
