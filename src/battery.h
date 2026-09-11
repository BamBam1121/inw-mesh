// BQ27220 fuel gauge. Register map from LilyGo's driver; all values are
// little-endian 16-bit words. Design capacity (1500 mAh) is flash-backed on the
// gauge, so it isn't rewritten on every boot.

#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "board_pins.h"

class Battery {
public:
    static constexpr uint16_t DESIGN_MAH = 1500;

    bool begin(TwoWire& w = Wire) {
        _w = &w;
        _w->beginTransmission(ADDR_BQ27220_GAUGE);
        _gauge = _w->endTransmission() == 0;
        return _gauge;
    }

    bool present() const { return _gauge; }

    // Cached: the status bar asks every frame and I2C at 400 kHz is not free.
    void tick(uint32_t now) {
        if (!_gauge || (now - _last < REFRESH_MS && _last)) return;
        _last = now;
        _percent = (uint8_t)min<uint16_t>(read16(REG_SOC), 100);
        _millivolts = read16(REG_VOLTAGE);
        _currentMa = (int16_t)read16(REG_CURRENT);
    }

    uint8_t  percent() const { return _percent; }
    uint16_t millivolts() const { return _millivolts; }

    // The gauge reports current signed: positive is charge going in.
    bool charging() const { return _currentMa > 5; }

private:
    enum : uint8_t { REG_VOLTAGE = 0x08, REG_CURRENT = 0x0C, REG_SOC = 0x2C };
    static constexpr uint32_t REFRESH_MS = 5000;

    uint16_t read16(uint8_t reg) {
        _w->beginTransmission(ADDR_BQ27220_GAUGE);
        _w->write(reg);
        if (_w->endTransmission(false) != 0) return 0;
        if (_w->requestFrom((uint8_t)ADDR_BQ27220_GAUGE, (uint8_t)2) != 2) return 0;
        const uint8_t lo = _w->read(), hi = _w->read();
        return (uint16_t)(lo | (hi << 8));
    }

    TwoWire* _w = nullptr;
    bool     _gauge = false;
    uint8_t  _percent = 0;
    uint16_t _millivolts = 0;
    int16_t  _currentMa = 0;
    uint32_t _last = 0;
};
