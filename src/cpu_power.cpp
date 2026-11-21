#include "power.h"
#include <Preferences.h>
#include <esp32-hal-cpu.h>

namespace power {
namespace {
  int8_t s_enabled = -1;
  uint32_t s_normalMhz = 0;
  uint32_t s_failedMhz = 0;
  constexpr uint32_t IDLE_MHZ = 80;
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
  s_failedMhz = 0;
  if (!on) updateCpu(false);
  return true;
}

uint32_t cpuClockMhz() { return getCpuFrequencyMhz(); }

void updateCpu(bool screenOff) {
  // Capture the configured boot clock, rather than assuming every build uses
  // 240 MHz. Arduino's HAL also updates the RTOS/APB timing when it switches.
  if (!s_normalMhz) s_normalMhz = cpuClockMhz();
  const uint32_t want = screenOff && cpuSaving() && s_normalMhz > IDLE_MHZ
                        ? IDLE_MHZ : s_normalMhz;
  if (cpuClockMhz() == want || s_failedMhz == want) return;
  if (!setCpuFrequencyMhz(want)) {
    s_failedMhz = want;           // do not spam/retry an unsupported clock every loop
    Serial.printf("[power] CPU clock change to %lu MHz failed\r\n", (unsigned long)want);
    return;
  }
  s_failedMhz = 0;
  Serial.printf("[power] CPU %lu MHz\r\n", (unsigned long)want);
}
} // namespace power
