#pragma once
// MeshCore's view of the T-Deck: the radio, the clock, the board hooks and the
// sensor manager that MyMesh (examples/companion_radio) expects as globals.
// The same names as variants/inw_pager, so the shared app code links to either.

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <SPI.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/ESP32Board.h>
#include <helpers/SensorManager.h>
#include <InwTDeckBoard.h>

// System time is the mesh clock. The T-Deck has no clock chip to write it to,
// so onSet stays unset unless the app gives it something to do.
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
  void setQuiet(uint32_t t) { _base.setCurrentTime(t); }
};

namespace ext { void telemetry(CayenneLPP& lpp); }   // src/extport.cpp (nothing on a T-Deck)

// Location from the Plus's GPS, written into node_lat/node_lon by the app.
class InwSensors : public SensorManager {
public:
  bool hasFix = false;
  bool querySensors(uint8_t perms, CayenneLPP& telemetry) override {
    if ((perms & TELEM_PERM_LOCATION) && (node_lat != 0 || node_lon != 0)) {
      telemetry.addGPS(TELEM_CHANNEL_SELF, (float)node_lat, (float)node_lon, (float)node_altitude);
    }
    if (perms & TELEM_PERM_ENVIRONMENT) ext::telemetry(telemetry);
    return true;
  }
};

extern InwTDeckBoard board;
extern RadioLibWrapper& radio_driver;
extern const char* radio_chip;          // "SX1262" or "none"
extern InwRTCClock rtc_clock;
extern InwSensors sensors;
extern SPIClass inw_spi;   // the one shared SPI bus: radio, SD card (and the panel)

bool radio_init();
mesh::LocalIdentity radio_new_identity();
