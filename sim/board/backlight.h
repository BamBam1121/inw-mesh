// Simulated backlight: remembers the level, lights nothing.
#pragma once
#include <Arduino.h>
class Backlight {
public:
  static constexpr uint8_t MAX_LEVEL = 16;
  void begin(uint8_t) { _level = MAX_LEVEL; }
  void setLevel(uint8_t l) { _level = l > MAX_LEVEL ? MAX_LEVEL : l; }
  uint8_t level() const { return _level; }
  void fadeTo(uint8_t t, uint16_t = 300) { setLevel(t); }
  void tick() {}
  bool fading() const { return false; }
  void setNow(uint8_t l) { setLevel(l); }
private:
  uint8_t _level = MAX_LEVEL;
};
#include "dimmer.h"
