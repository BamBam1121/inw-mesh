// Shared between the simulator's files.
#pragma once
#include <stdint.h>
namespace sim {
extern uint32_t epoch;        // the clock the screens show
extern bool phoneLinked;
extern bool wifiOn;
void setMillis(uint32_t ms);
void advance(uint32_t ms);
}
