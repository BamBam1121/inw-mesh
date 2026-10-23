// A flat cell can't feed the radio and Wi-Fi: starting them sags it into a brownout
// reset, the T-Deck starts again, and round it goes, running the cell down further
// each turn (reports from two T-Decks sitting at 2.4-2.7 V, 2026-09-30). So below
// 3.3 V - the bottom of the percentage curve - nothing more is started: it says why,
// sleeps, and looks again every few minutes, which is enough for a charger to lift it.
//
// Once it is waiting, or after a brownout, it wants more than 3.3 V: the reading at
// start is taken with almost nothing drawing, and a flat cell rests higher than it
// works. The slide switch off and on forgets the wait (and the brownout), so a
// T-Deck can never be stuck here on a wrong reading; holding the trackball through
// the message starts it too.
//
// Only the decision is here, so a PC can test it (sim/test_flat.cpp).
#pragma once
#include <stdint.h>

struct FlatHold {
  static constexpr uint16_t NO_CELL_MV = 1500;          // under this there is no cell: USB alone is running it
  static constexpr uint16_t START_MV = 3300;            // a fresh start needs this much
  static constexpr uint16_t LEAVE_MV = 3500;            // once waiting, this much to start
  static constexpr uint16_t AFTER_BROWNOUT_MV = 3600;   // straight after the power sagged
  static constexpr uint32_t LOOK_EVERY_S = 180;

  // What this start needs. waiting: it was put to sleep by this rule and has not started since.
  static uint16_t need(bool waiting, bool afterBrownout) {
    return waiting ? LEAVE_MV : afterBrownout ? AFTER_BROWNOUT_MV : START_MV;
  }

  // True: do not start the radio, Wi-Fi and the rest; say so and sleep.
  static bool hold(uint16_t mv, bool waiting, bool afterBrownout) {
    if (mv < NO_CELL_MV) return false;
    return mv < need(waiting, afterBrownout);
  }
};
