#pragma once
#include <Arduino.h>

// Text command interface shared by Serial, BLE UART and the web page.
// Must be called from the Arduino loop task.
namespace Commands {
String execute(const String& line);
String statusText();
String monitorLine();
extern bool monitor;
}

namespace Remote {
void begin();
void loop();
void setWifi(bool on);
void setBle(bool on);
bool wifiActive();
bool bleActive();
bool bleConnected();
String ssid();
}
