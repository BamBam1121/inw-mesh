// PCF85063A. Time registers are BCD at 0x04-0x0A; bit 7 of seconds is the
// oscillator-stop flag, meaning the time can't be trusted.

#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "board_pins.h"

struct RtcTime {
    uint16_t year = 0;
    uint8_t  month = 0, day = 0, hour = 0, minute = 0, second = 0;
};

class Rtc {
public:
    bool begin(TwoWire& w = Wire) {
        _w = &w;
        _w->beginTransmission(ADDR_RTC_PCF85063);
        _ok = _w->endTransmission() == 0;
        return _ok;
    }

    bool ok() const { return _ok; }
    bool valid() const { return _ok && !_oscStopped; }

    bool read(RtcTime& t) {
        if (!_ok) return false;
        _w->beginTransmission(ADDR_RTC_PCF85063);
        _w->write(REG_SECONDS);
        if (_w->endTransmission(false) != 0) return false;
        if (_w->requestFrom((uint8_t)ADDR_RTC_PCF85063, (uint8_t)7) != 7) return false;

        const uint8_t rawSec = _w->read();
        _oscStopped = (rawSec & 0x80) != 0;      // clock lost power since last set
        t.second = bcd(rawSec & 0x7F);
        t.minute = bcd(_w->read() & 0x7F);
        t.hour   = bcd(_w->read() & 0x3F);
        t.day    = bcd(_w->read() & 0x3F);
        _w->read();                              // weekday, derivable, unused
        t.month  = bcd(_w->read() & 0x1F);
        t.year   = (uint16_t)(2000 + bcd(_w->read()));
        return !_oscStopped;
    }

    // Called on the first GPS fix. Satellites carry better time than a coin cell.
    bool set(const RtcTime& t) {
        if (!_ok) return false;
        _w->beginTransmission(ADDR_RTC_PCF85063);
        _w->write(REG_SECONDS);
        _w->write(toBcd(t.second));              // writing clears the stop flag
        _w->write(toBcd(t.minute));
        _w->write(toBcd(t.hour));
        _w->write(toBcd(t.day));
        _w->write(0);
        _w->write(toBcd(t.month));
        _w->write(toBcd((uint8_t)(t.year % 100)));
        if (_w->endTransmission() != 0) return false;
        _oscStopped = false;
        return true;
    }

private:
    enum : uint8_t { REG_SECONDS = 0x04 };
    static uint8_t bcd(uint8_t v)   { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
    static uint8_t toBcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

    TwoWire* _w = nullptr;
    bool     _ok = false, _oscStopped = true;
};
