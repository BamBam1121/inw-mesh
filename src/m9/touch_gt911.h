// The M9 has no touch panel. Same interface as the T-Deck's GT911 driver, always
// absent, for the shared code that names it (main.cpp's start-up and test commands);
// the touch input itself is only compiled in where BOARD_HAS_TOUCH says so.
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "board_pins.h"

class TouchPanel;
extern TouchPanel touchPanel;   // main.cpp

class TouchPanel {
public:
  bool begin(TwoWire&) { return false; }
  bool second(int16_t&, int16_t&) const { return false; }
  uint8_t maxTouches() const { return 0; }
  void setMirror(bool, bool) {}
  void poll(bool& down, int16_t& x, int16_t& y) { down = false; x = y = 0; }
  bool ok() const { return false; }
  uint32_t touches() const { return 0; }
  uint8_t address() const { return 0; }
  uint16_t rawX() const { return 0; }
  uint16_t rawY() const { return 0; }
  uint16_t resX() const { return 0; }
  uint16_t resY() const { return 0; }
};
