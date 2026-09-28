// T-Deck touchscreen: a GT911 capacitive controller on the I2C bus (0x5D, or 0x14
// on some units), shared with the keyboard's controller.
//
// What's here was learned on real T-Decks by Wadamesh (their TDeckTouch.cpp):
// - status at 0x814E (bit 7: a new frame, low nibble: how many touches), the
//   first touch at 0x8150 (x lo, x hi, y lo, y hi), and the frame is acknowledged
//   by writing 0 back to 0x814E;
// - the GT911 reports in the panel's portrait frame. In the landscape the UI runs
//   (keyboard at the bottom) screen x = its y, and screen y = 239 - its x;
// - a reset in the middle of an I2C transfer (a reflash) can wedge it into
//   repeating one "pressed" frame, or lose the frame that says the finger lifted.
//   Left alone that is a press that never ends and a screen that ignores fingers,
//   until a power cycle. So a press is let go when frames stop coming, when the
//   status read keeps failing, or when one exact point is held for 10 s (a real
//   finger always jitters).
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "board_pins.h"

class TouchPanel;
extern TouchPanel touchPanel;   // main.cpp

class TouchPanel {
public:
  bool begin(TwoWire& w) {
    _w = &w;
    _w->setTimeOut(20);                    // a silent controller must not stall the loop
    for (uint8_t a : { (uint8_t)ADDR_GT911_A, (uint8_t)ADDR_GT911_B }) {
      _w->beginTransmission(a);
      if (_w->endTransmission() == 0) { _addr = a; break; }
    }
    if (!_addr) return false;
    uint8_t pid[4] = {0};
    if (!rd(0x8140, pid, 4) || pid[0] != '9') { _addr = 0; return false; }   // product id "911"
    // The range it reports in, from its config: 240 x 320 on every T-Deck so far,
    // which is what the mapping below assumes. Scaled if a unit says otherwise.
    uint8_t res[4];
    if (rd(0x8048, res, 4)) {
      const uint16_t w = res[0] | (res[1] << 8), h = res[2] | (res[3] << 8);
      if (w >= 100 && w <= 4096 && h >= 100 && h <= 4096) { _resX = w; _resY = h; }
    }
    if (PIN_TOUCH_INT >= 0) pinMode(PIN_TOUCH_INT, INPUT);
    _ok = true;
    return true;
  }
  bool ok() const { return _ok; }
  uint8_t address() const { return _addr; }
  uint16_t rawX() const { return _lrx; }      // the last reading, before mapping
  uint16_t rawY() const { return _lry; }
  uint32_t touches() const { return _count; }
  uint16_t resX() const { return _resX; }     // the range it reports in (its portrait frame)
  uint16_t resY() const { return _resY; }
  // Settings > Display: a panel that comes out mirrored, or a screen turned over.
  void setMirror(bool mx, bool my) { _mx = mx; _my = my; }

  // The finger now: down, and where in screen pixels. Cheap to call every loop;
  // the chip is read at most every POLL_MS.
  void poll(bool& down, int16_t& x, int16_t& y) {
    if (_ok) read();
    down = _down; x = _x; y = _y;
  }

private:
  static constexpr uint32_t POLL_MS = 12;

  void read() {
    const uint32_t now = millis();
    if (now - _at < POLL_MS) return;
    _at = now;
    uint8_t st;
    if (!rd(0x814E, &st, 1)) {
      if (_down && ++_fails >= 8) _down = false;   // the bus died mid-press
      return;
    }
    _fails = 0;
    if (!(st & 0x80)) {                           // nothing new
      // The lift frame was lost: a quarter second AND several reads with nothing.
      // Time alone isn't enough - after a slow loop (a map tile, a full redraw)
      // the first read finds nothing yet, and letting go there split one scroll
      // into two touches, the second of which could end as a tap and open a row.
      if (_down && ++_empty >= 4 && now - _frameAt > 250) _down = false;
      return;
    }
    _frameAt = now;
    _empty = 0;
    uint8_t p[4];
    if ((st & 0x0F) && rd(0x8150, p, 4)) {
      const uint16_t rx = p[0] | (p[1] << 8), ry = p[2] | (p[3] << 8);
      _lrx = rx; _lry = ry;
      if (_phantom && rx == _prx && ry == _pry) { wr(0x814E, 0); return; }   // still the frozen frame
      _phantom = false;
      if (!_down) { _pressAt = now; _prx = rx; _pry = ry; _jitter = false; }
      else if (rx != _prx || ry != _pry) _jitter = true;
      if (_down && !_jitter && now - _pressAt > 10000) {
        _phantom = true;                          // 10 s frozen on one point: not a finger
        _down = false;
        wr(0x814E, 0);
        return;
      }
      _x = (int16_t)constrain((int)ry * SCREEN_W / _resY, 0, SCREEN_W - 1);
      _y = (int16_t)constrain(SCREEN_H - 1 - (int)rx * SCREEN_H / _resX, 0, SCREEN_H - 1);
      if (_mx) _x = SCREEN_W - 1 - _x;
      if (_my) _y = SCREEN_H - 1 - _y;
      if (!_down) _count++;
      _down = true;
    } else {
      _down = false;                              // a real lift
      _phantom = false;
    }
    wr(0x814E, 0);                                // acknowledge the frame
  }

  bool rd(uint16_t reg, uint8_t* buf, uint8_t n) {
    _w->beginTransmission(_addr);
    _w->write((uint8_t)(reg >> 8));
    _w->write((uint8_t)reg);
    if (_w->endTransmission(false) != 0) return false;
    if (_w->requestFrom(_addr, n) != n) return false;
    for (uint8_t i = 0; i < n; i++) buf[i] = (uint8_t)_w->read();
    return true;
  }
  void wr(uint16_t reg, uint8_t v) {
    _w->beginTransmission(_addr);
    _w->write((uint8_t)(reg >> 8));
    _w->write((uint8_t)reg);
    _w->write(v);
    _w->endTransmission();
  }

  TwoWire* _w = nullptr;
  uint8_t  _addr = 0, _fails = 0, _empty = 0;
  bool     _ok = false, _down = false, _phantom = false, _jitter = false, _mx = false, _my = false;
  uint32_t _count = 0;
  int16_t  _x = 0, _y = 0;
  uint16_t _prx = 0, _pry = 0, _lrx = 0, _lry = 0;
  uint16_t _resX = SCREEN_H, _resY = SCREEN_W;
  uint32_t _at = 0, _frameAt = 0, _pressAt = 0;
};
