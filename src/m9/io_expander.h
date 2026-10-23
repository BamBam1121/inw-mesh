// The M9 has no I/O expander. One GPIO (PIN_POWER_ON, active LOW) powers the screen,
// GPS and the rest of the peripherals; the GPS also has its own enable, whose sense
// depends on the hardware version (board_pins.h). begin() powers everything up; the
// pager's per-rail calls only reach the GPS enable here.

#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <soc/usb_serial_jtag_reg.h>
#include "board_pins.h"

namespace m9 {
// 1 = v1.0 (keyboard at 0x6C, L76K GPS), 2 = v1.1 (0x6D, AG3352), 0 = not known yet.
// Found by keyboard.h when it looks for the controller.
inline uint8_t& version() { static uint8_t v = 0; return v; }
inline uint8_t gpsOnLevel() { return version() == 1 ? LOW : HIGH; }
}

class IoExpander {
public:
    bool begin(TwoWire& w = Wire, uint8_t = 0) {
        // GPIO20 is also the S3's own USB D+, and its USB block holds that pad (with a
        // pull-up) from reset. The M9 doesn't use it - its USB socket goes to a bridge
        // chip - and wires GPIO20/21 to the keyboard instead, which never answers until
        // the pad is let go (Wadamesh's bring-up #8). First, before any I2C.
        REG_CLR_BIT(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
        // Chip selects up first, so nothing on the shared bus answers by accident
        // (Meshtastic's earlyInitVariant does the same).
        for (int cs : { PIN_LORA_CS, PIN_SD_CS, PIN_TFT_CS }) { ::pinMode(cs, OUTPUT); ::digitalWrite(cs, HIGH); }
        ::pinMode(PIN_POWER_ON, OUTPUT);
        ::digitalWrite(PIN_POWER_ON, POWER_ON_LEVEL);
        // The GPS reset goes through a transistor: HIGH holds the GPS in reset, LOW lets
        // it run (Wadamesh, from the schematic). Left floating it could do either.
        ::pinMode(PIN_GPS_RST, OUTPUT);
        ::digitalWrite(PIN_GPS_RST, LOW);
        delay(100);                            // let the keyboard controller and screen come up
        // Which hardware version: the keyboard controller's address says (the GPS enable's
        // sense depends on it). keyboard.h looks again, and agrees.
        for (int i = 0; i < 10 && !m9::version(); i++) {
            w.beginTransmission(ADDR_M9_KB_V11);
            if (w.endTransmission() == 0) { m9::version() = 2; break; }
            w.beginTransmission(ADDR_M9_KB_V10);
            if (w.endTransmission() == 0) { m9::version() = 1; break; }
            delay(25);
        }
        // Every rail starts on, as on the pager (main.cpp's GPS schedule turns it off later).
        digitalWrite(EXP_GPS_EN, HIGH);
        return true;
    }
    bool ok() const { return true; }
    void pinMode(uint8_t, uint8_t) {}
    // Only the GPS has a switch of its own (EXP_GPS_EN); the others are always on.
    void digitalWrite(uint8_t exp, uint8_t v) {
        if (exp != EXP_GPS_EN) return;
        ::pinMode(PIN_GPS_EN, OUTPUT);
        ::digitalWrite(PIN_GPS_EN, v ? m9::gpsOnLevel() : !m9::gpsOnLevel());
    }
    bool regs(uint8_t*, uint8_t*) { return false; }
    int  digitalRead(uint8_t) { return -1; }
    void enableRail(uint8_t exp, uint16_t settleMs = 10) { digitalWrite(exp, HIGH); if (exp == EXP_GPS_EN) delay(settleMs); }
};
