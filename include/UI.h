#pragma once

// OLED menu, I2C expander (menu buttons + AUX switches), status LED, buzzer and alarms.
// Runs in the Arduino loop task; never blocks the RC task.
namespace UI {
void begin();
void loop();
bool displayOk();
bool expanderOk();
}
