// No vibration motor on the T-Deck. Same interface as the pager's DRV2605
// driver, always absent, so alerts fall back to sound and the screen.

#pragma once
#include <Arduino.h>
#include <Wire.h>

class Haptic {
public:
  static constexpr uint8_t CONFIG_COUNT = 1;
  bool begin(TwoWire& = Wire) { return false; }
  bool ok() const { return false; }
  void setMode(uint8_t) {}
  uint8_t mode() const { return 0; }
  static const char* modeName(uint8_t) { return "none"; }
  void pattern(const uint8_t*, uint8_t) {}
  void setPattern(const uint8_t*, uint8_t) {}
  void setTick(uint8_t, uint8_t) {}
  void buzz(uint8_t = 3) {}
  void tick() {}
};
