// The T-Deck has no battery-backed clock. Same interface as the pager's
// PCF85063 driver, always absent: the time comes from GPS (Plus), Wi-Fi or the
// phone, and the app already copes with a clock that isn't set yet.

#pragma once
#include <Arduino.h>
#include <Wire.h>

struct RtcTime {
    uint16_t year = 0;
    uint8_t  month = 0, day = 0, hour = 0, minute = 0, second = 0;
};

class Rtc {
public:
    bool begin(TwoWire& = Wire) { return false; }
    bool ok() const { return false; }
    bool valid() const { return false; }
    bool read(RtcTime&) { return false; }
    bool set(const RtcTime&) { return false; }
};
