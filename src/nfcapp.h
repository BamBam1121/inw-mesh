#pragma once

namespace app {
#if defined(INW_NFC) && INW_NFC
  void nfcIdlePower();
  bool nfcPowerSaving();
  bool setNfcPowerSaving(bool on);
  void nfcPowerDebug();
  bool nfcRailRequested();
  bool nfcActive(); // read, write or tag-emulation session not yet closed
#else
  inline void nfcIdlePower() {}
  inline bool nfcPowerSaving() { return false; }
  inline bool setNfcPowerSaving(bool) { return false; }
  inline void nfcPowerDebug() {}
  inline bool nfcRailRequested() { return true; } // no-NFC build retains bringup rail policy
  inline bool nfcActive() { return false; }
#endif
}
