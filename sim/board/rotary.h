// The T-Deck trackball's pulse counters, for the simulator (sim_stubs.cpp).
#pragma once
#include <stdint.h>
namespace tdeck_tb {
void counts(uint32_t out[4]);   // up, down, left, right
extern uint32_t simCounts[4];
}
