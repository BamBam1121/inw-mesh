// The T-Deck keyboard for the simulator: present, typed keys come from sim_main.
#pragma once
#include <stdint.h>
#include "board_pins.h"

enum : uint8_t {
    KEY_IDX_BACKSPACE = 29,
    KEY_IDX_NONE      = 0xFF,
};

struct KeyEvent {
    bool    pressed;
    uint8_t index;
    char    ch;
};

class TwoWire;
class Keyboard {
public:
    bool begin(TwoWire&) { return true; }
    bool read(KeyEvent&) { return false; }
    uint8_t lastKey() const { return last; }
    bool symbolHeld() const { return false; }
    bool capsOn() const { return false; }
    bool ok() const { return true; }
    void setBacklight(uint8_t) {}
    uint8_t last = 0;
};
