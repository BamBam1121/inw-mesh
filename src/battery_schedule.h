#pragma once
#include <stdint.h>

inline uint32_t batteryIntervalMs(bool lightSaving, bool screenOff) {
  return lightSaving && screenOff ? 30000 : 5000;
}

// Shutdown confirmation is elapsed time, not a count of samples: changing the
// poll interval must not stretch a minute of critical voltage to six minutes.
class LowBatteryWindow {
public:
  bool update(bool flat, uint32_t now) {
    if (!flat) { _tracking = false; return false; }
    if (!_tracking) { _since = now; _tracking = true; return false; }
    return (uint32_t)(now - _since) >= 60000;
  }
private:
  bool _tracking = false;
  uint32_t _since = 0;
};
