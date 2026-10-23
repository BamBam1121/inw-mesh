// The M9 has no trackball: the arrow keys stand in for it (rotary.h), so the T-Deck's
// pulse counters are only kept for the hardware check, always zero.
#include <Arduino.h>
#include "board_pins.h"

namespace tdeck_tb {
void attach() {}
void take(int32_t& dx, int32_t& dy) { dx = dy = 0; }
void counts(uint32_t out[4]) { for (int i = 0; i < 4; i++) out[i] = 0; }
}
