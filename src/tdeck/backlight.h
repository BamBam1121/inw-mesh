// T-Deck backlight: ordinary PWM on GPIO42 (Wadamesh drives it at 20 kHz, 8-bit).
// Same interface as the pager's AW9364 version - 16 levels plus off, with fades -
// so the idle dimmer and the brightness setting work unchanged.

#pragma once
#include <Arduino.h>

class Backlight {
public:
    static constexpr uint8_t MAX_LEVEL = 16;   // 0 is off, 1 dimmest, 16 brightest

    void begin(uint8_t pin) {
        _pin = pin;
        ledcSetup(CHANNEL, 20000, 8);
        ledcAttachPin(_pin, CHANNEL);
        _level = 0;
        setLevel(MAX_LEVEL);
    }

    void setLevel(uint8_t level) {
        if (level > MAX_LEVEL) level = MAX_LEVEL;
        if (level == _level) return;
        // Roughly even to the eye: the steps are squared, with a floor that still glows.
        ledcWrite(CHANNEL, level ? 6 + (uint32_t)249 * level * level / (MAX_LEVEL * MAX_LEVEL) : 0);
        _level = level;
    }

    uint8_t level() const { return _level; }

    void fadeTo(uint8_t target, uint16_t durationMs = 300) {
        if (target > MAX_LEVEL) target = MAX_LEVEL;
        if (target == _level) { _fading = false; return; }
        _fadeFrom = _level;
        _fadeTo   = target;
        _fadeMs   = durationMs ? durationMs : 1;
        _fadeAt   = millis();
        _fading   = true;
    }

    // By the clock: where the fade should be by now, however long it has been since the
    // last call. It used to move one step a call, so a fade that should take 0.3 s took
    // as long as twelve passes of the loop did - seconds, when a screen change or a slow
    // picture held each pass up, which is the screen "slowly fading on" after an alert.
    void tick() {
        if (!_fading) return;
        const uint32_t el = millis() - _fadeAt;
        if (el >= _fadeMs) { setLevel(_fadeTo); _fading = false; return; }
        setLevel((uint8_t)((int)_fadeFrom + ((int)_fadeTo - (int)_fadeFrom) * (int)el / (int)_fadeMs));
    }

    bool fading() const { return _fading; }
    // The fade under way starts over from now: for a wake, whose fade was asked for
    // before the panel had its picture.
    void restartFade() { if (_fading) _fadeAt = millis(); }
    void setNow(uint8_t level) { _fading = false; setLevel(level); }

private:
    static constexpr uint8_t CHANNEL = 6;   // clear of the channels the core hands out first
    uint8_t  _pin = 0;
    uint8_t  _level = 0;
    bool     _fading = false;
    uint8_t  _fadeTo = 0, _fadeFrom = 0;
    uint32_t _fadeAt = 0, _fadeMs = 1;
};

#include "dimmer.h"   // the idle dimmer (shared by every board) drives this
