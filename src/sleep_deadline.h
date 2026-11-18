#pragma once
#include <stdint.h>

// millis() deadlines, wrap-safe for intervals below INT32_MAX. A radio/button
// interrupt may end any sleep sooner; the timer only bounds scheduled work.
class SleepDeadline {
public:
  explicit SleepDeadline(uint32_t now, uint32_t cap = 30000) : _now(now), _ms(cap) {}
  void at(uint32_t deadline) {
    const int32_t left = (int32_t)(deadline - _now);
    limit(left > 0 ? (uint32_t)left : 0);
  }
  void after(uint32_t last, uint32_t interval) { at(last + interval); }
  void limit(uint32_t ms) { if (ms < _ms) _ms = ms; }
  uint32_t ms() const { return _ms; }
private:
  uint32_t _now, _ms;
};
