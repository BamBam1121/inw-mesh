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
  void begin() { _base.begin(); }
  uint32_t getCurrentTime() override { return _base.getCurrentTime(); }
  void setCurrentTime(uint32_t t) override {
    _base.setCurrentTime(t);
    if (onSet) onSet(t);
  }
  // Set without calling back (used when the value came FROM the hardware RTC).
  void setQuiet(uint32_t t) { _base.setCurrentTime(t); }
};

// No environment sensors on this board. Location comes from our GPS driver,
// written into node_lat/node_lon by the app; telemetry reports it when asked
// and permitted.
class InwSensors : public SensorManager {
public:
  bool hasFix = false;
  bool querySensors(uint8_t perms, CayenneLPP& telemetry) override {
    if ((perms & TELEM_PERM_LOCATION) && (node_lat != 0 || node_lon != 0)) {
      telemetry.addGPS(TELEM_CHANNEL_SELF, (float)node_lat, (float)node_lon, (float)node_altitude);
    }
    return true;
  }
};

extern InwPagerBoard board;
extern RadioLibWrapper& radio_driver;   // the driver for whichever chip this board has (built by radio_init)
extern const char* radio_chip;          // "SX1262", "LR1121", or "none"
extern InwRTCClock rtc_clock;
extern InwSensors sensors;
extern SPIClass inw_spi;   // the one shared SPI bus: radio, SD card (and the panel)

bool radio_init();
mesh::LocalIdentity radio_new_identity();
