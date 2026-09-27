// Hold the trackball down (it is the BOOT button) through a reset to get an I2C
// scan on screen and serial: tells a keyboard that isn't answering apart from a
// driver bug. The T-Deck's bus is short - the keyboard's controller and the
// touch chip - so the list is too.

#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "board_pins.h"
#include "display_config.h"
#include "io_expander.h"

namespace bringup {

struct Chip {
    uint8_t     addr;
    const char* name;
};

static const Chip EXPECTED[] = {
    { ADDR_TDECK_KB, "keyboard (C3)" },
    { ADDR_GT911_A,  "GT911 touch"   },   // at 0x14 instead on some units, see report()
};
static constexpr uint8_t EXPECTED_COUNT = sizeof(EXPECTED) / sizeof(EXPECTED[0]);

inline bool present(TwoWire& w, uint8_t addr) {
    w.beginTransmission(addr);
    return w.endTransmission() == 0;
}

// Power is one switch here, turned on by IoExpander::begin().
inline void powerAllRails(IoExpander&) {}

inline void scanToSerial(TwoWire& w) {
    Serial.println("[bringup] i2c scan 0x08-0x77");
    uint8_t found = 0;
    for (uint8_t a = 0x08; a < 0x78; a++) {
        if (!present(w, a)) continue;
        Serial.printf("  0x%02X\n", a);
        found++;
    }
    Serial.printf("[bringup] %u device(s)\n", found);
}

inline void report(LGFX& d, TwoWire& w) {
    const uint16_t bg    = d.color565(0x06, 0x0a, 0x09);
    const uint16_t green = d.color565(0x3d, 0xff, 0xa8);
    const uint16_t dim   = d.color565(0x5f, 0x80, 0x74);
    const uint16_t amber = d.color565(0xe6, 0xb9, 0x55);

    d.fillScreen(bg);
    d.setFont(&fonts::Font2);
    d.setTextColor(green, bg);
    d.drawString("root@t-deck:~$ selftest", 8, 6);

    bool up[EXPECTED_COUNT];
    uint8_t missing = 0;
    for (uint8_t i = 0; i < EXPECTED_COUNT; i++) {
        up[i] = present(w, EXPECTED[i].addr) || (EXPECTED[i].addr == ADDR_GT911_A && present(w, ADDR_GT911_B));
        if (!up[i]) missing++;
    }
    int y = 30;
    for (uint8_t i = 0; i < EXPECTED_COUNT; i++) {
        d.setTextColor(dim, bg);
        char label[32];
        snprintf(label, sizeof(label), "%02X %s ", EXPECTED[i].addr, EXPECTED[i].name);
        const int lx = d.drawString(label, 8, y);
        d.setTextColor(up[i] ? green : amber, bg);
        d.drawString(up[i] ? "OK" : "--", 8 + lx + 4, y);
        y += 18;
    }
    d.setTextColor(missing ? amber : green, bg);
    char footer[48];
    snprintf(footer, sizeof(footer), "> %u/%u present", EXPECTED_COUNT - missing, EXPECTED_COUNT);
    d.drawString(footer, 8, y + 10);
    d.setTextColor(dim, bg);
    d.drawString("hold the trackball at reset to run this again", 8, y + 30);

    Serial.printf("[bringup] %u/%u expected chips present\n", EXPECTED_COUNT - missing, EXPECTED_COUNT);
}

}  // namespace bringup
