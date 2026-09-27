// Idle auto-dim over a board's Backlight (backlight.h in src/<board>/, which
// includes this after defining the class). Shared by every board.

#pragma once
#include <Arduino.h>

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
    // No fade: an animation has already done the turning on or off.
    void wakeInstant()  { _lastActivity = millis(); _state = FULL; _bl->setNow(_full); }
    void sleepInstant() { _state = SLEEP; _bl->setNow(0); }

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
