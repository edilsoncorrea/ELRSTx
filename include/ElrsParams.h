#pragma once
#include <Arduino.h>
#include "CRSF.h"

// ExpressLRS module configuration over CRSF parameter frames (what the ELRS Lua script uses).
// poll()/onFrame() run in the RC task, which owns the CRSF serial; everything else is for the loop task.
namespace ElrsParams {

enum Type : uint8_t {
    T_UINT8 = 0, T_INT8 = 1, T_UINT16 = 2, T_INT16 = 3, T_FLOAT = 8,
    T_SELECT = 9, T_STRING = 10, T_FOLDER = 11, T_INFO = 12, T_COMMAND = 13,
};

enum CmdStep : uint8_t {
    CMD_IDLE = 0, CMD_CLICK = 1, CMD_EXECUTING = 2, CMD_ASK_CONFIRM = 3,
    CMD_CONFIRM = 4, CMD_CANCEL = 5, CMD_QUERY = 6,
};

constexpr uint8_t MAX_PARAMS = 64;

struct Param {
    uint8_t id;
    uint8_t parent;
    uint8_t type;
    bool hidden;
    bool loaded;
    char name[28];
    char text[112];   // select options (';'-separated), string/info value or command info
    char units[8];
    int16_t value;    // select index, number, or command step
    int16_t min;
    int16_t max;
    uint8_t timeout;  // command poll interval, 10 ms units
};

struct Status {
    char device[28];
    uint8_t fieldCount;
    uint8_t loadedCount;
    bool loading;
    bool statusValid;
    uint8_t badPackets;
    uint16_t goodPackets;
    uint8_t flags;
    char message[28];
};

void begin();

// RC task
void onFrame(uint8_t type, const uint8_t* payload, uint8_t len, uint32_t nowMs);
void poll(CRSF& crsf, uint32_t nowMs);

// Loop task. touch() keeps the session alive; a new session reloads all parameters.
void touch();
void reload();
Status status();
bool param(uint8_t id, Param& out);
uint8_t children(uint8_t folder, uint8_t* ids, uint8_t maxIds);
void setValue(uint8_t id, uint8_t value);
void command(uint8_t id, CmdStep step);
String valueText(const Param& p, int16_t value);
int16_t nextValue(const Param& p, int16_t from, int dir);
int16_t findOption(const Param& p, const String& label);
}
