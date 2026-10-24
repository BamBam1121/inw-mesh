#pragma once
#include <Arduino.h>

namespace power {
  // Diagnostic time accounting only; never changes the sleep policy.
  enum class SleepReason : uint8_t {
    Input, GpsWork, WifiWork, RadioWork, UiTick, Draw, Effects, StorageWork, Scheduling,
    Sleeping, Off, CpuOff, Error, ScreenOn, Boot, Usb, Audio, Bluetooth, Wifi, Gps,
    Motion, Nfc, Background, Panel, Fade, Overlay, KeyboardEffect, PrefsSave, UiSave,
    Field, StoreQueue, Deadline, RadioNotReady, Mesh, RadioIrq, Button, Clock, MeshPoolInvalid, MeshTxQueue, MeshAllocated, RadioNotRx, Count
  };
  void sleepDiagnosticPhase(SleepReason reason, bool screenOff);
  void sleepDiagnosticContext(SleepReason background);
  void sleepDiagnosticMeshContext(SleepReason mesh);
  void resetSleepDiagnostics(bool screenOff);
  void printSleepDiagnostics();
  uint64_t sleepDiagnosticMicros(SleepReason reason);
  // Volatile evidence survives option toggles and cpu reset, until reboot.
  struct SleepFailure {
    uint32_t sequence, uptimeMs, budgetMs, clockMhz;
    int32_t code;
    const char* stage;
    int radioBefore, buttonBefore, radioAfter, buttonAfter;
    uint64_t elapsedUs;
  };
  uint32_t sleepFailureCount();
  uint32_t sleepRecoveryCount();
  uint32_t sleepRetryMs();
  bool lastSleepFailure(SleepFailure& out);
  bool lightSleep();
  bool setLightSleep(bool on);
  // All fields are evaluated on the main loop after synchronous background work.
  struct SleepInputs {
    bool screenOff, audio, usb, bluetooth, wifi, gps, meshBusy, radioReady;
    bool motion, background, nfc;
    bool imuIrq; // reserved compatibility field; motion never wakes the CPU
    uint32_t deadline; // absolute millis() of earliest application deadline
  };
  // Returns true if a light-sleep attempt consumed this loop's idle interval.
  bool idleLightSleep(const SleepInputs& in);
  const char* lightSleepStatus();
  uint32_t lightSleepCount();
  uint32_t lightSleepLastMs();
  uint32_t lightSleepMaxMs();
  uint64_t lightSleepMicros();
  uint32_t lightSleepWakeCause();
}
