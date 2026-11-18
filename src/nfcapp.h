#pragma once

// The chip's own power is already switched off whenever no NFC screen is open
// (nfcPower in main.cpp), so ECO mode only needs to know when one is.
namespace app {
#if defined(INW_NFC) && INW_NFC
  bool nfcActive(); // read, write or tag-emulation session not yet closed
#else
  inline bool nfcActive() { return false; }
#endif
}
