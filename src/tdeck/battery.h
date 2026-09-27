// T-Deck battery: no fuel gauge and no charger chip on I2C, just the cell
// voltage halved onto GPIO4. The percentage is read off a Li-ion discharge
// curve, smoothed so it doesn't jitter with radio bursts.
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
#include "board_pins.h"

class Battery {
public:
    bool begin(TwoWire& = Wire) {
        analogReadResolution(12);
        _mv = sample();
        _haveReading = _mv > 2500;
        _percent = fromCurve(_mv);
        return true;
    }

    bool present() const { return true; }
    bool hasReading() const { return _haveReading; }

    void tick(uint32_t now) {
        if (now - _lastRead < 2000) return;
        _lastRead = now;
        const uint16_t mv = sample();
        if (mv < 2500) return;                         // nothing sensible on the pin
        // Heavy smoothing: a transmit burst sags the cell for a moment.
        _mv = _haveReading ? (uint16_t)((_mv * 7u + mv) / 8u) : mv;
        _haveReading = true;
        const uint8_t p = fromCurve(_mv);
        // While on battery it only goes down; a jump up means it was plugged in.
        if (pluggedIn() || p < _percent || p > _percent + 5) _percent = p;
    }

    uint8_t  percent() const { return _percent; }
    uint8_t  gaugePercent() const { return _percent; }
    uint16_t millivolts() const { return _mv; }

    // A computer on the USB port is visible; a wall charger isn't, but it lifts
    // the cell above what a battery alone sits at.
    bool pluggedIn() const { return HWCDC::isPlugged() || _mv >= 4230; }
    bool pollVbus() { return pluggedIn(); }
    bool charging() const { return pluggedIn() && _percent < 100; }

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

    void report() {
        Serial.printf("[batt] %u mV, %u%%, %s (T-Deck: voltage only)\n", _mv, _percent,
                      pluggedIn() ? "on USB" : "on battery");
    }
    void configReport() { Serial.println("[batt] no fuel gauge on this board"); }

private:
    static uint16_t sample() {
        uint32_t sum = 0;
        for (int i = 0; i < 8; i++) sum += analogReadMilliVolts(PIN_BAT_ADC);
        return (uint16_t)(2.037f * sum / 8);
    }

    // Resting Li-ion cell, lightly loaded.
    static uint8_t fromCurve(uint16_t mv) {
        static const uint16_t MV[]  = {4180, 4100, 4000, 3920, 3850, 3800, 3750, 3710, 3670, 3620, 3500, 3300};
        static const uint8_t  PCT[] = { 100,   90,   80,   70,   60,   50,   40,   30,   20,   10,    5,    0};
        if (mv >= MV[0]) return 100;
        for (int i = 1; i < 12; i++)
            if (mv >= MV[i]) return PCT[i] + (uint8_t)((uint32_t)(PCT[i - 1] - PCT[i]) * (mv - MV[i]) / (MV[i - 1] - MV[i]));
        return 0;
    }

    uint16_t _mv = 0;
    uint8_t  _percent = 0;
    bool     _haveReading = false;
    uint32_t _lastRead = 0;
};
