// Hold BOOT through reset to get an I2C scan on screen and serial. Useful for
// telling a dead or re-addressed chip apart from a driver bug.

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

// Addresses are the defaults for the parts LilyGo lists on this board. Anything
// that comes back missing but shows up in the raw scan below is on a strap you
// don't expect: take the address from the scan, don't force it here.
static const Chip EXPECTED[] = {
    { ADDR_ES8311_CODEC,  "ES8311  codec"  },
    { ADDR_XL9555_EXP,    "XL9555  expand" },
    { ADDR_BHI260AP_IMU,  "BHI260AP imu"   },
    { ADDR_TCA8418_KB,    "TCA8418 keys"   },
    { ADDR_RTC_PCF85063,  "PCF85063 rtc"   },
    { ADDR_BQ27220_GAUGE, "BQ27220 gauge"  },
    { ADDR_DRV2605_HAPT,  "DRV2605 haptic" },
    { ADDR_BQ25896_CHG,   "BQ25896 chg"    },
};
static constexpr uint8_t EXPECTED_COUNT = sizeof(EXPECTED) / sizeof(EXPECTED[0]);

inline bool present(TwoWire& w, uint8_t addr) {
    w.beginTransmission(addr);
    return w.endTransmission() == 0;
}

// Everything except the radio; setup() brings EXP_LORA_EN up separately, just
// before the radio is initialised.
inline void powerAllRails(IoExpander& exp) {
    static const uint8_t RAILS[] = {
        EXP_DRV_EN, EXP_AMP_EN, EXP_GPS_EN, EXP_NFC_EN, EXP_KB_EN,
        EXP_GPIO_EN, EXP_SD_EN,
    };
    for (uint8_t pin : RAILS) exp.enableRail(pin, 0);
    delay(50);                       // regulators settle, then the keyboard reset
    exp.pinMode(EXP_KB_RST, OUTPUT);
    exp.digitalWrite(EXP_KB_RST, LOW);
    delay(5);
    exp.digitalWrite(EXP_KB_RST, HIGH);
    delay(20);
}

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

// Draws the pass/fail list in the terminal-green theme (see THEME.md).
inline void report(LGFX& d, TwoWire& w) {
    const uint16_t bg    = d.color565(0x06, 0x0a, 0x09);
    const uint16_t green = d.color565(0x3d, 0xff, 0xa8);
    const uint16_t dim   = d.color565(0x5f, 0x80, 0x74);
    const uint16_t amber = d.color565(0xe6, 0xb9, 0x55);

    d.fillScreen(bg);
    d.setFont(&fonts::Font2);
    d.setTextColor(green, bg);
    d.drawString("root@pager:~$ selftest", 8, 6);

    // Probe once and keep the answers: a second pass over the bus for the serial
    // recap could disagree with what is on the screen.
    bool up[EXPECTED_COUNT];
    uint8_t missing = 0;
    for (uint8_t i = 0; i < EXPECTED_COUNT; i++) {
        up[i] = present(w, EXPECTED[i].addr);
        if (!up[i]) missing++;
    }

    // Two columns: eight chips will not fit down a 222px-tall screen.
    const int colX[2] = {8, 244};
    int y[2] = {28, 28};

    for (uint8_t i = 0; i < EXPECTED_COUNT; i++) {
        const uint8_t c = i / 4;
        d.setTextColor(dim, bg);
        char label[32];
        snprintf(label, sizeof(label), "%02X %s ", EXPECTED[i].addr, EXPECTED[i].name);
        const int lx = d.drawString(label, colX[c], y[c]);
        d.setTextColor(up[i] ? green : amber, bg);
        d.drawString(up[i] ? "OK" : "--", colX[c] + lx + 4, y[c]);
        y[c] += 16;
    }

    d.setTextColor(missing ? amber : green, bg);
    char footer[48];
    snprintf(footer, sizeof(footer), "> %u/%u present", EXPECTED_COUNT - missing,
             EXPECTED_COUNT);
    d.drawString(footer, 8, 100);

    d.setTextColor(dim, bg);
    d.drawString("hold BOOT at reset to run this again", 8, 118);

    Serial.printf("[bringup] %u/%u expected chips present\n",
                  EXPECTED_COUNT - missing, EXPECTED_COUNT);
    for (uint8_t i = 0; i < EXPECTED_COUNT; i++) {
        if (!up[i]) Serial.printf("  missing 0x%02X %s\n", EXPECTED[i].addr,
                                  EXPECTED[i].name);
    }
}

}  // namespace bringup
