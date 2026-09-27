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
        uint8_t r00;
        if (chgRead(0x00, r00) && (r00 & 0x80)) chgWrite(0x00, r00 & ~0x80);   // input back on after a test
        uint8_t r09;
        if (chgRead(0x09, r09) && (r09 & 0x20)) chgWrite(0x09, r09 & ~0x20);   // battery back on after a power-off
        chgWrite(0x05, r05 & 0xF0);                     // ITERM = 64 mA, pre-charge unchanged
        chgWrite(0x07, r07 & ~0x30);                    // WATCHDOG disabled
        // Charge at 1472 mA, about 1C for this 1500 mAh cell. It was charging at
        // 2 A, which heats and wears a small pack for little time saved.
        uint8_t r04;
        if (chgRead(0x04, r04)) chgWrite(0x04, (r04 & 0x80) | 23);
        uint8_t r03;
        if (chgRead(0x03, r03) && !(r03 & 0x10)) chgWrite(0x03, r03 | 0x10);   // charging on (clears any old hold)
    }

    // If the charger is done but the gauge never flagged full, run the tail of the
    // charge again (toggle CHG_CONFIG) so the gauge can see the taper. Once a boot.
    void resyncFull() {
        if (_resynced || _held) return;
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
    bool hasReading() const { return _haveReading; }    // false until one good read

    // Cached: the status bar asks every frame and I2C at 400 kHz is not free.
    // The bus is shared with the keyboard, expander and codec, so a read fails
    // now and then; keep the last good value instead of showing 0%.
    void tick(uint32_t now) {
        if (!_gauge || (now - _last < REFRESH_MS && _last)) return;
        _last = now;
        if (now - _lastDump > 30000 || !_lastDump) { _lastDump = now | 1; dump(); }
        if (now > 20000) resyncFull();
        uint16_t v;
        if (read16(REG_SOC, v) && v <= 100) { _gaugePct = (uint8_t)v; _haveReading = true; }
        if (read16(REG_VOLTAGE, v) && v > 2500 && v < 5000) { _millivolts = v; _haveReading = true; }
        if (read16(REG_CURRENT, v)) _currentMa = (int16_t)v;
        if (read16(0x12, v) && v > 100 && v < 5000) _fcc = v;
        uint8_t st;
        if (chgRead(0x0B, st)) {
            _vbus = (st >> 5) != 0;
            _chgDone = _vbus && ((st >> 3) & 3) == 3;
        }
        // ---- our own charge count -------------------------------------------------
        // The gauge's percentage can't be trusted: its battery model is TI's generic
        // default (a 3000 mAh cell), it has never once flagged a full charge, and it
        // "learned" this 1500 mAh pack down to 865 mAh, so its figure fell ~1.6x too
        // fast. Its coulomb counter is exact, though. So: count every mAh in and out
        // from that counter; call it 100% only when the charger says it has finished;
        // and at rest, drift gently toward what the voltage says if the two disagree
        // a lot, which mops up any error without the number ever jumping.
        const bool up = charging() || _chgDone;
        if (_millivolts) {
            const int drawMa = _currentMa < 0 ? -_currentMa : 0;   // cell resistance ~200 mOhm with wiring
            const int restMv = _millivolts + (up ? 0 : drawMa * 200 / 1000);
            _restMv = _restMv ? (_restMv * 3 + restMv) / 4 : restMv;
            _byVolt = fromVoltage(_restMv);
        }
        uint16_t raw;
        if (read16(REG_RAW_CC, raw)) {                 // mAh, up on discharge, down on charge
            if (_ccValid) {
                const int16_t d = (int16_t)(raw - _ccLast);
                if (abs(d) <= 60) _mah -= d;              // more in 5 s is a reset or a bad read
            }
            _ccLast = raw;
            _ccValid = true;
        }
        if (!_mahKnown && _millivolts) {               // first reading after boot
            const float byVolt = CAP_MAH * _byVolt / 100.0f;
            // A saved figure from before a power off or restart is better than the
            // voltage, unless the two are far apart (charged or used while we weren't
            // counting).
            _mah = (_savedMah >= 0 && fabsf(_savedMah - byVolt) < CAP_MAH * 0.25f && !up) ? _savedMah : byVolt;
            if (up && _savedMah >= 0) _mah = max(_savedMah, byVolt * 0.9f);
            _mahKnown = true;
        }
        if (_chgDone) _mah = CAP_MAH;                   // the only reliable 100%
        else if (_mahKnown && !up && abs(_currentMa) < 130) {
            const float byVolt = CAP_MAH * _byVolt / 100.0f;
            if (fabsf(byVolt - _mah) > CAP_MAH * 0.12f) _mah += (byVolt - _mah) * 0.02f;
        }
        _mah = constrain(_mah, 0.0f, (float)CAP_MAH);
        uint8_t target = (uint8_t)(_mah * 100.0f / CAP_MAH + 0.5f);
        if (up && !_chgDone && target > 99) target = 99; // not full until the charger says so
        if (!up && _millivolts && _millivolts < 3480 && target > 5) target = 5;   // nearly flat whatever the count says
        // Show it the way a phone does: charging it only counts up, otherwise it
        // only counts down, one step at a time, so a noisy read never makes it bounce.
        _target = target;
        if (!_shown) { _percent = target; _shown = true; }
        else if (up) { if (target > _percent) _percent++; }
        else if (target < _percent) _percent--;
    }

    // What main.cpp keeps across restarts (RTC memory) and power offs (flash).
    float remainingMah() const { return _mahKnown ? _mah : -1; }
    void  restoreMah(float mah, bool exact) {
        if (mah < 0 || mah > CAP_MAH) return;
        if (exact) { _mah = mah; _mahKnown = true; }  // a restart: nothing happened in between
        else _savedMah = mah;                           // a power off: check it against the voltage
    }

    // One line with everything the figure was worked out from.
    void report() {
        Serial.printf("[batt] shown %u%% target %u%% | counted %.0f/%u mAh | voltage says %u%% | gauge says %u%% (fcc %u) | %umV rest %dmV %dmA | vbus %d done %d charging %d hiz %d\n",
                      _percent, _target, _mah, CAP_MAH, _byVolt, _gaugePct, _fcc, _millivolts, _restMv, _currentMa,
                      _vbus, _chgDone, charging(), hiZ());
    }

    // Test aid: cut the charger's input so the pager runs from its battery while
    // the USB cable stays in. begin() always turns it back off.
    void setHiZ(bool on) {
        uint8_t r;
        if (chgRead(0x00, r)) chgWrite(0x00, on ? (r | 0x80) : (r & ~0x80));
        _last = 0;
    }
    bool hiZ() { uint8_t r; return chgRead(0x00, r) && (r & 0x80); }

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

    // The gauge's own settings that decide when it calls the pack full, read-only.
    // It only resets its count to 100% after two 40 s windows with the charge
    // current under Taper Current yet still flowing, above Charging Voltage minus
    // Taper Voltage - if those don't match what the charger does, it never learns.
    void configReport() {
        struct { uint16_t addr; const char* name; bool hex; } R[] = {
            {0x91FD, "charging voltage mV", false}, {0x9201, "taper current mA", false},
            {0x92A5, "taper voltage mV", false},    {0x929D, "full charge cap mAh", false},
            {0x929F, "design cap mAh", false},      {0x9206, "op config A", true},
            {0x9208, "op config B", true},          {0x929B, "gauging config", true},
        };
        uint16_t st = 0;
        read16(REG_OP_STATUS, st);
        Serial.printf("[gauge] access %u (1 full, 2 unsealed, 3 sealed)\n", (st >> 1) & 3);
        for (auto& r : R) {
            uint16_t v;
            if (readRomWord(r.addr, v)) Serial.printf(r.hex ? "[gauge] %-20s 0x%04X\n" : "[gauge] %-20s %u\n", r.name, v);
            else Serial.printf("[gauge] %-20s (unreadable)\n", r.name);
        }
        uint16_t flags = 0, rm = 0, fcc = 0;
        read16(0x0A, flags); read16(0x10, rm); read16(0x12, fcc);
        Serial.printf("[gauge] now: RM %u / FCC %u mAh, FC %d, CHGINH %d\n", rm, fcc, (flags >> 9) & 1, (flags >> 3) & 1);
    }

    // Throw away what the gauge has learned and start from the pack's rating.
    // It re-learns over the next full charge and discharge.
    bool relearn() { return setCapacity(DESIGN_MAH); }

    // Power off: the charger disconnects the battery from everything (BATFET_DIS,
    // ship mode, ~26 uA). It only takes hold with USB unplugged. The PWR button
    // (wired to the charger's QON, not to a GPIO) or plugging in brings it back.
    bool shipMode() {
        uint8_t r09;
        if (!_gauge || !chgRead(0x09, r09)) return false;
        return chgWrite(0x09, (r09 | 0x20) & ~0x08);   // BATFET off, no 10 s delay
    }

    bool     pluggedIn() const { return _vbus; }
    // Cheap single-register check, so plugging in is noticed within a moment.
    bool pollVbus() {
        uint8_t st;
        if (_gauge && chgRead(0x0B, st)) _vbus = (st >> 5) != 0;
        return _vbus;
    }
    uint16_t fullChargeMah() const { return _fcc; }

    // Pause or resume charging (optimised charging holds at 80%). Only the
    // charger's enable bit changes; voltage, current and safety limits don't.
    // A hold never survives a reboot: begin() re-enables charging.
    void holdCharge(bool hold) {
        uint8_t r03;
        if (hold == _held || !chgRead(0x03, r03)) return;
        if (chgWrite(0x03, hold ? (r03 & ~0x10) : (r03 | 0x10))) _held = hold;
    }
    bool chargeHeld() const { return _held; }

    // The gauge reports current signed: positive is charge going in.
    bool charging() const { return _currentMa > 5; }

private:
    enum : uint8_t {
        REG_CONTROL = 0x00, REG_VOLTAGE = 0x08, REG_CURRENT = 0x0C, REG_SOC = 0x2C,
        REG_OP_STATUS = 0x3A, REG_DESIGN_CAP = 0x3C, REG_ROM_ADDR = 0x3E,
        REG_MAC_DATA = 0x40, REG_MAC_SUM = 0x60, REG_MAC_LEN = 0x61, REG_RAW_CC = 0x22,
    };
    static constexpr uint16_t ROM_FULL_CHARGE_CAP = 0x929D, ROM_DESIGN_CAP = 0x929F;
    // What 100% holds: the 1500 mAh rating less a little for age and the charger
    // stopping at 64 mA. The resting-voltage drift corrects it if it's off.
    static constexpr uint16_t CAP_MAH = 1450;
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

    // Read one data-memory word: point MACDataControl at the address, then the
    // block appears at MACData, big-endian.
    bool readRomWord(uint16_t addr, uint16_t& out) {
        const uint8_t sel[] = {REG_ROM_ADDR, (uint8_t)addr, (uint8_t)(addr >> 8)};
        if (!write(sel, 3)) return false;
        delay(12);
        _w->beginTransmission(ADDR_BQ27220_GAUGE);
        _w->write((uint8_t)REG_ROM_ADDR);
        if (_w->endTransmission(false) != 0) return false;
        if (_w->requestFrom((uint8_t)ADDR_BQ27220_GAUGE, (uint8_t)4) != 4) return false;
        const uint8_t a0 = _w->read(), a1 = _w->read(), hi = _w->read(), lo = _w->read();
        if (((a1 << 8) | a0) != addr) return false;       // the gauge didn't take the address
        out = (uint16_t)((hi << 8) | lo);
        return true;
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
    bool     _haveReading = false;
    bool     _gauge = false, _configured = false, _resynced = false, _held = false, _vbus = false;
    uint16_t _fcc = 0;
    uint8_t  _percent = 0, _gaugePct = 0;
    uint16_t _millivolts = 0, _designBefore = 0, _designNow = 0;
    int16_t  _currentMa = 0;
    bool     _shown = false, _chgDone = false;
    uint8_t  _target = 0, _byVolt = 0;
    float    _mah = 0, _savedMah = -1;          // our count of what's left
    bool     _mahKnown = false, _ccValid = false;
    uint16_t _ccLast = 0;
    int      _restMv = 0;
    uint32_t _last = 0, _lastDump = 0;
};
