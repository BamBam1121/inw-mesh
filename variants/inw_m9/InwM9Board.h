#pragma once
#include <Wire.h>
#include <Arduino.h>
#include "helpers/ESP32Board.h"

// Elecrow ThinkNode M9 (LR1110) as a MeshCore board. Like the pager and the T-Deck, the
// app installs the battery reader (src/m9/battery.h does the ADC work).
class InwM9Board : public ESP32Board {
public:
  uint16_t (*battReader)() = nullptr;
  void begin() { ESP32Board::begin(); }
  uint16_t getBattMilliVolts() override { return battReader ? battReader() : 0; }
  // The LR1110's IRQ is DIO9 on the chip, wired to GPIO42 (MeshCore's M9 board answers
  // LORA_DIO0 = 41 here, which is BUSY; the radio is driven through its own IRQ pin anyway).
  const char* getManufacturerName() const override { return "Elecrow ThinkNode M9 (Squatch Mesh)"; }
};
