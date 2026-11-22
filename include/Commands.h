#pragma once
#include <Arduino.h>

// Text command interface shared by Serial and the web page.
// Must be called from the Arduino loop task.
namespace Commands {
String execute(const String& line);
String statusText();
String monitorLine();
extern bool monitor;
}

// WiFi: joins the home network when one is saved (http://elrstx.local), otherwise
// or when it is out of reach after 10 s, opens its own AP (http://192.168.4.1).
namespace Remote {
enum class Mode : uint8_t { Off, Connecting, Home, Ap };
void begin();
void loop();
void setWifi(bool on);
bool wifiActive();
Mode mode();
String ssid();         // network in use (home or own AP)
String apName();       // own AP name, ELRSTx-XXXX
String homeNetwork();  // saved home network, "" = none
String ip();
String url();
bool setHome(const String& ssid, const String& pass);  // "" ssid = forget
String lastFailure();  // why the home network failed last time, "" = never
String scan();         // visible networks
}
