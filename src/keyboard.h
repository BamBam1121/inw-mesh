// TCA8418 4x10 key matrix. Keymap and modifier codes match LilyGo's factory
// firmware. The orange key held gives digits and symbols; Caps is a toggle.
// No arrows or Esc, so the wheel navigates and Backspace means back.

#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "board_pins.h"

// Key indices are row * 10 + col, matching the map below.
enum : uint8_t {
    KEY_IDX_ALT       = 20,
    KEY_IDX_B         = 25,   // orange button + B is LilyGo's backlight chord
    KEY_IDX_CAPS      = 28,
    KEY_IDX_BACKSPACE = 29,
    KEY_IDX_SPACE     = 30,   // doubles as the symbol modifier
};

struct KeyEvent {
    bool    pressed;
    uint8_t index;            // row * 10 + col
    char    ch;               // 0 for keys with no character (modifiers, blanks)
};

class Keyboard {
public:
    static constexpr uint8_t ADDR = ADDR_TCA8418_KB;

    bool begin(TwoWire& w = Wire) {
        _w = &w;
        // Claim the matrix pins: rows on GPIO1, columns split across GPIO2 and 3.
        if (!_write(REG_KP_GPIO1, (uint8_t)((1u << KB_MATRIX_ROWS) - 1))) return false;
        _write(REG_KP_GPIO2, 0xFF);                        // columns 0-7
        _write(REG_KP_GPIO3, (uint8_t)((1u << (KB_MATRIX_COLS - 8)) - 1));
        _write(REG_CFG, CFG_KE_IEN);
        _write(REG_INT_STAT, 0x0F);                        // write 1 to clear
        _ok = true;
        return true;
    }

    // Pops one event from the chip's FIFO. Returns false when it is empty.
    bool read(KeyEvent& ev) {
        if (!_ok || (_read(REG_KEY_LCK_EC) & 0x0F) == 0) return false;

        const uint8_t raw = _read(REG_KEY_EVENT_A);
        if (raw == 0) return false;
        ev.pressed = (raw & 0x80) != 0;
        ev.index   = (uint8_t)((raw & 0x7F) - 1);          // FIFO counts from 1

        // Update modifiers before resolving the character. Orange (Alt) is a hold
        // for the symbol/number layer; Caps is a lock that toggles on each press.
        if (ev.index == KEY_IDX_ALT)  _symbol = ev.pressed;
        if (ev.index == KEY_IDX_CAPS && ev.pressed) _caps = !_caps;

        ev.ch = character(ev.index);
        return true;
    }

    // True while Orange (Alt) is held, this board's symbol shift.
    bool symbolHeld() const { return _symbol; }
    bool capsOn() const { return _caps; }
    bool ok() const { return _ok; }

    void setBacklight(uint8_t duty) { analogWrite(PIN_KB_BL, duty); }

private:
    enum : uint8_t {
        REG_CFG = 0x01, REG_INT_STAT = 0x02, REG_KEY_LCK_EC = 0x03,
        REG_KEY_EVENT_A = 0x04, REG_KP_GPIO1 = 0x1D, REG_KP_GPIO2 = 0x1E,
        REG_KP_GPIO3 = 0x1F,
        CFG_KE_IEN = 0x01,
    };

    char character(uint8_t index) const {
        if (index >= KB_MATRIX_ROWS * KB_MATRIX_COLS) return 0;
        static const char BASE[] =
            "qwertyuiop"
            "asdfghjkl\n"
            "\0zxcvbnm\0\0"
            " \0\0\0\0\0\0\0\0\0";
        static const char SYMBOL[] =
            "1234567890"
            "*/+-=:'\"@\0"
            "\0_$;?!,.\0\0"
            " \0\0\0\0\0\0\0\0\0";
        char c = _symbol ? SYMBOL[index] : BASE[index];
        if (!_symbol && _caps && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        return c;
    }

    bool _write(uint8_t reg, uint8_t val) {
        _w->beginTransmission(ADDR);
        _w->write(reg); _w->write(val);
        return _w->endTransmission() == 0;
    }

    uint8_t _read(uint8_t reg) {
        _w->beginTransmission(ADDR);
        _w->write(reg);
        if (_w->endTransmission(false) != 0) return 0;
        if (_w->requestFrom(ADDR, (uint8_t)1) != 1) return 0;
        return (uint8_t)_w->read();
    }

    TwoWire* _w = nullptr;
    bool     _ok = false;
    bool     _symbol = false;
    bool     _caps = false;
};
