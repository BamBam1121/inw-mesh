// T-Deck battery: no fuel gauge and no charger chip on I2C, just one voltage halved
// onto GPIO4. It is the board's supply after the switch-over between USB and the cell
// (so the cell on battery, the USB supply when plugged in). What that means - the
// percentage, and whether it is on USB power - is worked out in battery_est.h, which
// a PC can test.
//
// Read with analogReadMilliVolts (eFuse-calibrated), per Wadamesh: the S3's ADC
// under-reads near the top of its range, so the plain analogRead*3.3/4096 sum
// MeshCore uses shows a full cell as ~3.78 V (about 53%). 2.037 is their measured
// divider ratio.
//
// Same interface as the pager's BQ27220/BQ25896 version; what this board can't
// do (charge limits, ship mode, gauge learning) answers "not here" and changes
// nothing.

#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include "board_pins.h"
#include "battery_est.h"

class Battery {
public:
    bool begin(TwoWire& = Wire) {
        analogReadResolution(12);
        Preferences p;
        if (p.begin("inw-batt", false)) {
            // Firmware before 2026-10-09 kept a "correction" here that it had learned from
            // the USB supply, taking it for a full cell: it made every reading on battery
            // up to 10% low. Thrown away.
            if (p.isKey("cal")) p.remove("cal");
            // How fast this unit's cell fills on USB, learned from its own charges, and the
            // figure it last showed (all there is to go on if it starts up on USB).
            const float r = p.getFloat("rate", BatteryEstimate::RATE_DEFAULT);
            if (r >= BatteryEstimate::RATE_MIN && r <= BatteryEstimate::RATE_MAX) _est.rate = r;
            // And how fast it runs down on battery, as this owner uses it.
            const float d = p.getFloat("drain", 0);
            if (d > 0.05f && d < 100.0f) _est.drain = d;
            _saved = p.getUChar("pct", 255);
            if (_saved <= 100) _est.remember(_saved);
            p.end();
        }
        // The first conversion after the pin is set up can come out low, and it used to
        // be the whole of the first figure: throw one away and take the middle of three.
        sample();
        uint16_t a = sample(); delay(20);
        uint16_t b = sample(); delay(20);
        uint16_t c = sample();
        if (a > b) { const uint16_t t = a; a = b; b = t; }
        _est.update(millis(), c <= a ? a : c >= b ? b : c, HWCDC::isPlugged());
        _wasExternal = _est.external;
        return true;
    }

    // The T-Deck's own draw changed by a lot (main.cpp): what the voltage does next is
    // that, not the battery running down (battery_est.h).
    void drawChanged(uint32_t now, int mA) { _est.drawChanged(now, mA); }

    bool present() const { return true; }
    bool hasReading() const { return _est.mv > 2500; }

    void tick(uint32_t now) {
        if (now - _lastRead < BatteryEstimate::PERIOD_MS) return;
        _lastRead = now;
        _est.update(now, sample(), HWCDC::isPlugged());
        // Kept across restarts: the rate when a charge has taught a new one, and the figure
        // whenever it has moved three points or the cable went in or out (a few writes a day).
        const bool rate = _est.rateChanged, drain = _est.drainChanged;
        const int moved = (int)_est.percent - (int)_saved;
        if (rate || drain || _saved > 100 || moved >= 3 || moved <= -3 || _est.external != _wasExternal) {
            _est.rateChanged = _est.drainChanged = false;
            _wasExternal = _est.external;
            Preferences p;
            if (p.begin("inw-batt", false)) {
                if (rate) p.putFloat("rate", _est.rate);
                if (drain) p.putFloat("drain", _est.drain);
                if (_saved != _est.percent) { _saved = _est.percent; p.putUChar("pct", _saved); }
                p.end();
            }
        }
    }

    uint8_t  percent() const { return _est.percent; }
    uint8_t  gaugePercent() const { return _est.percent; }
    // On battery, the cell. On USB the pin shows the supply, not the cell (still what a
    // flat-battery check at start-up needs: it is not flat).
    uint16_t millivolts() const { return _est.mv; }
    // The cell as last seen: stands still while on USB. 0 if it never has been.
    uint16_t cellMillivolts() const { return _est.cellMv; }

    // A computer on the USB port says so; any USB supply shows as a voltage no cell can
    // have (battery_est.h).
    bool pluggedIn() const { return _est.external; }
    bool pollVbus() { return pluggedIn(); }
    bool charging() const { return _est.external && !_est.full && _est.percent < 100; }
    // The percentage is from memory or a guess: it started on USB and hasn't seen the cell.
    bool guessing() const { return !_est.known; }

    // The pager's gauge and charger controls: nothing to drive here.
    float    remainingMah() const { return -1; }
    void     restoreMah(float, bool) {}
    uint16_t fullChargeMah() const { return 0; }
    uint16_t designNow() const { return 0; }
    bool     configured() const { return false; }
    bool     relearn() { return false; }
    bool     shipMode() { return false; }
    void     setHiZ(bool) {}
    void     holdCharge(bool) {}
    bool     chargeHeld() const { return false; }

    // By hand, from a USB test command: how fast this cell fills, and what it holds now.
    void setRate(float perHour) {
        _est.rate = constrain(perHour, BatteryEstimate::RATE_MIN, BatteryEstimate::RATE_MAX);
        _est.rateChanged = true;
    }
    void setPercent(uint8_t pct) { _est.setPercent(millis(), pct); }
    float rate() const { return _est.rate; }
    // Minutes until it is reckoned full; 0 when it is not charging (battery_est.h).
    uint16_t minutesToFull() const { return _est.minutesToFull(); }
    // Minutes it will last on battery at the rate it has been running down; 0 on USB.
    // leftKnown: that rate has been seen on this unit, not guessed from the cell's size.
    uint16_t minutesLeft() const { return _est.minutesLeft(millis()); }
    bool leftKnown() const { return _est.leftKnown(millis()); }
    float drain() const { return _est.drain; }

    void report() {
        if (_est.external)
            Serial.printf("[batt] on USB power: pin %u mV (the supply), cell last seen %u mV, %u%%%s, %s, fills %.1f%%/h, full in %u min\n",
                          _est.mv, _est.cellMv, _est.percent, _est.known ? "" : " (from memory)",
                          _est.full ? "reckoned full" : "charging", _est.rate, _est.minutesToFull());
        else
            Serial.printf("[batt] on battery: cell %u mV, %u%%, fills %.1f%%/h on USB, runs down %.2f%%/h (%s), %u min left\n",
                          _est.mv, _est.percent, _est.rate, _est.drain, leftKnown() ? "seen" : "not seen yet: a guess", minutesLeft());
    }
    void configReport() { Serial.println("[batt] no fuel gauge on this board"); }

private:
    static uint16_t sample() {
        uint32_t sum = 0;
        for (int i = 0; i < 8; i++) sum += analogReadMilliVolts(PIN_BAT_ADC);
        return (uint16_t)(2.037f * sum / 8);
    }

    BatteryEstimate _est;
    uint32_t _lastRead = 0;
    uint8_t _saved = 255;             // the figure in storage; 255: none
    bool _wasExternal = false;
};
