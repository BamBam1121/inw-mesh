// Rotary encoder, counted by the ESP32-S3 pulse counter (x4 decoding, four
// counts per detent). A GPIO interrupt lost edges whenever SPIFFS had the flash
// cache disabled; the PCNT keeps counting regardless.

#pragma once
#include <Arduino.h>
#include <driver/pcnt.h>

class Rotary {
public:
  void begin(uint8_t pinA, uint8_t pinB, uint8_t pinPress) {
    _press = pinPress;
    pinMode(pinA, INPUT_PULLUP);
    pinMode(pinB, INPUT_PULLUP);
    pinMode(_press, INPUT_PULLUP);

    pcnt_config_t c = {};
    c.pulse_gpio_num = pinA;
    c.ctrl_gpio_num  = pinB;
    c.channel        = PCNT_CHANNEL_0;
    c.unit           = UNIT;
    c.pos_mode       = PCNT_COUNT_INC;
    c.neg_mode       = PCNT_COUNT_DEC;
    c.lctrl_mode     = PCNT_MODE_REVERSE;
    c.hctrl_mode     = PCNT_MODE_KEEP;
    c.counter_h_lim  = 30000;
    c.counter_l_lim  = -30000;
    pcnt_unit_config(&c);

    c.pulse_gpio_num = pinB;
    c.ctrl_gpio_num  = pinA;
    c.channel        = PCNT_CHANNEL_1;
    c.pos_mode       = PCNT_COUNT_DEC;
    c.neg_mode       = PCNT_COUNT_INC;
    pcnt_unit_config(&c);

    pcnt_set_filter_value(UNIT, 1000);   // ~12.5 us: kills contact chatter
    pcnt_filter_enable(UNIT);
    pcnt_counter_pause(UNIT);
    pcnt_counter_clear(UNIT);
    pcnt_counter_resume(UNIT);
  }

  // Detents since the last call; turning down is positive.
  int8_t takeDetents() {
    int16_t now = 0;
    pcnt_get_counter_value(UNIT, &now);
    _acc += now - _last;
    _last = now;
    if (now > 20000 || now < -20000) {    // stay far from the hardware limit
      pcnt_counter_clear(UNIT);
      _last = 0;
    }
    const int32_t d = _acc / 4;
    _acc -= d * 4;
    return (int8_t)constrain(d, -100, 100);
  }

  // One event per press. Debounced against contact bounce on the switch.
  bool takePress() {
    const bool down = digitalRead(_press) == LOW;
    const uint32_t t = millis();
    if (down == _pressed || t - _pressChange < DEBOUNCE_MS) return false;
    _pressChange = t;
    _pressed = down;
    return down;
  }

  bool pressed() const { return _pressed; }

private:
  static constexpr pcnt_unit_t UNIT = PCNT_UNIT_0;
  static constexpr uint32_t DEBOUNCE_MS = 25;
  uint8_t  _press = 0;
  int32_t  _last = 0, _acc = 0;
  bool     _pressed = false;
  uint32_t _pressChange = 0;
};
