#include <Arduino.h>
#include <esp_timer.h>
#include "CRSF.h"
#include "CRSFTest.h"
#include "Commands.h"
#include "Config.h"
#include "ElrsParams.h"
#include "Inputs.h"
#include "Pins.h"
#include "Safety.h"
#include "State.h"
#include "UI.h"

// Uncomment to run the protocol unit tests at boot
#define RUN_UNIT_TESTS

namespace {

constexpr uint32_t DEFAULT_PERIOD_US = 20000;  // 50 Hz until the module sends its timing
constexpr uint32_t MIN_PERIOD_US = 1000;
constexpr uint32_t BATTERY_INTERVAL_MS = 100;
constexpr uint32_t PING_INTERVAL_MS = 1000;

CRSF crsf;
Safety safety;
TaskHandle_t rcTaskHandle = nullptr;
esp_timer_handle_t rcTimer = nullptr;
uint32_t periodUs = DEFAULT_PERIOD_US;

void onRcTimer(void*) { xTaskNotifyGive(rcTaskHandle); }

void setPeriod(uint32_t us) {
    if (us == periodUs) return;
    periodUs = us;
    esp_timer_stop(rcTimer);
    esp_timer_start_periodic(rcTimer, periodUs);
}

// ELRS sends its packet interval to the handset; sending at that rate gives the lowest latency
uint32_t wantedPeriod(const CRSFTelemetry& t, uint32_t now) {
    if (!State::fresh(t.lastSyncMs, now, 2000) || t.syncPeriodUs == 0) return DEFAULT_PERIOD_US;
    return constrain(t.syncPeriodUs, MIN_PERIOD_US, DEFAULT_PERIOD_US);
}

void rcCycle(RcState& st, uint32_t& lastPingMs, uint32_t& lastBatteryMs) {
    const Settings s = Config::get();
    const uint32_t now = millis();

    while (Serial1.available()) crsf.processByte(Serial1.read(), now);
    const CRSFTelemetry& t = crsf.telemetry();
    setPeriod(wantedPeriod(t, now));

    uint16_t ch[CRSF_MAX_CHANNELS];
    for (auto& c : ch) c = CRSF_CHANNEL_VALUE_MID;
    for (uint8_t a = 0; a < AXIS_COUNT; a++) {
        st.raw[a] = s.model.simulation ? Inputs::simulate(a, now) : Inputs::readRaw(a);
        ch[s.model.channel[a]] = Inputs::toCrsf(st.raw[a], s.hw.cal[a], s.model.deadband, s.model.invert[a]);
    }
    const uint8_t sw = State::auxSwitches.load(std::memory_order_relaxed);
    for (uint8_t i = 0; i < AUX_COUNT; i++) {
        ch[AUX_FIRST_CHANNEL + i] = (sw >> i) & 1 ? CRSF_CHANNEL_VALUE_MAX : CRSF_CHANNEL_VALUE_MIN;
    }
    safety.apply(ch, s.model.channel[AXIS_THROTTLE], s.model);

    for (uint8_t i = 0; i < CRSF_MAX_CHANNELS; i++) crsf.setChannel(i, ch[i]);
    crsf.sendRCFrame(st.lastFrame);
    ElrsParams::poll(crsf, now);

    // Ask the module to identify itself; the reply proves the UART link works even without a receiver
    if (!t.deviceName[0] && now - lastPingMs >= PING_INTERVAL_MS) {
        lastPingMs = now;
        crsf.sendDevicePing();
    }

    if (now - lastBatteryMs >= BATTERY_INTERVAL_MS) {
        lastBatteryMs = now;
        const uint16_t mv = Battery::readMillivolts(s.hw.battRatioX1000);
        st.txBatteryMv = st.txBatteryMv ? (st.txBatteryMv * 7u + mv) / 8 : mv;
    }

    memcpy(st.channels, ch, sizeof(ch));
    st.frames++;
    st.periodUs = periodUs;
    st.safeStartHold = safety.holding();
    st.telem = t;
    State::publish(st);
}

void rcTask(void*) {
    RcState st = {};
    uint32_t lastPingMs = 0;
    uint32_t lastBatteryMs = 0;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        rcCycle(st, lastPingMs, lastBatteryMs);
    }
}

void readSerialCommands() {
    static String line;
    while (Serial.available()) {
        const char c = Serial.read();
        if (c == '\r' || c == '\n') {
            if (line.length()) Serial.println(Commands::execute(line));
            line = "";
        } else if (line.length() < 120) {
            line += c;
        }
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    const uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 2000) delay(10);

    Config::begin();
    Inputs::begin();
    UI::begin();
    Remote::begin();

    Serial1.setRxBufferSize(1024);
    Serial1.begin(CRSF_BAUDRATE, SERIAL_8N1, PIN_CRSF_RX, PIN_CRSF_TX, CRSF_INVERTED);
    crsf.begin(Serial1);
    ElrsParams::begin();
    crsf.setExtendedHandler(ElrsParams::onFrame);

#ifdef RUN_UNIT_TESTS
    CRSFTest::runAllTests();
#endif

    xTaskCreate(rcTask, "rc", 4096, nullptr, 10, &rcTaskHandle);
    esp_timer_create_args_t timerArgs = {};
    timerArgs.callback = onRcTimer;
    timerArgs.name = "rc";
    esp_timer_create(&timerArgs, &rcTimer);
    esp_timer_start_periodic(rcTimer, periodUs);

    Serial.println();
    Serial.println("=== ELRSTx " FW_VERSION " ===");
    Serial.printf("CRSF: %d baud, TX GPIO%d -> module RX, RX GPIO%d <- module TX\n", CRSF_BAUDRATE, PIN_CRSF_TX, PIN_CRSF_RX);
#if INPUTS_VIA_EXPANDER
    Serial.printf("Display: %s  Expander: %s\n", UI::displayOk() ? "ok" : "not found", UI::expanderOk() ? "ok" : "not found");
#else
    Serial.printf("Display: %s  Buttons/switches: GPIO\n", UI::displayOk() ? "ok" : "not found");
#endif
    Serial.println("Type 'help' for commands.");
}

void loop() {
    readSerialCommands();

    static uint32_t lastMonitorMs = 0;
    if (Commands::monitor && millis() - lastMonitorMs >= 1000) {
        lastMonitorMs = millis();
        Serial.println(Commands::monitorLine());
    }

    Remote::loop();
    UI::loop();
    delay(1);
}
