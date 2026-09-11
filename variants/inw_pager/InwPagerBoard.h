#pragma once
#include <Wire.h>
#include <Arduino.h>
#include "helpers/ESP32Board.h"

// LilyGo T-Lora Pager (SX1262) as a MeshCore board.
//
// The battery is on a BQ27220 fuel gauge that our app already drives, so the
// app installs a reader here rather than MeshCore guessing at an ADC divider.
class InwPagerBoard : public ESP32Board {
public:
  uint16_t (*battReader)() = nullptr;
  void begin() { ESP32Board::begin(); }
  uint16_t getBattMilliVolts() override { return battReader ? battReader() : 0; }
  const char* getManufacturerName() const override { return "LilyGo T-Lora Pager (INW)"; }
};
