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
void take(int32_t& dx, int32_t& dy);   // steps since the last call: right and down are positive
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
    int8_t dx, dy;
    takeXY(dx, dy);
    return (int8_t)constrain(dx + dy, -100, 100);
  }
  // The same, each way on its own: a screen laid out in rows and columns moves its
  // highlight the way the ball was rolled (View::roll).
  void takeXY(int8_t& dx, int8_t& dy) {
    int32_t x, y;
    tdeck_tb::take(x, y);
    // The ball's lines flip once for each small step of it. How many of those make one
    // step on the screen is the speed: every one (fast), two in three (medium) or every
    // other one (slow, what it did before both edges were counted). What is left over
    // is kept for the next roll, so a slow roll still gets there.
    _accX += (_rev ? -x : x) * _per6;
    _accY += (_rev ? -y : y) * _per6;
    const int32_t ox = _accX / 6, oy = _accY / 6;
    _accX -= ox * 6;
    _accY -= oy * 6;
    dx = (int8_t)constrain(ox, -50, 50);
    dy = (int8_t)constrain(oy, -50, 50);
  }
  // 0 slow, 1 medium, 2 fast (Settings > Display).
  void setSpeed(uint8_t s) { _per6 = s == 0 ? 3 : s == 2 ? 6 : 4; _accX = _accY = 0; }

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
  int32_t  _accX = 0, _accY = 0;          // steps not yet handed out, in sixths
  uint8_t  _per6 = 4;                     // sixths of a screen step for each step of the ball
  uint32_t _pressChange = 0;
};
