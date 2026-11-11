// Optimised charging and battery saver.
//
// Optimised charging learns when the pager usually comes off the charger and,
// on a long charge that should end around then, pauses at 80% and finishes to
// 100% about two hours before. Less time sitting full at 4.2 V is the wear it
// saves. Like a phone, it needs a few days of unplugs before it kicks in.
//
// Battery saver turns off GPS, Bluetooth and Wi-Fi, dims the screen and the
// keyboard light and sleeps sooner. Messages keep working. It comes on by
// itself at the low threshold and turns off once charged to 80%.

#pragma once
#include <Arduino.h>

namespace power {
  void tick(bool batteryUpdated); // from loop, after the scheduled battery reading

  // Battery saver
  bool saver();
  void setSaver(bool on);         // manual; turning it off keeps it off until the next low battery

  // Independent CPU saver: does not disable GPS, BLE or Wi-Fi. Opt-in, saved
  // in its own NVS key so the existing settings blob remains compatible.
  bool cpuSaving();
  bool setCpuSaving(bool on);     // false if the preference could not be saved
  void updateCpu(bool screenOff); // screen state only; false restores the boot clock
  uint32_t cpuClockMhz();

  // Optimised charging
  bool holding();                 // paused at 80% right now
  void chargeFullNow();           // release the hold for this charge
  int  predictedUnplugMin();      // local minute-of-day, or -1 if not learned yet
  uint8_t unplugSamples();
  const char* chargeStatus();     // one line for the Battery page
}
