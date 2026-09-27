// The simulator's "display": a panel that draws nowhere but has the board's size,
// as the real one does after setRotation (the carousel sizes itself from it).
// Every screen is rendered into a sprite (the firmware's own canvas) and saved.
#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "board_pins.h"
#ifndef TFT_ROTATION
#define TFT_ROTATION 0
#endif

// Panel_NULL ignores rotation, so its size would stay 0x0.
struct SimPanel : public lgfx::Panel_NULL {
  SimPanel() { _width = SCREEN_W; _height = SCREEN_H; }
  void setRotation(uint_fast8_t) override { _width = SCREEN_W; _height = SCREEN_H; }
};

class LGFX : public lgfx::LGFX_Device {
  SimPanel _panel;
public:
  LGFX() { setPanel(&_panel); }
};
