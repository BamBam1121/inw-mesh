// The panel backlight is an AW9364 on GPIO42, not a PWM LED: 16 current steps,
// selected by counting LOW/HIGH pulses. LOW held = off, LOW->HIGH = full, each
// further pulse steps down one (wrapping 1 -> 16). Pulses must stay well under
// the ~2.5 ms the chip treats as shutdown.

#pragma once
#include <Arduino.h>

class Backlight {
public:
    static constexpr uint8_t MAX_LEVEL = 16;   // 0 is off, 1 dimmest, 16 brightest

    void begin(uint8_t pin) {
        _pin = pin;
        pinMode(_pin, OUTPUT);
        // LOW->HIGH lands on level 16, so the panel is lit even if boot stalls
        // before the dimmer's first tick.
        digitalWrite(_pin, HIGH);
        _level = MAX_LEVEL;
    }

    void setLevel(uint8_t level) {
        if (level > MAX_LEVEL) level = MAX_LEVEL;
        if (level == _level) return;

        if (level == 0) { digitalWrite(_pin, LOW); _level = 0; return; }

        // Coming back from off lands on 16, so the pulse count below starts there.
        if (_level == 0) { digitalWrite(_pin, HIGH); _level = MAX_LEVEL; }

        for (uint8_t i = pulsesTo(level); i > 0; i--) {
            digitalWrite(_pin, LOW);
            digitalWrite(_pin, HIGH);
        }
        _level = level;
    }

    uint8_t level() const { return _level; }

    // Non-blocking fade: one level per step, since 16 levels is all there is.
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

private:
    // Stepping only goes down, so climbing means wrapping through 16.
    uint8_t pulsesTo(uint8_t target) const {
        return (uint8_t)((MAX_LEVEL + _level - target) % MAX_LEVEL);
    }

    uint8_t  _pin = 0;
    uint8_t  _level = 0;
    bool     _fading = false;
    uint8_t  _fadeTo = 0;
    uint32_t _lastStep = 0, _stepMs = 1;
};

// Idle auto-dim: full brightness on activity, dim after `idleMs`, off after
// `sleepMs`. Feed it activity (a key, the rotary) via note().
class IdleDimmer {
public:
    void begin(Backlight* bl, uint8_t full = 12, uint8_t dim = 3,
               uint32_t idleMs = 15000, uint32_t sleepMs = 60000) {
        _bl = bl; _full = full; _dim = dim; _idleMs = idleMs; _sleepMs = sleepMs;
        _lastActivity = millis();
        _state = FULL; _bl->fadeTo(_full, 250);
    }

    void note() {
        _lastActivity = millis();
        if (_state != FULL) { _state = FULL; _bl->fadeTo(_full, 150); }
    }

    void tick() {
        _bl->tick();
        const uint32_t idle = millis() - _lastActivity;
        if (_state == FULL && idle > _idleMs)  { _state = DIM;   _bl->fadeTo(_dim, 600); }
        if (_state == DIM  && idle > _sleepMs) { _state = SLEEP; _bl->fadeTo(0,   800); }
    }

    // Screen off now, the way the side button works on a phone.
    void sleepNow() { if (_state != SLEEP) { _state = SLEEP; _bl->fadeTo(0, 250); } }

    bool asleep() const { return _state == SLEEP; }
    bool dimmed() const { return _state == DIM; }

    // The user's chosen brightness is the level the dimmer returns to. Without
    // this, any change made in Settings is undone by the next activity fade.
    void setFull(uint8_t level) {
        _full = level;
        if (_state == FULL) _bl->fadeTo(_full, 150);
    }
    uint8_t full() const { return _full; }

    void setTimes(uint32_t idleMs, uint32_t sleepMs) {
        _idleMs = idleMs; _sleepMs = sleepMs < idleMs ? idleMs + 1000 : sleepMs;
    }
    // Wake from a message without it counting as the user touching anything.
    void wake() { note(); }
    uint32_t idleFor() const { return millis() - _lastActivity; }

private:
    enum State { FULL, DIM, SLEEP };
    Backlight* _bl = nullptr;
    State    _state = FULL;
    uint8_t  _full = 12, _dim = 3;
    uint32_t _idleMs = 15000, _sleepMs = 60000, _lastActivity = 0;
};
