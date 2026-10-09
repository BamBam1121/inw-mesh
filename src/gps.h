// MIA-M10Q NMEA reader. RMC and GGA give the fix, position, satellites and UTC time,
// which is all the rest of the firmware needs. GSV and the antenna line are read
// only to say what a module with no fix yet can hear. Power EXP_GPS_EN first.

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
        // A second's worth of NMEA (~500 bytes) must fit between two loop passes.
        _uart->setRxBufferSize(2048);
#ifdef GPS_BAUD_ALT
        _uart->begin(baud(), SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);   // the speed it last found
#else
        _uart->begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
#endif
        if (PIN_GPS_PPS >= 0) pinMode(PIN_GPS_PPS, INPUT);
        _started = true;
        return true;
    }

    // Call every loop. Returns true on the tick a sentence completed a parse.
    bool update() {
        bool parsed = false;
        if (!_uart) return false;
        // Take only what is already buffered, in one call. Reading byte by byte
        // goes through the driver lock each time and is slower than the GPS
        // sends, so the loop used to chase the whole burst for ~250 ms.
        uint8_t buf[256];
        const int avail = _uart->available();
        const size_t n = avail > 0 ? _uart->read(buf, min((size_t)avail, sizeof(buf))) : 0;
        bytesRead += n;
        for (size_t i = 0; i < n; i++) {
            const char c = (char)buf[i];
            if (c == '$') { _len = 0; _line[_len++] = c; continue; }
            if (_len == 0) continue;                       // mid-sentence at startup
            if (c == '\r' || c == '\n') {
                _line[_len] = '\0';
                if (validChecksum()) { goodSentences++; parsed |= parse(); }
                _len = 0;
                continue;
            }
            if (_len < sizeof(_line) - 1) _line[_len++] = c;
            else _len = 0;                                 // overlong, resync
        }
#ifdef GPS_BAUD_ALT
        // A board whose GPS module has shipped at more than one speed: plenty of
        // bytes but not one sentence that checks out means the wrong speed.
        if (!goodSentences && bytesRead - _probeFrom > 1500) {
            _alt = !_alt;
            _uart->updateBaudRate(_alt ? GPS_BAUD_ALT : GPS_BAUD);
            _probeFrom = bytesRead;
            _len = 0;
        }
#endif
        return parsed;
    }

    // Sleep and wake by command, for a board with no power switch for its GPS (the
    // T-Deck: the pager cuts the module's rail instead). The u-blox M10 takes
    // UBX-RXM-PMREQ: backup mode, until anything arrives on its serial line. Only
    // done once it has been heard at 38400 - the u-blox speed; the L76K some T-Deck
    // Plus batches carry (9600) doesn't know the message and simply stays on.
    bool sleepByCommand() {
        if (!_uart || !goodSentences) return false;
#ifdef GPS_BAUD_ALT
        if (_alt) return false;
#endif
        uint8_t m[8 + 16] = {0xB5, 0x62, 0x02, 0x41, 16, 0,
                             0, 0, 0, 0,          // version 0, reserved
                             0, 0, 0, 0,          // duration 0 = until woken
                             0x06, 0, 0, 0,       // flags: backup, force
                             0x08, 0, 0, 0};      // wake on UART RX
        uint8_t a = 0, b = 0;
        for (size_t i = 2; i < 6 + 16; i++) { a += m[i]; b += a; }
        m[22] = a;
        m[23] = b;
        _uart->write(m, sizeof(m));
        _uart->flush();
        _asleep = true;
        _wokeAt = 0;
        return true;
    }
    // Any byte wakes it; the first may be lost while it comes up, so keep nudging
    // (from tick) until sentences flow again.
    void wakeByCommand() {
        if (!_uart || !_asleep) return;
        static const uint8_t nudge[] = {0xFF, 0xFF, 0xFF, 0xFF};
        _uart->write(nudge, sizeof(nudge));
        _asleep = false;
        _wokeAt = millis();
        _wakeFrom = goodSentences;
    }
    void wakeTick() {
        if (!_uart || _asleep || !_wokeAt) return;
        if (goodSentences != _wakeFrom) { _wokeAt = 0; return; }        // talking again
        if (millis() - _wokeAt > 2000) {                                 // not yet: nudge again
            static const uint8_t nudge[] = {0xFF, 0xFF, 0xFF, 0xFF};
            _uart->write(nudge, sizeof(nudge));
            _wokeAt = millis();
        }
    }
    bool asleep() const { return _asleep; }

    const GpsFix& fix() const { return _fix; }
    bool hasFix() const { return _fix.valid; }

    // What the module can hear before it has a fix: the way to tell "needs open sky"
    // from a dead antenna, and the first number to climb outdoors. Satellites with a
    // signal right now across every system it reports, and the strongest of them in
    // dB-Hz (about 20 is barely there, 40 is a clear sky).
    uint8_t heard() const {
        uint16_t n = 0;
        for (const Sys& y : _sys) if (y.a && millis() - y.at < 5000) n += y.heard;
        return n > 255 ? 255 : (uint8_t)n;
    }
    uint8_t bestSignal() const {
        uint8_t m = 0;
        for (const Sys& y : _sys) if (y.a && millis() - y.at < 5000 && y.best > m) m = y.best;
        return m;
    }
    // It has the time, which it only gets by decoding a satellite (or from its own
    // backup battery, if the board has one that is charged).
    bool timeKnown() const { return _timeKnown; }
    // The module's own antenna check, on boards that send one: "ok", "open", "short",
    // or "" when it has said nothing.
    const char* antenna() const { return _antenna == 1 ? "ok" : _antenna == 2 ? "open" : _antenna == 3 ? "short" : ""; }
    bool started() const { return _started; }
    uint32_t bytesRead = 0;
    uint32_t goodSentences = 0;   // with a valid checksum, of any kind
#ifdef GPS_BAUD_ALT
    uint32_t baud() const { return _alt ? GPS_BAUD_ALT : GPS_BAUD; }
#endif

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
        if (!strncmp(type, "GSV", 3)) parseGsv();
        else if (!strncmp(type, "TXT", 3)) parseTxt();
        return false;
    }

    // $GPGSV,messages,this one,in view,{id,elevation,azimuth,signal} x up to 4. One set
    // per system (GP, BD, GL, GA). "In view" also counts satellites it only expects to
    // be up there, so the ones with a signal are counted instead.
    void parseGsv() {
        Sys* y = nullptr;
        for (Sys& c : _sys) if (c.a == _line[1] && c.b == _line[2]) { y = &c; break; }
        if (!y) for (Sys& c : _sys) if (!c.a) { y = &c; c.a = _line[1]; c.b = _line[2]; break; }
        if (!y) return;
        const char* total = field(_line, 1);
        const char* num = field(_line, 2);
        if (!total || !num) return;
        if (atoi(num) <= 1) { y->pendHeard = 0; y->pendBest = 0; }
        for (uint8_t i = 7; i <= 19; i += 4) {
            const char* snr = field(_line, i);
            if (!snr) break;
            const int v = atoi(snr);
            if (v <= 0 || v > 99) continue;
            y->pendHeard++;
            if (v > y->pendBest) y->pendBest = (uint8_t)v;
        }
        if (atoi(num) >= atoi(total)) { y->heard = y->pendHeard; y->best = y->pendBest; y->at = millis(); }
    }

    // $GPTXT,01,01,01,ANTENNA OK - the AT6558 modules (ATGM336H, L76K) say this every second.
    void parseTxt() {
        const char* a = strstr(_line, "ANTENNA ");
        if (!a) return;
        a += 8;
        _antenna = !strncmp(a, "OK", 2) ? 1 : !strncmp(a, "OPEN", 4) ? 2 : !strncmp(a, "SHORT", 5) ? 3 : 0;
    }

    bool parseRmc() {
        char* f = field(_line, 1);                         // hhmmss.ss, empty until it has the time
        _timeKnown = f && isdigit((unsigned char)f[0]);
        if (_timeKnown && strlen(f) >= 6) {
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
        if (date && isdigit((unsigned char)date[0]) && strlen(date) >= 6) {
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
    bool     _asleep = false;
    uint32_t _wokeAt = 0, _wakeFrom = 0;
    char     _line[100] = {0};
    uint8_t  _len = 0;
    bool     _started = false;
    GpsFix   _fix;
    struct Sys { char a = 0, b = 0; uint8_t heard = 0, best = 0, pendHeard = 0, pendBest = 0; uint32_t at = 0; };
    Sys      _sys[4];
    bool     _timeKnown = false;
    uint8_t  _antenna = 0;
#ifdef GPS_BAUD_ALT
    bool     _alt = false;
    uint32_t _probeFrom = 0;
#endif
};
