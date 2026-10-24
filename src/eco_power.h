#pragma once
#include "power.h"
#include "cpu_sleep.h"

namespace power {
// The existing CPU preferences remain the source of truth across upgrades.
inline bool eco() { return cpuSaving() && lightSleep(); }
bool setEco(bool on);
// Fold the previous independent NFC option into ECO once at startup.
bool initEco();
}
