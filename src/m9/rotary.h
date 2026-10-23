// The M9 has no wheel and no trackball: its four arrow keys stand in for the
// T-Deck's trackball (one step per press), and the keyboard's Home key for holding
// the trackball down (lock and screen off; main.cpp also wakes on it). The keys
// arrive through keyboard.h, which main.cpp reads every loop.

#pragma once
#include <Arduino.h>
#include "board_pins.h"
#include "keyboard.h"

namespace tdeck_tb {
void attach();
void take(int32_t& dx, int32_t& dy);
void counts(uint32_t out[4]);
}

class Rotary {
public:
  void begin(uint8_t, uint8_t, uint8_t) {}

  int8_t takeDetents() {
    int8_t dx, dy;
    takeXY(dx, dy);
    return (int8_t)constrain(dx + dy, -100, 100);
  }
  // Arrow presses since the last call; right and down are positive.
  void takeXY(int8_t& dx, int8_t& dy) {
    m9kb::Nav& n = m9kb::nav();
    dx = _rev ? -n.dx : n.dx;
    dy = _rev ? -n.dy : n.dy;
    n.dx = n.dy = 0;
  }
  void setSpeed(uint8_t) {}            // a key press is always one step
  void setReversed(bool r) { _rev = r; }

  // No click to read: Enter does the pressing (main.cpp, BOARD_KEYS_ONLY).
  bool takePress() { return false; }

  // The Home key: lock and screen off, or wake.
  bool takeLongPress() {
    m9kb::Nav& n = m9kb::nav();
    const bool h = n.home;
    n.home = false;
    return h;
  }

  bool pressed() const { return false; }

private:
  bool _rev = false;
};
