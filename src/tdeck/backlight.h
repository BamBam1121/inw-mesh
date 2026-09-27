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
        const uint8_t steps = target > _level ? target - _level : _level - target;
        _fadeTo   = target;
        _stepMs   = durationMs / steps;
        _lastStep = millis();
        _fading   = true;
    }

    void tick() {
        if (!_fading) return;
        if (millis() - _lastStep < _stepMs) return;
        _lastStep = millis();
        setLevel(_level < _fadeTo ? _level + 1 : _level - 1);
        if (_level == _fadeTo) _fading = false;
    }

    bool fading() const { return _fading; }
    void setNow(uint8_t level) { _fading = false; setLevel(level); }

private:
    static constexpr uint8_t CHANNEL = 6;   // clear of the channels the core hands out first
    uint8_t  _pin = 0;
    uint8_t  _level = 0;
    bool     _fading = false;
    uint8_t  _fadeTo = 0;
    uint32_t _lastStep = 0, _stepMs = 1;
};

#include "dimmer.h"   // the idle dimmer (shared by every board) drives this
