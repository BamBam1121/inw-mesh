#pragma once
#include <Wire.h>
#include <Arduino.h>
#include "helpers/ESP32Board.h"

// LilyGo T-Deck / T-Deck Plus (SX1262) as a MeshCore board. Like the pager, the
// app installs the battery reader (src/tdeck/battery.h does the ADC work).
class InwTDeckBoard : public ESP32Board {
public:
  uint16_t (*battReader)() = nullptr;
  void begin() { ESP32Board::begin(); }
  uint16_t getBattMilliVolts() override { return battReader ? battReader() : 0; }
  const char* getManufacturerName() const override { return "LilyGo T-Deck (Squatch Mesh)"; }
};
