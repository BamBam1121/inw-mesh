// Simulated speaker: silent.
#pragma once
#include <Arduino.h>
#include <Wire.h>
class Es8311 {
public:
  static constexpr uint32_t SAMPLE_RATE = 16000;
  static constexpr uint32_t QUEUE_MS = 256;
  bool begin(TwoWire& = Wire) { return true; }
  bool ok() const { return true; }
  bool start() { return true; }
  void stop() {}
  void setMute(bool) {}
  void setVolumePercent(uint8_t) {}
  uint8_t reg(uint8_t) { return 0; }
  void write(const int16_t*, size_t) {}
};
