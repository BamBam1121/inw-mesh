// The T-Deck trackball, standing in for the pager's wheel. It is an optical
// ball with four pulse lines (one per direction) and a click on GPIO0. Rolling
// down or right counts forward, up or left counts back - what turning the wheel
// down does on the pager - so lists and the home carousel both follow the ball.
//
// Pulses are counted in interrupts so a quick roll isn't lost between loops.
// Each pulse is one step, as Meshtastic's T-Deck firmware does it.

#pragma once
#include <Arduino.h>
#include "board_pins.h"

// In trackball.cpp, so the interrupt handlers live in IRAM: a roll while flash
// is busy (a settings save) must not reach for code in flash.
namespace tdeck_tb {
void attach();
int32_t take();
void counts(uint32_t out[4]);   // pulses seen per line: up, down, left, right
}

class Rotary {
public:
  // The pins are the board's; the arguments only keep the pager's signature.
  void begin(uint8_t, uint8_t, uint8_t) {
    pinMode(PIN_TB_CLICK, INPUT_PULLUP);
    tdeck_tb::attach();
  }

  // Steps since the last call; down/right is positive.
  int8_t takeDetents() {
    const int32_t s = tdeck_tb::take();
    return (int8_t)constrain(_rev ? -s : s, -100, 100);
  }

  // A unit whose ball turns out backwards (or a screen turned upside down).
  void setReversed(bool r) { _rev = r; }

  // A click, reported when the ball is let go - so that holding it can mean
  // something else (takeLongPress) without also selecting what's under it.
  bool takePress() {
    poll();
    const bool c = _click;
    _click = false;
    return c;
  }

  // Held down for LONG_MS: the T-Deck's stand-in for the pager's side button
  // (lock and screen off). Once per hold.
  bool takeLongPress() {
    poll();
    const bool l = _long;
    _long = false;
    return l;
  }

  bool pressed() const { return _pressed; }

private:
  static constexpr uint32_t DEBOUNCE_MS = 25, LONG_MS = 900;

  void poll() {
    const bool down = digitalRead(PIN_TB_CLICK) == LOW;
    const uint32_t t = millis();
    if (down != _pressed && t - _pressChange >= DEBOUNCE_MS) {
      _pressChange = t;
      _pressed = down;
      if (!down && !_heldLong) _click = true;
      if (down) _heldLong = false;
    }
    if (_pressed && !_heldLong && t - _pressChange >= LONG_MS) { _heldLong = true; _long = true; }
  }

  bool     _pressed = false, _heldLong = false, _click = false, _long = false, _rev = false;
  uint32_t _pressChange = 0;
};
