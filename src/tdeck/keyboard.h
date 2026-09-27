// T-Deck keyboard: a separate ESP32-C3 on the I2C bus (0x55) that scans the keys
// itself and hands over finished characters, one byte per read (0 = none).
// Shift, Alt and Sym are resolved on the C3, so there are no key-up events and
// no modifier state to track here.
//
// Facts from Wadamesh's T-Deck driver, which learned them on hardware:
// - newer controller firmware also has a raw matrix mode (command 0x03); it is
//   not used here. Command 0x04 selects the character mode this reads, and is
//   harmless on older controllers, which only have that mode.
// - the backlight is the two-byte command {0x01, level}, 0 = off.
// - the C3 keeps its backlight lit across a reset of the main chip, so the
//   first backlight request after boot is always sent, even if it matches.

#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "board_pins.h"

enum : uint8_t {
    KEY_IDX_BACKSPACE = 29,   // same index the pager uses, so shared code reads it the same way
    KEY_IDX_NONE      = 0xFF,
};

struct KeyEvent {
    bool    pressed;
    uint8_t index;            // KEY_IDX_BACKSPACE, or KEY_IDX_NONE for everything else
    char    ch;               // 0 for keys with no character
};

class Keyboard {
public:
    static constexpr uint8_t ADDR = ADDR_TDECK_KB;

    bool begin(TwoWire& w = Wire) {
        _w = &w;
        // The C3 boots alongside us from the same power switch; give it a moment.
        for (int i = 0; i < 20 && !_present(); i++) delay(25);
        if (!_present()) return false;
        _command(0x04);                // character mode (the only mode older controllers have)
        _ok = true;
        return true;
    }

    // One character per call; false when nothing was typed.
    bool read(KeyEvent& ev) {
        if (!_ok) return false;
        const uint32_t now = millis();
        if (now - _lastPoll < POLL_MS) return false;   // the C3 is polled, not interrupt-driven
        _lastPoll = now;
        if (_w->requestFrom(ADDR, (uint8_t)1) != 1) return false;
        const uint8_t c = (uint8_t)_w->read();
        if (!c) return false;
        _last = c;
        ev.pressed = true;
        ev.index = KEY_IDX_NONE;
        ev.ch = 0;
        switch (c) {
            case 0x08: ev.index = KEY_IDX_BACKSPACE; break;
            case 0x0D: case 0x0A: ev.ch = '\n'; break;
            default:   if (c >= 0x20 && c < 0x7F) ev.ch = (char)c; break;
        }
        return true;
    }

    uint8_t lastKey() const { return _last; }     // the hardware check shows it
    bool symbolHeld() const { return false; }
    bool capsOn() const { return false; }
    bool ok() const { return _ok; }

    void setBacklight(uint8_t duty) {
        if (!_ok || (duty == _bl && _blSent)) return;
        _w->beginTransmission(ADDR);
        _w->write(0x01);
        _w->write(duty);
        if (_w->endTransmission() == 0) { _bl = duty; _blSent = true; }
    }

private:
    static constexpr uint32_t POLL_MS = 20;

    bool _present() {
        _w->beginTransmission(ADDR);
        return _w->endTransmission() == 0;
    }
    void _command(uint8_t c) {
        _w->beginTransmission(ADDR);
        _w->write(c);
        _w->endTransmission();
    }

    TwoWire* _w = nullptr;
    bool     _ok = false;
    uint32_t _lastPoll = 0;
    uint8_t  _bl = 0;
    bool     _blSent = false;
    uint8_t  _last = 0;
};
