// BQ27220 fuel gauge. Register map and the config-update sequence follow
// LilyGo's SensorLib driver; values are little-endian 16-bit words.
//
// The gauge ships assuming a different pack size, so its percentage is wrong
// until it is told the real one. begin() checks the stored design capacity and,
// if it isn't this board's 1500 mAh cell, writes it once. The gauge keeps it for
// as long as the battery stays connected.

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
        if (!_gauge) return false;
        uint16_t design = 0;
        if (read16(REG_DESIGN_CAP, design)) {
            _designBefore = design;
            if (design != DESIGN_MAH && design != 0xFFFF) _configured = setCapacity(DESIGN_MAH);
            if (read16(REG_DESIGN_CAP, design)) _designNow = design;
        }
        setupCharger();
        return true;
    }

    // The BQ25896 ended charge at 256 mA, a hard cut. The gauge only marks a pack
    // full after watching the current taper off under its threshold, so it never
    // saw "full" and sat at ~60% on a charged cell. End charge at 64 mA instead,
    // and keep the charger's watchdog off so it doesn't reset that after 40 s.
    void setupCharger() {
        uint8_t r05, r07;
        if (!chgRead(0x05, r05) || !chgRead(0x07, r07)) return;
        chgWrite(0x05, r05 & 0xF0);                     // ITERM = 64 mA, pre-charge unchanged
        chgWrite(0x07, r07 & ~0x30);                    // WATCHDOG disabled
    }

    // If the charger is done but the gauge never flagged full, run the tail of the
    // charge again (toggle CHG_CONFIG) so the gauge can see the taper. Once a boot.
    void resyncFull() {
        if (_resynced) return;
        uint16_t flags, soc;
        uint8_t st, r03;
        if (!read16(0x0A, flags) || !read16(REG_SOC, soc) || !chgRead(0x0B, st) || !chgRead(0x03, r03)) return;
        const bool vbus = (st >> 5) != 0, done = ((st >> 3) & 3) == 3, full = (flags >> 9) & 1;
        if (!vbus || !done || full || soc >= 95) return;
        _resynced = true;
        chgWrite(0x03, r03 & ~0x10);
        delay(50);
        chgWrite(0x03, r03 | 0x10);
        Serial.printf("[charger] restarted the charge tail so the gauge can see full (gauge %u%%)\n", soc);
    }

    bool present() const { return _gauge; }

    // Cached: the status bar asks every frame and I2C at 400 kHz is not free.
    // The bus is shared with the keyboard, expander and codec, so a read fails
    // now and then; keep the last good value instead of showing 0%.
    void tick(uint32_t now) {
        if (!_gauge || (now - _last < REFRESH_MS && _last)) return;
        _last = now;
        if (now - _lastDump > 30000 || !_lastDump) { _lastDump = now | 1; dump(); }
        if (now > 20000) resyncFull();
        uint16_t v;
        if (read16(REG_SOC, v) && v <= 100) _gaugePct = (uint8_t)v;
        if (read16(REG_VOLTAGE, v) && v > 2500 && v < 5000) _millivolts = v;
        if (read16(REG_CURRENT, v)) _currentMa = (int16_t)v;
        // The gauge only learns a pack over full cycles and can be far off until
        // then (it showed 60% on a full 4.197 V cell). When it disagrees with the
        // voltage by a lot, trust the voltage.
        if (_millivolts) {
            const uint8_t byVolt = fromVoltage(charging() ? _millivolts - 80 : _millivolts);
            _percent = abs((int)_gaugePct - (int)byVolt) > 15 ? byVolt : _gaugePct;
        } else {
            _percent = _gaugePct;
        }
    }

    uint8_t  percent() const { return _percent; }
    uint8_t  gaugePercent() const { return _gaugePct; }
    uint16_t millivolts() const { return _millivolts; }
    // What the gauge held at boot, what it holds now, and whether we rewrote it.
    uint16_t designBefore() const { return _designBefore; }
    uint16_t designNow() const { return _designNow; }
    bool     configured() const { return _configured; }

    // Raw gauge and charger state, for working out why the percentage is off.
    void dump() {
        uint16_t flags = 0, rm = 0, fcc = 0, soc = 0, dc = 0, op = 0, cur = 0, mv = 0, soh = 0;
        read16(0x0A, flags); read16(0x10, rm); read16(0x12, fcc); read16(REG_SOC, soc);
        read16(REG_DESIGN_CAP, dc); read16(REG_OP_STATUS, op); read16(REG_CURRENT, cur);
        read16(REG_VOLTAGE, mv); read16(0x2E, soh);
        Serial.printf("[gauge] %umV %dmA soc%u%% rm%u fcc%u design%u soh%u flags%04X op%04X FC%d DSG%d access%u (boot design %u, set %d)\n",
                      mv, (int16_t)cur, soc, rm, fcc, dc, soh & 0xFF, flags, op, (flags >> 9) & 1, flags & 1,
                      (op >> 1) & 3, _designBefore, _configured);
        uint8_t r[0x15] = {0};
        _w->beginTransmission(ADDR_BQ25896_CHG);
        _w->write((uint8_t)0x00);
        if (_w->endTransmission(false) == 0 && _w->requestFrom((uint8_t)ADDR_BQ25896_CHG, (uint8_t)0x15) == 0x15) {
            for (int i = 0; i < 0x15; i++) r[i] = _w->read();
            static const char* STAT[] = {"not charging", "pre-charge", "fast charge", "done"};
            Serial.printf("[charger] %s, ichg %umA iterm %umA vreg %umV, vbus %s, raw 04=%02X 05=%02X 06=%02X 07=%02X 0B=%02X\n",
                          STAT[(r[0x0B] >> 3) & 3], (r[0x04] & 0x7F) * 64, 64 + (r[0x05] & 0x0F) * 64,
                          3840 + (r[0x06] >> 2) * 16, (r[0x0B] >> 5) ? "yes" : "no",
                          r[0x04], r[0x05], r[0x06], r[0x07], r[0x0B]);
        }
    }

    // The gauge reports current signed: positive is charge going in.
    bool charging() const { return _currentMa > 5; }

private:
    enum : uint8_t {
        REG_CONTROL = 0x00, REG_VOLTAGE = 0x08, REG_CURRENT = 0x0C, REG_SOC = 0x2C,
        REG_OP_STATUS = 0x3A, REG_DESIGN_CAP = 0x3C, REG_ROM_ADDR = 0x3E,
        REG_MAC_DATA = 0x40, REG_MAC_SUM = 0x60, REG_MAC_LEN = 0x61,
    };
    static constexpr uint16_t ROM_FULL_CHARGE_CAP = 0x929D, ROM_DESIGN_CAP = 0x929F;
    static constexpr uint32_t REFRESH_MS = 5000;

    // Typical single-cell Li-ion curve under light load.
    static uint8_t fromVoltage(int mv) {
        static const int16_t MV[]  = {3400, 3500, 3600, 3650, 3700, 3750, 3800, 3900, 4000, 4100, 4170};
        static const uint8_t PCT[] = {   0,    4,   10,   18,   28,   40,   50,   66,   80,   92,  100};
        if (mv <= MV[0]) return 0;
        for (int i = 1; i < 11; i++)
            if (mv < MV[i]) return PCT[i - 1] + (PCT[i] - PCT[i - 1]) * (mv - MV[i - 1]) / (MV[i] - MV[i - 1]);
        return 100;
    }

    bool read16(uint8_t reg, uint16_t& out) {
        _w->beginTransmission(ADDR_BQ27220_GAUGE);
        _w->write(reg);
        if (_w->endTransmission(false) != 0) return false;
        if (_w->requestFrom((uint8_t)ADDR_BQ27220_GAUGE, (uint8_t)2) != 2) return false;
        const uint8_t lo = _w->read(), hi = _w->read();
        out = (uint16_t)(lo | (hi << 8));
        return true;
    }

    bool write(const uint8_t* b, uint8_t n) {
        _w->beginTransmission(ADDR_BQ27220_GAUGE);
        _w->write(b, n);
        return _w->endTransmission() == 0;
    }
    bool chgRead(uint8_t reg, uint8_t& v) {
        _w->beginTransmission(ADDR_BQ25896_CHG);
        _w->write(reg);
        if (_w->endTransmission(false) != 0 || _w->requestFrom((uint8_t)ADDR_BQ25896_CHG, (uint8_t)1) != 1) return false;
        v = _w->read();
        return true;
    }
    bool chgWrite(uint8_t reg, uint8_t v) {
        _w->beginTransmission(ADDR_BQ25896_CHG);
        _w->write(reg); _w->write(v);
        return _w->endTransmission() == 0;
    }
    bool writeReg(uint8_t reg, uint8_t v) { const uint8_t b[] = {reg, v}; return write(b, 2); }
    bool control(uint16_t cmd) { const uint8_t b[] = {REG_CONTROL, (uint8_t)cmd, (uint8_t)(cmd >> 8)}; delay(10); return write(b, 3); }

    bool configUpdateMode(bool want, uint32_t timeoutMs) {
        const uint32_t t0 = millis();
        uint16_t st;
        while (millis() - t0 < timeoutMs) {
            if (read16(REG_OP_STATUS, st) && (((st & 0x0400) != 0) == want)) return true;
            delay(100);
        }
        return false;
    }

    // One data-memory word: address, big-endian value, checksum, length.
    void writeRomWord(uint16_t addr, uint16_t value) {
        const uint8_t a0 = (uint8_t)addr, a1 = (uint8_t)(addr >> 8);
        const uint8_t hi = (uint8_t)(value >> 8), lo = (uint8_t)value;
        writeReg(REG_ROM_ADDR, a0); delay(10);
        writeReg(REG_ROM_ADDR + 1, a1); delay(10);
        const uint8_t data[] = {REG_MAC_DATA, hi, lo};
        write(data, 3);
        writeReg(REG_MAC_SUM, (uint8_t)(0xFF - ((a0 + a1 + hi + lo) & 0xFF)));
        writeReg(REG_MAC_LEN, 0x06);
        delay(10);
    }

    // Unseal with TI's default keys, enter config update, write design and full
    // charge capacity, then exit with a reinit and seal again if it was sealed.
    bool setCapacity(uint16_t mah) {
        uint16_t st = 0;
        if (!read16(REG_OP_STATUS, st)) return false;
        const uint8_t access = (st >> 1) & 0x03;       // 1 full, 2 unsealed, 3 sealed
        const bool wasSealed = access == 3;
        if (wasSealed) { control(0x0414); control(0x3672); }
        if (access != 1) { control(0xFFFF); control(0xFFFF); }
        control(0x0090);                                // ENTER_CFG_UPDATE
        if (!configUpdateMode(true, 1500)) return false;
        writeRomWord(ROM_DESIGN_CAP, mah);
        writeRomWord(ROM_FULL_CHARGE_CAP, mah);
        control(0x0091);                                // EXIT_CFG_UPDATE_REINIT
        const bool ok = configUpdateMode(false, 3000);
        if (wasSealed) control(0x0030);
        return ok;
    }

    TwoWire* _w = nullptr;
    bool     _gauge = false, _configured = false, _resynced = false;
    uint8_t  _percent = 0, _gaugePct = 0;
    uint16_t _millivolts = 0, _designBefore = 0, _designNow = 0;
    int16_t  _currentMa = 0;
    uint32_t _last = 0, _lastDump = 0;
};
