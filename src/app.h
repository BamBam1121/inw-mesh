// Hardware singletons and app-level helpers shared by the screens (defined in main.cpp).

#pragma once
#include <Arduino.h>
#include "ui.h"
#include "settings.h"
#include "themes.h"

class Haptic; class Gps; class Battery; class IdleDimmer; class Keyboard;
class JinglePlayer; class Es8311; class LogStore; class Rtc; class Carousel;

extern LGFX display;
extern Theme theme;
extern Haptic haptic;
extern Gps gps;
extern Battery battery;
extern IdleDimmer dimmer;
extern Keyboard keyboard;
extern JinglePlayer jingle;
extern Es8311 codec;
extern LogStore logs;
extern Rtc rtc;

namespace app {
  uint32_t now();                 // epoch seconds, mesh clock
  bool     timeValid();           // false until RTC/GPS/phone/mesh has set it
  void     setTime(uint32_t epoch);
  const char* batteryText();      // "87%" / "CHG 87%"
  uint8_t  batteryPct();
  bool     charging();
  bool     pluggedIn();
  void     pluggedInFeedback();     // chime + tap when the charger goes in
  void     rebootToFlashMode();     // saves, then restarts into ROM USB download mode
  uint16_t batteryMv();
  bool     radioOk();
  uint16_t unread();
  void     applyDisplay();        // brightness, timeouts, keyboard light
  void     applySound();
  void     applyHaptics();
  void     applyTheme();          // colours, tick and vibration of ui_settings.themeId
  const ThemeSpec& themeSpec();
  void     testNotify();
  void     lock();                // show the lock face
  void     reboot();
  // Distance/bearing helpers for the map and contact detail.
  bool     myPosition(double& lat, double& lon);
  double   distanceKm(double lat1, double lon1, double lat2, double lon2);
  const char* fmtDistance(double km);
  // Open things from anywhere (defined in the view files).
  void openChats();
  void openContacts();
  void openMap(double lat = 0, double lon = 0, const char* focusName = nullptr);
  void openTools();
  void openSettings();
  void openThreadForContact(const uint8_t* pub);
  void openThreadForChannel(int idx);
  void openContactDetail(const uint8_t* pub);
}
