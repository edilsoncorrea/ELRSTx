#include "State.h"

namespace {
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
RcState shared = {};
}

std::atomic<uint8_t> State::auxSwitches{0};
std::atomic<uint16_t> State::buttonAdc{4095};

void State::publish(const RcState& s) {
    portENTER_CRITICAL(&mux);
    shared = s;
    portEXIT_CRITICAL(&mux);
}

RcState State::snapshot() {
    portENTER_CRITICAL(&mux);
    RcState s = shared;
    portEXIT_CRITICAL(&mux);
    return s;
}

bool State::fresh(uint32_t lastMs, uint32_t nowMs, uint32_t maxAgeMs) {
    return lastMs != 0 && nowMs - lastMs < maxAgeMs;
}

bool State::moduleAlive(const RcState& s, uint32_t nowMs) {
    return fresh(s.telem.lastFrameMs, nowMs, 1500);
}

bool State::linkUp(const RcState& s, uint32_t nowMs, uint16_t timeoutMs) {
    return fresh(s.telem.lastLinkMs, nowMs, timeoutMs) && s.telem.uplinkLq > 0;
}

bool State::syncActive(const RcState& s, uint32_t nowMs) {
    return fresh(s.telem.lastSyncMs, nowMs, 2000) && s.telem.syncPeriodUs > 0;
}
