// The T-Deck has no I/O expander: one GPIO (PIN_POWER_ON) switches the power
// for the keyboard, radio, display and SD together. begin() turns it on; the
// pager's per-rail calls have nothing to switch here and do nothing.

#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "board_pins.h"

class IoExpander {
public:
    bool begin(TwoWire& = Wire, uint8_t = 0) {
        ::pinMode(PIN_POWER_ON, OUTPUT);
        ::digitalWrite(PIN_POWER_ON, HIGH);
        delay(100);                      // let the keyboard controller and radio come up
        return true;
    }
    bool ok() const { return true; }
    void pinMode(uint8_t, uint8_t) {}
    void digitalWrite(uint8_t, uint8_t) {}
    bool regs(uint8_t*, uint8_t*) { return false; }
    int  digitalRead(uint8_t) { return -1; }
    void enableRail(uint8_t, uint16_t = 10) {}
};
