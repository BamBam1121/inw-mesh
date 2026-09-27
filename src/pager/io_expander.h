// XL9555 I2C GPIO expander. It gates the power rails for the radio, GPS, NFC,
// SD, haptics and amp, so a peripheral that "isn't there" usually means its rail
// is off. PCA9535-compatible registers; direction bit 1 = input (reset state).

#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "board_pins.h"

class IoExpander {
public:
    // LilyGo straps A0-A2 low. The PCA9535 family answers anywhere in 0x20-0x27,
    // so if a board revision moves it, look there.
    static constexpr uint8_t ADDR = ADDR_XL9555_EXP;

    bool begin(TwoWire& w = Wire, uint8_t addr = ADDR) {
        _w = &w; _addr = addr;
        _w->beginTransmission(_addr);
        if (_w->endTransmission() != 0) return false;
        // Read back rather than assume the reset state: a warm reboot leaves the
        // rails however the last run left them, and we want the cache to match.
        if (!_read(REG_OUTPUT0, _out) || !_read(REG_CONFIG0, _cfg)) return false;
        _ok = true;
        return true;
    }

    bool ok() const { return _ok; }

    void pinMode(uint8_t pin, uint8_t mode) {
        if (!_ok || pin > 15) return;
        _setBit(_cfg, pin, mode == INPUT);
        _write(pin < 8 ? REG_CONFIG0 : REG_CONFIG1, pin < 8 ? _cfg[0] : _cfg[1]);
    }

    void digitalWrite(uint8_t pin, uint8_t level) {
        if (!_ok || pin > 15) return;
        _setBit(_out, pin, level == HIGH);
        _write(pin < 8 ? REG_OUTPUT0 : REG_OUTPUT1, pin < 8 ? _out[0] : _out[1]);
    }

    // The chip's own output and direction registers, both ports (diagnostics).
    bool regs(uint8_t* out, uint8_t* cfg) { return _ok && _read(REG_OUTPUT0, out) && _read(REG_CONFIG0, cfg); }

    int digitalRead(uint8_t pin) {
        if (!_ok || pin > 15) return -1;
        uint8_t in[2];
        if (!_read(REG_INPUT0, in)) return -1;
        return (in[pin / 8] >> (pin % 8)) & 1;
    }

    // Drive a rail high and give the regulator a moment before anyone talks to the
    // chip behind it.
    void enableRail(uint8_t pin, uint16_t settleMs = 10) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
        if (settleMs) delay(settleMs);
    }

private:
    enum : uint8_t {
        REG_INPUT0 = 0x00, REG_OUTPUT0 = 0x02, REG_CONFIG0 = 0x06, REG_CONFIG1 = 0x07,
        REG_OUTPUT1 = 0x03,
    };

    static void _setBit(uint8_t* pair, uint8_t pin, bool set) {
        const uint8_t mask = 1u << (pin % 8);
        if (set) pair[pin / 8] |= mask; else pair[pin / 8] &= ~mask;
    }

    bool _write(uint8_t reg, uint8_t val) {
        _w->beginTransmission(_addr);
        _w->write(reg); _w->write(val);
        return _w->endTransmission() == 0;
    }

    // Both ports in one go: the register pointer auto-increments across the pair.
    bool _read(uint8_t reg, uint8_t* pair) {
        _w->beginTransmission(_addr);
        _w->write(reg);
        if (_w->endTransmission(false) != 0) return false;
        if (_w->requestFrom(_addr, (uint8_t)2) != 2) return false;
        pair[0] = _w->read();
        pair[1] = _w->read();
        return true;
    }

    TwoWire* _w = nullptr;
    uint8_t  _addr = ADDR;
    uint8_t  _out[2] = {0xFF, 0xFF};
    uint8_t  _cfg[2] = {0xFF, 0xFF};
    bool     _ok = false;
};
