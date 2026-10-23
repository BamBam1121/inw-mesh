// ThinkNode M9 keyboard: a second microcontroller (an ESP32-S2 running Elecrow's key
// scanner) answers on its own I2C bus, SDA 20 / SCL 21, at 0x6C on hardware v1.0 and
// 0x6D on v1.1. It is a register device: write the register number, then read.
//   0x00  hardware version (0x03)      0x01  the waiting key, 0 = none (one at a time)
//   0x02  key backlight, 0..255 (it lights on a key press by itself, off after 10 s)
// It applies Shift / Sym / Alt itself and sends printable keys as their character.
// Other codes:
//   0xB4..0xB7  left, up, down, right   0x0D Enter (0xA3 held)   0x08 Del
//   0x82 Home   0x86 Back   0x81 / 0x83 messages   0x84 / 0x85 map (0x87 held)
//   0x88 the triangle key   0x90 Ctrl
// The interrupt line it has (GPIO12) is undocumented, so it is polled, as Wadamesh does
// on real M9s; their notes (scratch copies, not in the tree) are where all of this,
// and the two traps below, come from.
//
// The arrows don't type anything: they are this board's trackball (rotary.h takes
// them from here), and Home is its lock/wake button (main.cpp, BOARD_KEYS_ONLY).

#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "board_pins.h"
#include "io_expander.h"   // m9::version()

enum : uint8_t {
    KEY_IDX_BACKSPACE = 29,   // same index the pager uses, so shared code reads it the same way
    KEY_IDX_NONE      = 0xFF,
};

struct KeyEvent {
    bool    pressed;
    uint8_t index;            // KEY_IDX_BACKSPACE, or KEY_IDX_NONE for everything else
    char    ch;               // 0 for keys with no character
};

namespace m9kb {
// The arrows and Home, for rotary.h and main.cpp.
struct Nav { int8_t dx = 0, dy = 0; bool home = false; };
inline Nav& nav() { static Nav n; return n; }
}

class Keyboard {
public:
    bool begin(TwoWire& w = Wire) {
        _w = &w;
        // Its own microcontroller, which may still be starting: looked for again every
        // second from read() if it isn't there yet.
        for (int i = 0; i < 20 && !_addr; i++) if (!_find()) delay(25);
        return _addr != 0;
    }

    // One key per call; false when nothing was typed. Arrows and Home are passed on
    // to the trackball stand-in and the lock button, and don't come back from here.
    bool read(KeyEvent& ev) {
        const uint32_t now = millis();
        if (!_addr) {
            if (now - _lookedAt < 1000) return false;
            _lookedAt = now;
            if (!_find()) return false;
        }
        // Every 15 ms is plenty for typing; any faster only keeps the bus busy.
        if ((int32_t)(now - _nextPoll) < 0) return false;
        _nextPoll = now + 15;
        const uint8_t c = _readReg(0x01);
        if (c == 0x00 || c == 0xFF) return false;
        // The same key again within 70 ms is the same press read twice (the controller
        // hadn't cleared it yet) or the key bouncing: Wadamesh's double letters.
        if (c == _last && now - _lastAt < 70) return false;
        _last = c;
        _lastAt = now;
        ev.pressed = true;
        ev.index = KEY_IDX_NONE;
        ev.ch = 0;
        switch (c) {
            case 0xB4: m9kb::nav().dx--; return false;      // left
            case 0xB7: m9kb::nav().dx++; return false;      // right
            case 0xB5: m9kb::nav().dy--; return false;      // up
            case 0xB6: m9kb::nav().dy++; return false;      // down
            case 0x82: m9kb::nav().home = true; return false;
            case 0x08: case 0x86: ev.index = KEY_IDX_BACKSPACE; return true;   // Del, and Back
            case 0x0D: ev.ch = '\n'; return true;
            default:
                if (c >= 0x20 && c < 0x7F) { ev.ch = (char)c; return true; }
                return false;                                // messages, map, Ctrl...: not used yet
        }
    }

    uint8_t lastKey() const { return _last; }     // the hardware check shows it
    uint8_t address() const { return _addr; }
    bool symbolHeld() const { return false; }
    bool capsOn() const { return false; }
    bool ok() const { return _addr != 0; }

    void setBacklight(uint8_t duty) {
        _duty = duty;
        if (!_addr) return;
        _w->beginTransmission(_addr);
        _w->write(0x02);
        _w->write(duty);
        _w->endTransmission();
    }

private:
    bool _find() {
        for (uint8_t a : { ADDR_M9_KB_V11, ADDR_M9_KB_V10 }) {
            _addr = a;
            if (_readReg(0x00) == 0xFF) continue;           // nobody there
            m9::version() = a == ADDR_M9_KB_V11 ? 2 : 1;
            Serial.printf("[kb] M9 keyboard at 0x%02X\n", a);
            if (_duty != 0xFF) setBacklight(_duty);         // asked for before it answered
            return true;
        }
        _addr = 0;
        return false;
    }
    uint8_t _readReg(uint8_t reg) {
        _w->beginTransmission(_addr);
        _w->write(reg);
        if (_w->endTransmission() != 0) return 0xFF;
        if (_w->requestFrom(_addr, (uint8_t)1) != 1) return 0xFF;
        return (uint8_t)_w->read();
    }

    TwoWire* _w = nullptr;
    uint8_t  _addr = 0;
    uint8_t  _last = 0;
    uint8_t  _duty = 0xFF;
    uint32_t _lastAt = 0, _lookedAt = 0, _nextPoll = 0;
};
