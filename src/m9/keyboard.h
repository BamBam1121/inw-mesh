// ThinkNode M9 keyboard: an STC8H microcontroller scans the keys and answers on I2C
// (0x6C on hardware v1.0, 0x6D on v1.1). When a key goes down it raises PIN_KB_INT
// (rests LOW, rising edge on a press); the key's code is then one byte read from
// register 0x01. Printable keys come as their ASCII character, with Shift and Sym
// already applied by the STC8H.
//
// All of this is from Meshtastic's driver (src/input/STC8HKeyboard.cpp and the M9
// case of kbI2cBase.cpp), recovered from Elecrow's own firmware; copies are in
// m9-reference/meshtastic. Codes:
//   0x20..0x7E  the character         0x0D Enter          0x08 Del (0x89 held)
//   0xB4..0xB7  left, up, down, right (the arrow keys)
//   0x81 Mute   0x82 Home   0x83 Time   0x84 torch   0x85 FM (0x86 held)   0x87 Preset
//   0x88        "no key" (ignored)
// Register 0x03 takes a 16-bit long-press time in ms (Meshtastic sets 700).
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
// In trackball.cpp, so the interrupt handler is in IRAM: a key pressed while flash
// is busy (a settings save) must not reach for code in flash.
extern volatile bool pending;   // set in the interrupt: a key went down since the last look
void onKey();
// The arrows and Home, for rotary.h and main.cpp.
struct Nav { int8_t dx = 0, dy = 0; bool home = false; };
inline Nav& nav() { static Nav n; return n; }
}

class Keyboard {
public:
    bool begin(TwoWire& w = Wire) {
        _w = &w;
        // The STC8H starts with the board; give it a moment to answer.
        for (int i = 0; i < 20 && !_addr; i++) {
            if (_present(ADDR_M9_KB_V11)) { _addr = ADDR_M9_KB_V11; m9::version() = 2; }
            else if (_present(ADDR_M9_KB_V10)) { _addr = ADDR_M9_KB_V10; m9::version() = 1; }
            else delay(25);
        }
        if (!_addr) return false;
        // Long press after 700 ms, as Meshtastic sets it (Del and FM have held codes).
        _w->beginTransmission(_addr);
        _w->write(0x03);
        _w->write((uint8_t)(700 >> 8));
        _w->write((uint8_t)(700 & 0xFF));
        _w->endTransmission();
        pinMode(PIN_KB_INT, INPUT);
        attachInterrupt(digitalPinToInterrupt(PIN_KB_INT), m9kb::onKey, RISING);
        pinMode(PIN_KB_BL, OUTPUT);
        digitalWrite(PIN_KB_BL, LOW);
        _ok = true;
        return true;
    }

    // One key per call; false when nothing was typed. Arrows and Home are passed on
    // to the trackball stand-in and the lock button, and don't come back from here.
    bool read(KeyEvent& ev) {
        if (!_ok) return false;
        while (m9kb::pending) {
            m9kb::pending = false;
            const uint8_t c = _readReg(0x01);
            if (c == 0xFF || c == 0x00 || c == 0x88) continue;
            _last = c;
            ev.pressed = true;
            ev.index = KEY_IDX_NONE;
            ev.ch = 0;
            switch (c) {
                case 0xB4: m9kb::nav().dx--; continue;      // left
                case 0xB7: m9kb::nav().dx++; continue;      // right
                case 0xB5: m9kb::nav().dy--; continue;      // up
                case 0xB6: m9kb::nav().dy++; continue;      // down
                case 0x82: m9kb::nav().home = true; continue;
                case 0x08: case 0x89: ev.index = KEY_IDX_BACKSPACE; return true;
                case 0x0D: case 0x0A: ev.ch = '\n'; return true;
                default:
                    if (c >= 0x20 && c < 0x7F) { ev.ch = (char)c; return true; }
                    continue;                                // Mute, Time, torch, FM, Preset: not used yet
            }
        }
        return false;
    }

    uint8_t lastKey() const { return _last; }     // the hardware check shows it
    uint8_t address() const { return _addr; }
    bool symbolHeld() const { return false; }
    bool capsOn() const { return false; }
    bool ok() const { return _ok; }

    // The key backlight is a plain on/off GPIO here: any level lights it.
    void setBacklight(uint8_t duty) {
        if (!_ok) return;
        digitalWrite(PIN_KB_BL, duty ? HIGH : LOW);
    }

private:
    bool _present(uint8_t a) {
        _w->beginTransmission(a);
        return _w->endTransmission() == 0;
    }
    uint8_t _readReg(uint8_t reg) {
        _w->beginTransmission(_addr);
        _w->write(reg);
        if (_w->endTransmission(false) != 0) return 0xFF;
        if (_w->requestFrom(_addr, (uint8_t)1) != 1) return 0xFF;
        return (uint8_t)_w->read();
    }

    TwoWire* _w = nullptr;
    bool     _ok = false;
    uint8_t  _addr = 0;
    uint8_t  _last = 0;
};
