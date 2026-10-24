#include "eco_power.h"
#include "nfcapp.h"

namespace power {
bool setEco(bool on) {
  const bool cpu = cpuSaving(), sleep = lightSleep();
#if defined(INW_NFC) && INW_NFC
  const bool nfc = app::nfcPowerSaving();
  if (nfc != on && !app::setNfcPowerSaving(on)) return false;
#endif
  // Always call the sleep setter: explicitly toggling ECO also clears a
  // previous sleep-error latch, as the old CPU switch did.
  if (!setLightSleep(on)) {
#if defined(INW_NFC) && INW_NFC
    if (nfc != on) app::setNfcPowerSaving(nfc);
#endif
    return false;
  }
  if (cpu != on && !setCpuSaving(on)) {
    setLightSleep(sleep);
#if defined(INW_NFC) && INW_NFC
    if (nfc != on) app::setNfcPowerSaving(nfc);
#endif
    return false;
  }
  return true;
}

bool initEco() {
  const bool on = eco();
  // Normal boots never rewrite NVS. Only reconcile older mixed settings.
  if (lightSleep() != on && !setLightSleep(on)) return false;
  if (cpuSaving() != on && !setCpuSaving(on)) return false;
#if defined(INW_NFC) && INW_NFC
  if (app::nfcPowerSaving() != on && !app::setNfcPowerSaving(on)) return false;
#endif
  return true;
}
}
