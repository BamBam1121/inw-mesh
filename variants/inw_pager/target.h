#pragma once
// MeshCore's view of the board: the radio, the clock, the board hooks and the
// sensor manager that MyMesh (examples/companion_radio) expects as globals.

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <SPI.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include "CustomLR1121Wrapper.h"
#include <helpers/ESP32Board.h>
#include <helpers/SensorManager.h>
#include <InwPagerBoard.h>

// System time is the mesh clock. The PCF85063 on the board is written through
// onSet whenever something authoritative (the phone app, GPS) sets it, and read
// back at boot by our app, so the time survives a power cycle.
class InwRTCClock : public mesh::RTCClock {
  ESP32RTCClock _base;
public:
  void (*onSet)(uint32_t epoch) = nullptr;
  // MeshCore's ESP32RTCClock::begin() sets the clock to 15 May 2024 after a power-on
  // reset - and the reset button is one. It runs when the radio starts, after the time
  // was already read back from the clock chip (main.cpp), so every reset lost the time
  // until Wi-Fi or a GPS fix set it again (a help desk report, pager 1.2.8, 2026-10-12).
  // A time already set is kept.
  void begin() {
    const uint32_t keep = _base.getCurrentTime();
    _base.begin();
    if (keep > 1735689600UL) _base.setCurrentTime(keep);    // after 2025-01-01: a real time
  }
  uint32_t getCurrentTime() override { return _base.getCurrentTime(); }
  void setCurrentTime(uint32_t t) override {
    _base.setCurrentTime(t);
    if (onSet) onSet(t);
  }
  // Set without calling back (used when the value came FROM the hardware RTC).
  void setQuiet(uint32_t t) { _base.setCurrentTime(t); }
};

namespace ext { void telemetry(CayenneLPP& lpp); }   // src/extport.cpp
void inwLocShare(double& lat, double& lon);   // src/main.cpp: rounds a position to the chosen accuracy

// No environment sensors on the board itself, only what is plugged into the top
// header (src/extport.cpp). Location comes from our GPS driver, written into
// node_lat/node_lon by the app; telemetry reports it when asked and permitted.
class InwSensors : public SensorManager {
public:
  bool hasFix = false;
  bool querySensors(uint8_t perms, CayenneLPP& telemetry) override {
    if ((perms & TELEM_PERM_LOCATION) && (node_lat != 0 || node_lon != 0)) {
      double lat = node_lat, lon = node_lon;
      inwLocShare(lat, lon);                    // main.cpp: Settings > position accuracy
      telemetry.addGPS(TELEM_CHANNEL_SELF, (float)lat, (float)lon, (float)node_altitude);
    }
    if (perms & TELEM_PERM_ENVIRONMENT) ext::telemetry(telemetry);
    return true;
  }
};

extern InwPagerBoard board;
extern RadioLibWrapper& radio_driver;   // the driver for whichever chip this board has (built by radio_init)
extern const char* radio_chip;          // "SX1262", "LR1121", or "none"
extern char radio_why[40];              // when it is "none": what the two drivers said, for reports
extern InwRTCClock rtc_clock;
extern InwSensors sensors;
extern SPIClass inw_spi;   // the one shared SPI bus: radio, SD card (and the panel)

bool radio_init();
mesh::LocalIdentity radio_new_identity();
