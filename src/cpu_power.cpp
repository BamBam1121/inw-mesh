// ECO's CPU half. On the pager (from PR #13) it drops the clock to 80 MHz while the
// screen is off. The T-Deck and the M9 already do that every time the screen goes dark
// (main.cpp, BOARD_SLOW_CPU_WHEN_DARK), so there this only answers ECO's questions and
// leaves the clock to the loop: two hands on it would fight over 80 and 240.
#include "power.h"
#include "board_pins.h"
#include <Preferences.h>
#include <esp32-hal-cpu.h>

namespace power {
namespace {
  int8_t s_enabled = -1;
#if !BOARD_SLOW_CPU_WHEN_DARK
  uint32_t s_normalMhz = 0;
  uint32_t s_failedMhz = 0;
  constexpr uint32_t IDLE_MHZ = 80;
#endif
}

bool cpuSaving() {
  if (s_enabled < 0) {
    s_enabled = 0;                // experimental: existing installs stay unchanged
    Preferences p;
    if (p.begin("inw-cpu", true)) {
      s_enabled = p.getBool("saving", false) ? 1 : 0;
      p.end();
    }
  }
  return s_enabled == 1;
}

bool setCpuSaving(bool on) {
  Preferences p;
  if (!p.begin("inw-cpu", false)) return false;
  const bool saved = p.putBool("saving", on) == sizeof(bool);
  p.end();
  if (!saved) return false;
  s_enabled = on ? 1 : 0;
#if !BOARD_SLOW_CPU_WHEN_DARK
  s_failedMhz = 0;
  if (!on) updateCpu(false);
#endif
  return true;
}

uint32_t cpuClockMhz() { return getCpuFrequencyMhz(); }

void updateCpu(bool screenOff) {
#if BOARD_SLOW_CPU_WHEN_DARK
  (void)screenOff;                // the loop already runs at 80 MHz while dark
#else
  if (!s_normalMhz) s_normalMhz = cpuClockMhz();
  const uint32_t want = screenOff && cpuSaving() && s_normalMhz > IDLE_MHZ ? IDLE_MHZ : s_normalMhz;
  if (cpuClockMhz() == want || s_failedMhz == want) return;
  if (!setCpuFrequencyMhz(want)) {
    s_failedMhz = want;
    Serial.printf("[power] CPU clock change to %lu MHz failed\r\n", (unsigned long)want);
    return;
  }
  s_failedMhz = 0;
  Serial.printf("[power] CPU %lu MHz\r\n", (unsigned long)want);
#endif
}
} // namespace power
