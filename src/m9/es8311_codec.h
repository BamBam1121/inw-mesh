// M9 sound: a buzzer behind a transistor on GPIO9, no codec and no I2S. It keeps the
// name and interface of the pager's ES8311 driver so the jingle player works
// unchanged: the player's samples are played out as PWM duty (a 1-bit DAC), one
// sample every 62.5 us at 16 kHz, on the player's own task. A buzzer smooths the
// carrier away, so tunes come out as tunes, rougher than a speaker.
// Nobody has heard this on a real M9 yet.

#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <esp_timer.h>
#include "board_pins.h"

class Es8311 {
public:
  static constexpr uint32_t SAMPLE_RATE = 16000;
  static constexpr uint32_t QUEUE_MS = 0;    // nothing queued: write() plays as it goes

  bool begin(TwoWire& = Wire) {
    pinMode(PIN_M9_BUZZER, OUTPUT);
    digitalWrite(PIN_M9_BUZZER, LOW);
    _ok = true;
    return true;
  }
  bool ok() const { return _ok; }

  bool start() {
    if (!_ok) return false;
    ledcSetup(CHANNEL, 156250, 8);            // 80 MHz / 512: well above hearing, 8-bit duty
    ledcAttachPin(PIN_M9_BUZZER, CHANNEL);
    ledcWrite(CHANNEL, 0);
    return true;
  }

  void stop() {
    ledcWrite(CHANNEL, 0);
    ledcDetachPin(PIN_M9_BUZZER);
    digitalWrite(PIN_M9_BUZZER, LOW);
  }

  void setMute(bool m) { _muted = m; }

  void setVolumePercent(uint8_t pct) {
    if (pct > 100) pct = 100;
    _gain = (uint16_t)((uint32_t)pct * pct * 256 / 10000);   // 0..256
  }

  uint8_t reg(uint8_t) { return 0; }

  // Each sample sets the duty, then waits out its 62.5 us on the clock.
  void write(const int16_t* mono, size_t n) {
    int64_t at = esp_timer_get_time();
    for (size_t i = 0; i < n; i++) {
      int32_t s = _muted ? 0 : (int32_t)mono[i] * _gain / 256;   // -32768..32767
      int32_t duty = 128 + s / 256;
      if (duty < 0) duty = 0;
      if (duty > 255) duty = 255;
      ledcWrite(CHANNEL, (uint32_t)duty);
      at += 1000000 / SAMPLE_RATE;
      while (esp_timer_get_time() < at) {}
    }
  }

private:
  static constexpr uint8_t CHANNEL = 5;     // clear of the backlight's 6
  bool     _ok = false;
  bool     _muted = true;
  uint16_t _gain = 92;
};
