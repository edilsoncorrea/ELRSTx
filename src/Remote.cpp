#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "Commands.h"
#include "Config.h"
#include "Hardware.h"

namespace {

WebServer server(80);
constexpr const char* HOSTNAME = "elrstx";          // http://elrstx.local
constexpr uint32_t HOME_TIMEOUT_MS = 20000;          // then fall back to the own AP

Remote::Mode wifiMode = Remote::Mode::Off;
uint32_t connectStartMs = 0;
bool routesReady = false;
String apSsid;
String homeSsid;
String homePass;
String failureText;  // why the last home connection failed
uint8_t lastReason = 0;
bool eventsReady = false;

void loadHome() {
    Preferences p;
    if (!p.begin("wifi", true)) return;
    homeSsid = p.getString("ssid", "");
    homePass = p.getString("pass", "");
    p.end();
}

bool storeHome() {
    Preferences p;
    if (!p.begin("wifi", false)) return false;
    const bool ok = p.putString("ssid", homeSsid) == homeSsid.length() && p.putString("pass", homePass) == homePass.length();
    p.end();
    return ok;
}

const char PAGE[] = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ELRSTx</title>
<style>
body{font-family:system-ui,sans-serif;max-width:680px;margin:auto;padding:12px;background:#111;color:#eee}
pre{background:#1d1d1d;padding:8px;white-space:pre-wrap;border-radius:4px}
input,button{font-size:16px;padding:6px 10px;margin:2px}
fieldset{border:1px solid #444;border-radius:6px;margin:12px 0}
</style></head><body>
<h2>ELRSTx</h2>
<fieldset><legend>Status</legend><pre id="st">...</pre></fieldset>
<fieldset><legend>Command</legend>
<input id="c" placeholder="set deadband 40" size="26" onkeydown="if(event.key=='Enter')run()">
<button onclick="run()">Send</button>
<div>
<button onclick="run('config')">Config</button><button onclick="run('save')">Save</button>
<button onclick="run('cal')">Calibration</button><button onclick="run('profile')">Profiles</button>
<button onclick="run('help')">Help</button>
</div>
<pre id="out"></pre></fieldset>
<p><a href="/hardware" style="color:#6cf">Hardware (pins, display, CRSF)</a></p>
<fieldset><legend>Firmware update (OTA)</legend>
<p>Upload .pio/build/&lt;env&gt;/firmware.bin. Never update while flying.</p>
<form method="POST" action="/update" enctype="multipart/form-data">
<input type="file" name="fw" accept=".bin"><button>Upload</button></form></fieldset>
<script>
async function run(c){
  c=c||document.getElementById('c').value;
  const r=await fetch('/cmd',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'c='+encodeURIComponent(c)});
  document.getElementById('out').textContent=await r.text();
}
async function poll(){
  try{const r=await fetch('/status');document.getElementById('st').textContent=await r.text();}catch(e){}
  setTimeout(poll,1000);
}
poll();
</script></body></html>)HTML";

const char HW_PAGE[] = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ELRSTx hardware</title>
<style>
body{font-family:system-ui,sans-serif;max-width:680px;margin:auto;padding:12px;background:#111;color:#eee}
textarea{width:100%;height:60vh;background:#1d1d1d;color:#eee;font:14px monospace;border-radius:4px}
button{font-size:16px;padding:6px 10px;margin:2px}#msg{white-space:pre-wrap}
</style></head><body>
<h2>hardware.json</h2>
<p>Pins, display and CRSF wiring. Saved changes apply after a reboot. <b>Disarm and land first.</b>
crsf_rx = crsf_tx means one wire (half-duplex, inverted, like a JR bay).
If the radio stops responding: hold OK while powering on, or send <code>hw reset</code> over serial.</p>
<textarea id="j" spellcheck="false"></textarea>
<div><button onclick="save()">Save</button><button onclick="load('/hardware.json?default=1')">Built-in</button>
<button onclick="reboot()">Reboot</button> <a href="/" style="color:#6cf">back</a></div>
<p id="msg"></p>
<script>
async function load(u){const r=await fetch(u||'/hardware.json');document.getElementById('j').value=await r.text();}
async function save(){
  const r=await fetch('/hardware.json',{method:'POST',headers:{'Content-Type':'application/json'},body:document.getElementById('j').value});
  document.getElementById('msg').textContent=await r.text();
}
async function reboot(){await fetch('/cmd',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'c=reboot'});}
load();
</script></body></html>)HTML";

void setupRoutes() {
    server.on("/", HTTP_GET, [] { server.send(200, "text/html", PAGE); });
    server.on("/status", HTTP_GET, [] { server.send(200, "text/plain", Commands::statusText()); });
    server.on("/hardware", HTTP_GET, [] { server.send(200, "text/html", HW_PAGE); });
    server.on("/hardware.json", HTTP_GET, [] {
        server.send(200, "application/json", server.hasArg("default") ? Hardware::defaultJson() : Hardware::savedJson());
    });
    server.on("/hardware.json", HTTP_POST, [] {
        String err;
        if (Hardware::save(server.arg("plain"), err)) server.send(200, "text/plain", "Saved. Reboot to apply.");
        else server.send(400, "text/plain", "Not saved: " + err);
    });
    server.on("/cmd", HTTP_POST, [] { server.send(200, "text/plain", Commands::execute(server.arg("c"))); });
    server.on(
        "/update", HTTP_POST,
        [] {
            const bool ok = !Update.hasError();
            server.send(ok ? 200 : 500, "text/plain", ok ? "Update OK, rebooting..." : "Update FAILED");
            if (ok) {
                delay(500);
                ESP.restart();
            }
        },
        [] {
            HTTPUpload& up = server.upload();
            if (up.status == UPLOAD_FILE_START) Update.begin(UPDATE_SIZE_UNKNOWN);
            else if (up.status == UPLOAD_FILE_WRITE) Update.write(up.buf, up.currentSize);
            else if (up.status == UPLOAD_FILE_END) Update.end(true);
            else if (up.status == UPLOAD_FILE_ABORTED) Update.abort();
        });
    routesReady = true;
}

// Web server, OTA and mDNS once a network (home or own AP) is up
void startServices() {
    if (!routesReady) setupRoutes();
    server.begin();
    const Settings s = Config::get();
    ArduinoOTA.setHostname(HOSTNAME);
    ArduinoOTA.setPassword(s.hw.wifiPass);
    ArduinoOTA.begin();  // also starts mDNS with HOSTNAME
    MDNS.addService("http", "tcp", 80);
}

void startAp() {
    const Settings s = Config::get();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSsid.c_str(), s.hw.wifiPass);
    wifiMode = Remote::Mode::Ap;
    startServices();
}

}  // namespace

void Remote::begin() {
    char buf[16];
    snprintf(buf, sizeof(buf), "ELRSTx-%04X", (uint16_t)(ESP.getEfuseMac() >> 32));
    apSsid = buf;
    loadHome();
}

void Remote::loop() {
    if (wifiMode == Mode::Connecting) {
        if (WiFi.status() == WL_CONNECTED) {
            wifiMode = Mode::Home;
            startServices();
        } else if (millis() - connectStartMs > HOME_TIMEOUT_MS) {
            switch (lastReason) {
            case WIFI_REASON_NO_AP_FOUND: failureText = "network not found (out of range, or 5 GHz only)"; break;
            case WIFI_REASON_AUTH_FAIL:
            case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
            case WIFI_REASON_HANDSHAKE_TIMEOUT: failureText = "wrong password"; break;
            case 0: failureText = "no answer from the router"; break;
            default: failureText = String("reason ") + lastReason + " (" + WiFi.disconnectReasonName((wifi_err_reason_t)lastReason) + ")";
            }
            startAp();  // home network not reachable (e.g. at the field)
        }
        return;
    }
    if (wifiMode != Mode::Off) {
        server.handleClient();
        ArduinoOTA.handle();
    }
}

void Remote::setWifi(bool on) {
    if (on == (wifiMode != Mode::Off)) return;
    if (on) {
        if (homeSsid.length()) {
            // Try the home network first; loop() falls back to the own AP
            if (!eventsReady) {
                WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
                    lastReason = info.wifi_sta_disconnected.reason;
                }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
                eventsReady = true;
            }
            lastReason = 0;
            WiFi.mode(WIFI_STA);
            WiFi.setHostname(HOSTNAME);
            WiFi.begin(homeSsid.c_str(), homePass.c_str());
            connectStartMs = millis();
            wifiMode = Mode::Connecting;
        } else {
            startAp();
        }
    } else {
        ArduinoOTA.end();
        MDNS.end();
        server.stop();
        WiFi.softAPdisconnect(true);
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        wifiMode = Mode::Off;
    }
}

bool Remote::wifiActive() { return wifiMode != Mode::Off; }
Remote::Mode Remote::mode() { return wifiMode; }
String Remote::ssid() { return wifiMode == Mode::Ap || homeSsid.isEmpty() ? apSsid : homeSsid; }
String Remote::apName() { return apSsid; }
String Remote::homeNetwork() { return homeSsid; }

String Remote::ip() {
    if (wifiMode == Mode::Home) return WiFi.localIP().toString();
    if (wifiMode == Mode::Ap) return WiFi.softAPIP().toString();
    return "";
}

String Remote::url() {
    if (wifiMode == Mode::Home) return String("http://") + HOSTNAME + ".local";
    if (wifiMode == Mode::Ap) return "http://" + WiFi.softAPIP().toString();
    return "";
}

String Remote::lastFailure() { return failureText; }

String Remote::scan() {
    const bool wasOff = wifiMode == Mode::Off;
    if (wasOff) WiFi.mode(WIFI_STA);
    else if (WiFi.getMode() == WIFI_AP) WiFi.mode(WIFI_AP_STA);
    const int n = WiFi.scanNetworks();
    String o = String(n < 0 ? 0 : n) + " networks (2.4 GHz only):";
    for (int i = 0; i < n; i++) {
        o += "\n  " + WiFi.SSID(i) + "  ch " + WiFi.channel(i) + "  " + WiFi.RSSI(i) + " dBm";
    }
    WiFi.scanDelete();
    if (wasOff) WiFi.mode(WIFI_OFF);
    return o;
}

bool Remote::setHome(const String& ssid, const String& pass) {
    homeSsid = ssid;
    homePass = pass;
    return storeHome();
}
