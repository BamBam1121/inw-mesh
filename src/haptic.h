// DRV2605 driving an ERM motor (not an LRA: the LRA library barely moves it).
// Default mode 5 = ERM library 1, Strong Buzz, clamps wide open. Notifications
// use the chip's own sequencer; the scroll tick is one Soft Bump at a quarter
// overdrive so the mass doesn't ring.

#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "board_pins.h"

class Haptic {
public:
  static constexpr uint8_t CONFIG_COUNT = 6;

  bool begin(TwoWire& w = Wire) {
    _w = &w;
    _w->beginTransmission(ADDR_DRV2605_HAPT);
    _ok = _w->endTransmission() == 0;
    if (_ok) apply();
    return _ok;
  }
  bool ok() const { return _ok; }

  void setMode(uint8_t m) { _mode = m % CONFIG_COUNT; if (_ok) apply(); }
  uint8_t mode() const { return _mode; }
  static const char* modeName(uint8_t m) {
    static const char* N[CONFIG_COUNT] = {
      "1 soft (lra lib)", "2 erm buzz long", "3 erm strong", "4 erm strong+",
      "5 erm long max", "6 erm strong max",
    };
    return N[m % CONFIG_COUNT];
  }

  // Play a theme's sequence: effect ids, with bit 7 set meaning a pause of
  // (v & 0x7F) x 10 ms. Up to 8 slots, one GO write.
  void pattern(const uint8_t* seq, uint8_t n) {
    if (!_ok || !n) return;
    write(REG_ODCLAMP, _odClamp);
    uint8_t slot = REG_SEQ;
    for (uint8_t i = 0; i < n && slot <= 0x0B; i++) write(slot++, seq[i]);
    if (slot <= 0x0B) write(slot, 0);
    write(REG_MODE, 0);
    write(REG_GO, 1);
  }

  // The theme's default notification pattern, used by buzz().
  void setPattern(const uint8_t* seq, uint8_t n) { _pat = seq; _patN = n; }
  void setTick(uint8_t effect, uint8_t clamp) { _tickEffect = effect; _tickClamp = clamp; }

  // Notification: the theme pattern if one is set, else `pulses` strong buzzes.
  void buzz(uint8_t pulses = 3) {
    if (!_ok) return;
    if (_pat && _patN && pulses == 3) { pattern(_pat, _patN); return; }
    write(REG_ODCLAMP, _odClamp);          // the tick lowers it; put it back
    uint8_t slot = REG_SEQ;
    for (uint8_t i = 0; i < pulses && slot <= 0x0B; i++) {
      write(slot++, _effect);
      if (i + 1 < pulses && slot <= 0x0B) write(slot++, 0x80 | 22);   // 220 ms
    }
    if (slot <= 0x0B) write(slot, 0);
    write(REG_MODE, 0);
    write(REG_GO, 1);
  }

  // One small bump per detent/key. Rate-limited only as a bus guard.
  void tick() {
    if (!_ok) return;
    const uint32_t now = millis();
    if (now - _lastTick < 30) return;
    _lastTick = now;
    write(REG_ODCLAMP, _tickClamp);
    write(REG_SEQ, _tickEffect);
    write(REG_SEQ + 1, 0);
    write(REG_MODE, 0);
    write(REG_GO, 1);
  }

private:
  enum : uint8_t { REG_MODE = 0x01, REG_LIB = 0x03, REG_SEQ = 0x04, REG_GO = 0x0C,
                   REG_RATEDV = 0x16, REG_ODCLAMP = 0x17, REG_FEEDBACK = 0x1A, REG_CTRL3 = 0x1D };

  void erm() {
    uint8_t fb = read(REG_FEEDBACK);
    if (fb == 0xFF) fb = 0x36;
    write(REG_FEEDBACK, fb & 0x7F);        // N_ERM_LRA = 0: ERM
  }

  void apply() {
    write(REG_MODE, 0);
    switch (_mode) {
      case 0: write(REG_LIB, 6); write(REG_CTRL3, 0x88); _effect = 47; break;
      case 1: erm(); write(REG_LIB, 1); write(REG_CTRL3, 0x88); _effect = 48; break;
      case 2: erm(); write(REG_LIB, 1); write(REG_CTRL3, 0x88); _effect = 14; break;
      case 3: erm(); write(REG_LIB, 1); write(REG_CTRL3, 0x88); write(REG_RATEDV, 0xC0); write(REG_ODCLAMP, 0xC0); _effect = 14; break;
      case 4: erm(); write(REG_LIB, 1); write(REG_CTRL3, 0x88); write(REG_RATEDV, 0xFF); write(REG_ODCLAMP, 0xFF); _effect = 48; break;
      default: erm(); write(REG_LIB, 1); write(REG_CTRL3, 0x88); write(REG_RATEDV, 0xFF); write(REG_ODCLAMP, 0xFF); _effect = 14; break;
    }
    const uint8_t od = read(REG_ODCLAMP);
    _odClamp = od == 0xFF && _mode < 4 ? 0x89 : od;
  }

  bool write(uint8_t reg, uint8_t v) {
    _w->beginTransmission(ADDR_DRV2605_HAPT);
    _w->write(reg); _w->write(v);
    return _w->endTransmission() == 0;
  }
  uint8_t read(uint8_t reg) {
    _w->beginTransmission(ADDR_DRV2605_HAPT);
    _w->write(reg);
    if (_w->endTransmission(false) != 0) return 0xFF;
    if (_w->requestFrom((uint8_t)ADDR_DRV2605_HAPT, (uint8_t)1) != 1) return 0xFF;
    return _w->read();
  }

  TwoWire* _w = nullptr;
  bool     _ok = false;
  uint8_t  _mode = 5, _effect = 14, _odClamp = 0xFF;
  uint32_t _lastTick = 0;
  const uint8_t* _pat = nullptr;
  uint8_t  _patN = 0, _tickEffect = 7, _tickClamp = 0x40;
};
