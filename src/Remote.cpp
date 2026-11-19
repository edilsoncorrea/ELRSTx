#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <ArduinoOTA.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#if defined(CONFIG_BLUEDROID_ENABLED)
#include <BLE2902.h>
#endif
#include "Commands.h"
#include "Config.h"

namespace {

const char* const NUS_SERVICE = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
const char* const NUS_RX = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
const char* const NUS_TX = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";

struct BleLine {
    char text[128];
};

WebServer server(80);
bool wifiOn = false;
bool routesReady = false;
String apSsid;

bool bleOn = false;
bool bleReady = false;
volatile bool bleLinked = false;
BLECharacteristic* bleTx = nullptr;
QueueHandle_t bleQueue = nullptr;

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
<fieldset><legend>Firmware update (OTA)</legend>
<p>Upload .pio/build/esp32-c3-devkitm-1/firmware.bin. Never update while flying.</p>
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

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer*) override { bleLinked = true; }
    void onDisconnect(BLEServer* s) override {
        bleLinked = false;
        if (bleOn) s->startAdvertising();
    }
};

// Runs in the BLE stack task: only queue the line, execute it from loop()
class RxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* c) override {
        if (!bleOn || !bleQueue) return;
        String v = c->getValue();
        v.trim();
        if (!v.length()) return;
        BleLine line = {};
        strlcpy(line.text, v.c_str(), sizeof(line.text));
        xQueueSend(bleQueue, &line, 0);
    }
};

void bleSend(const String& text) {
    if (!bleTx || !bleLinked) return;
    const String t = text + "\n";
    for (size_t i = 0; i < t.length(); i += 20) {
        const size_t n = min((size_t)20, t.length() - i);
        bleTx->setValue((uint8_t*)t.c_str() + i, n);
        bleTx->notify();
        delay(3);
    }
}

void setupRoutes() {
    server.on("/", HTTP_GET, [] { server.send(200, "text/html", PAGE); });
    server.on("/status", HTTP_GET, [] { server.send(200, "text/plain", Commands::statusText()); });
    server.on("/cmd", HTTP_POST, [] { server.send(200, "text/plain", Commands::execute(server.arg("c"))); });
    server.on(
        "/update", HTTP_POST,
        [] {
            const bool ok = !Update.hasError();
            server.send(200, "text/plain", ok ? "Update OK, rebooting..." : "Update FAILED");
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

}  // namespace

void Remote::begin() {
    bleQueue = xQueueCreate(4, sizeof(BleLine));
    char buf[16];
    snprintf(buf, sizeof(buf), "ELRSTx-%04X", (uint16_t)(ESP.getEfuseMac() >> 32));
    apSsid = buf;
}

void Remote::loop() {
    if (wifiOn) {
        server.handleClient();
        ArduinoOTA.handle();
    }
    BleLine line;
    while (bleQueue && xQueueReceive(bleQueue, &line, 0) == pdTRUE) bleSend(Commands::execute(line.text));
}

void Remote::setWifi(bool on) {
    if (on == wifiOn) return;
    if (on) {
        const Settings s = Config::get();
        WiFi.mode(WIFI_AP);
        WiFi.softAP(apSsid.c_str(), s.hw.wifiPass);
        if (!routesReady) setupRoutes();
        server.begin();
        ArduinoOTA.setHostname("elrstx");
        ArduinoOTA.setPassword(s.hw.wifiPass);
        ArduinoOTA.begin();
    } else {
        ArduinoOTA.end();
        server.stop();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);
    }
    wifiOn = on;
}

void Remote::setBle(bool on) {
    if (on == bleOn) return;
    if (on && !bleReady) {
        BLEDevice::init("ELRSTx");
        BLEServer* srv = BLEDevice::createServer();
        srv->setCallbacks(new ServerCallbacks());
        BLEService* svc = srv->createService(NUS_SERVICE);
        bleTx = svc->createCharacteristic(NUS_TX, BLECharacteristic::PROPERTY_NOTIFY);
#if defined(CONFIG_BLUEDROID_ENABLED)
        bleTx->addDescriptor(new BLE2902());  // CCCD so clients can subscribe; NimBLE adds it automatically
#endif
        BLECharacteristic* rx =
            svc->createCharacteristic(NUS_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
        rx->setCallbacks(new RxCallbacks());
        svc->start();
        BLEDevice::getAdvertising()->addServiceUUID(NUS_SERVICE);
        bleReady = true;
    }
    bleOn = on;
    // The stack stays initialised once started; turning off stops advertising and ignores input
    if (on) BLEDevice::startAdvertising();
    else BLEDevice::stopAdvertising();
}

bool Remote::wifiActive() { return wifiOn; }
bool Remote::bleActive() { return bleOn; }
bool Remote::bleConnected() { return bleLinked; }
String Remote::ssid() { return apSsid; }
