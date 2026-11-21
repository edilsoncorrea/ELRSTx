#pragma once
#include <Arduino.h>

// Pins are no longer fixed here: they come from hardware/<board>.json at boot (see Hardware.h).

#define FW_VERSION          "2.2.0"

// Bit layout of the input mask, same for PCF8574 pins P0..P7 and direct GPIOs
#define EXP_BTN_UP          0
#define EXP_BTN_DOWN        1
#define EXP_BTN_OK          2
#define EXP_BTN_BACK        3
#define EXP_SW_FIRST        4   // bits 4..7 -> AUX1..AUX4 (CH5..CH8), AUX1 = arm
