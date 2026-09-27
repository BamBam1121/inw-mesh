// The T-Deck's touch chip for the simulator: always found. sim_main sets how many
// touches it has seen; the finger itself comes from sim_main's tap() and swipe().
#pragma once
#include <stdint.h>
#include "board_pins.h"

class TwoWire;
class TouchPanel;
extern TouchPanel touchPanel;   // sim_stubs.cpp

class TouchPanel {
public:
  bool begin(TwoWire&) { return true; }
  bool ok() const { return true; }
  uint8_t address() const { return 0x5D; }
  uint16_t rawX() const { return 118; }
  uint16_t rawY() const { return 171; }
  uint32_t touches() const { return count; }
  uint16_t resX() const { return 240; }
  uint16_t resY() const { return 320; }
  void setMirror(bool, bool) {}
  void poll(bool& down, int16_t& x, int16_t& y) { down = false; x = y = 0; }
  uint32_t count = 0;
};
