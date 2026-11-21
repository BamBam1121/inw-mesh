#include "eco_power.h"

namespace power {
bool setEco(bool on) {
  const bool cpu = cpuSaving(), sleep = lightSleep();
  // Always call the sleep setter: explicitly toggling ECO also clears a
  // previous sleep-error latch, as the old CPU switch did.
  if (!setLightSleep(on)) return false;
  if (cpu != on && !setCpuSaving(on)) { setLightSleep(sleep); return false; }
  return true;
}

bool initEco() {
  const bool on = eco();
  // Normal boots never rewrite NVS. Only reconcile older mixed settings.
  if (lightSleep() != on && !setLightSleep(on)) return false;
  if (cpuSaving() != on && !setCpuSaving(on)) return false;
  return true;
}
}
