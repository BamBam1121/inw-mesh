// Simulated battery: a healthy 87%, on battery.
#pragma once
#include <Arduino.h>
#include <Wire.h>
class Battery {
public:
  bool begin(TwoWire& = Wire) { return true; }
  bool present() const { return true; }
  bool hasReading() const { return true; }
  void tick(uint32_t) {}
  uint8_t percent() const { return pct; }
  uint8_t gaugePercent() const { return pct; }
  uint16_t millivolts() const { return 4020; }
  bool pluggedIn() const { return plugged; }
  bool pollVbus() { return plugged; }
  bool charging() const { return plugged && pct < 100; }
  float remainingMah() const { return -1; }
  void restoreMah(float, bool) {}
  uint16_t fullChargeMah() const { return 0; }
  uint16_t designNow() const { return 0; }
  bool configured() const { return false; }
  bool relearn() { return false; }
  bool shipMode() { return false; }
  void setHiZ(bool) {}
  void holdCharge(bool) {}
  bool chargeHeld() const { return false; }
  void report() {}
  void configReport() {}
  uint8_t pct = 87;
  bool plugged = false;
};
