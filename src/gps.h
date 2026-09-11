// MIA-M10Q NMEA reader. Only RMC and GGA are parsed: fix, position, satellites
// and UTC time are all the rest of the firmware needs. Power EXP_GPS_EN first.

#pragma once
#include <Arduino.h>
#include "board_pins.h"

struct GpsFix {
    bool     valid = false;      // RMC reported an active fix
    double   lat = 0, lon = 0;
    float    altitudeM = 0;
    uint8_t  satellites = 0;
    uint16_t year = 0;
    uint8_t  month = 0, day = 0, hour = 0, minute = 0, second = 0;
    uint32_t updatedAt = 0;      // millis() of the last good sentence
};

class Gps {
public:
    bool begin(HardwareSerial& uart = Serial1) {
        _uart = &uart;
        _uart->begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
        pinMode(PIN_GPS_PPS, INPUT);
        _started = true;
        return true;
    }

    // Call every loop. Returns true on the tick a sentence completed a parse.
    bool update() {
        bool parsed = false;
        while (_uart && _uart->available()) {
            const char c = (char)_uart->read();
            if (c == '$') { _len = 0; _line[_len++] = c; continue; }
            if (_len == 0) continue;                       // mid-sentence at startup
            if (c == '\r' || c == '\n') {
                _line[_len] = '\0';
                if (validChecksum()) parsed |= parse();
                _len = 0;
                continue;
            }
            if (_len < sizeof(_line) - 1) _line[_len++] = c;
            else _len = 0;                                 // overlong, resync
        }
        return parsed;
    }

    const GpsFix& fix() const { return _fix; }
    bool hasFix() const { return _fix.valid; }
    bool started() const { return _started; }

    // True once the fix is fresh enough to trust. GPS keeps reporting the last
    // position after the antenna is covered, so age matters more than validity.
    bool fresh(uint32_t maxAgeMs = 10000) const {
        return _fix.valid && (millis() - _fix.updatedAt) < maxAgeMs;
    }

private:
    // NMEA checksum is an XOR of everything between '$' and '*'.
    bool validChecksum() const {
        const char* star = strrchr(_line, '*');
        if (!star || star - _line < 2 || strlen(star) < 3) return false;
        uint8_t sum = 0;
        for (const char* p = _line + 1; p < star; p++) sum ^= (uint8_t)*p;
        return sum == (uint8_t)strtol(star + 1, nullptr, 16);
    }

    // Splits the sentence in place. Returns nullptr past the end.
    static char* field(char* s, uint8_t index) {
        for (uint8_t i = 0; i < index; i++) {
            s = strchr(s, ',');
            if (!s) return nullptr;
            s++;
        }
        return s;
    }

    // NMEA packs degrees and minutes together as ddmm.mmmm.
    static double degMin(const char* s, char hemi) {
        if (!s || *s == ',' || *s == '\0') return 0;
        const double raw = atof(s);
        const double deg = floor(raw / 100.0);
        const double val = deg + (raw - deg * 100.0) / 60.0;
        return (hemi == 'S' || hemi == 'W') ? -val : val;
    }

    bool parse() {
        const char* type = _line + 3;                      // skip "$GP" / "$GN"
        if (!strncmp(type, "RMC", 3)) return parseRmc();
        if (!strncmp(type, "GGA", 3)) return parseGga();
        return false;
    }

    bool parseRmc() {
        char* f = field(_line, 1);                         // hhmmss.ss
        if (f && strlen(f) >= 6) {
            _fix.hour   = (uint8_t)((f[0] - '0') * 10 + (f[1] - '0'));
            _fix.minute = (uint8_t)((f[2] - '0') * 10 + (f[3] - '0'));
            _fix.second = (uint8_t)((f[4] - '0') * 10 + (f[5] - '0'));
        }
        char* status = field(_line, 2);
        _fix.valid = status && *status == 'A';

        char* lat = field(_line, 3); char* ns = field(_line, 4);
        char* lon = field(_line, 5); char* ew = field(_line, 6);
        if (_fix.valid && lat && ns && lon && ew) {
            _fix.lat = degMin(lat, *ns);
            _fix.lon = degMin(lon, *ew);
        }

        char* date = field(_line, 9);                      // ddmmyy
        if (date && strlen(date) >= 6) {
            _fix.day   = (uint8_t)((date[0] - '0') * 10 + (date[1] - '0'));
            _fix.month = (uint8_t)((date[2] - '0') * 10 + (date[3] - '0'));
            _fix.year  = (uint16_t)(2000 + (date[4] - '0') * 10 + (date[5] - '0'));
        }
        if (_fix.valid) _fix.updatedAt = millis();
        return true;
    }

    bool parseGga() {
        char* sats = field(_line, 7);
        if (sats) _fix.satellites = (uint8_t)atoi(sats);
        char* alt = field(_line, 9);
        if (alt) _fix.altitudeM = (float)atof(alt);
        return true;
    }

    HardwareSerial* _uart = nullptr;
    char     _line[100] = {0};
    uint8_t  _len = 0;
    bool     _started = false;
    GpsFix   _fix;
};
