// Ring of recent log lines, viewable on the device (Tools > log). Also mirrored
// to serial.

#pragma once
#include <Arduino.h>
#include <stdarg.h>

enum LogLevel : uint8_t { LOG_INFO = 0, LOG_WARN, LOG_ERROR };

class LogStore {
public:
    static constexpr uint8_t  CAP  = 48;
    static constexpr uint8_t  TEXT = 44;

    void add(LogLevel level, const char* fmt, ...) {
        Entry& e = _e[_head];
        e.level = level;
        e.ms    = millis();
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(e.text, sizeof(e.text), fmt, ap);
        va_end(ap);

        _head = (uint8_t)((_head + 1) % CAP);
        if (_count < CAP) _count++;

        Serial.printf("[%c] %s\n", "IWE"[level], e.text);
    }

    uint8_t count() const { return _count; }

    // 0 is the newest. Reading newest first is what you want after something broke.
    const char* line(uint8_t i, LogLevel* level = nullptr) const {
        if (i >= _count) return "";
        const uint8_t idx = (uint8_t)((_head + CAP - 1 - i) % CAP);
        if (level) *level = _e[idx].level;
        return _e[idx].text;
    }

    void clear() { _count = 0; _head = 0; }

private:
    struct Entry {
        char     text[TEXT] = {0};
        uint32_t ms = 0;
        LogLevel level = LOG_INFO;
    };
    Entry   _e[CAP];
    uint8_t _head = 0, _count = 0;
};
