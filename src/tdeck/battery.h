// T-Deck battery: no fuel gauge and no charger chip on I2C, just the cell
// voltage halved onto GPIO4. What that voltage means - the percentage, and whether
// a charger is in - is worked out in battery_est.h, which a PC can test.
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
        // This unit's reading correction, learned at the end of earlier full charges.
        Preferences p;
        if (p.begin("inw-batt", true)) {
            const float c = p.getFloat("cal", 1.0f);
            if (c > 0.89f && c < 1.11f) _est.cal = c;
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
        return true;
    }

    // The T-Deck's own draw changed by a lot (main.cpp): what the voltage does next is
    // that, not a charger going in or out (battery_est.h).
    void drawChanged(uint32_t now, int mA) { _est.drawChanged(now, mA); }

    bool present() const { return true; }
    bool hasReading() const { return _est.mv > 2500; }

    void tick(uint32_t now) {
        if (now - _lastRead < BatteryEstimate::PERIOD_MS) return;
        _lastRead = now;
        _est.update(now, sample(), HWCDC::isPlugged());
        if (_est.calChanged) {                   // a full charge corrected the reading: keep it
            _est.calChanged = false;
            Preferences p;
            if (p.begin("inw-batt", false)) { p.putFloat("cal", _est.cal); p.end(); }
        }
    }

    uint8_t  percent() const { return _est.percent; }
    uint8_t  gaugePercent() const { return _est.percent; }
    uint16_t millivolts() const { return _est.mv; }

    // A computer on the USB port says so; a wall charger shows as the step it puts
    // on the cell's voltage (battery_est.h).
    bool pluggedIn() const { return _est.external; }
    bool pollVbus() { return pluggedIn(); }
    bool charging() const { return _est.external && !_est.full && _est.percent < 100; }

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
        Serial.printf("[batt] %u mV, %u%%, %s, reading x%.4f (T-Deck: voltage only)\n", _est.mv, _est.percent,
                      _est.full ? "full, on power" : _est.external ? "charging" : "on battery", _est.cal);
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
};
