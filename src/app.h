// Hardware singletons and app-level helpers shared by the screens (defined in main.cpp).

#pragma once
#include <Arduino.h>
#include "ui.h"
#include "settings.h"
#include "themestore.h"   // themes.h, and the owner's own

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
  const char* radioFault();       // why the radio is down, when radioOk() is false
  uint16_t unread();
  void     applyDisplay();        // brightness, timeouts, keyboard light
  void     applySound();
  void     applyHaptics();
  void     keysPump();               // read the keyboard now, into the queue the loop takes keys from (fx calls it each frame)
  bool     screenChangesAnimate();   // the themes' animations between screens (Settings > Display)
  void     setScreenChangesAnimate(bool on);
  // Signal bars (Settings > Display). Sizes: 0 off, 1 small, 2 large.
  uint8_t  signalSize();             // in the status bar
  void     setSignalSize(uint8_t n);
  uint8_t  lockSignalSize();         // on the lock face
  void     setLockSignalSize(uint8_t n);
  bool     lockSignalLeft();         // lock face: top left instead of top right
  void     setLockSignalLeft(bool left);
  inline bool signalBars() { return signalSize() || lockSignalSize(); }   // shown anywhere
  int      signalLevel();            // 0 nothing heard lately, 1-4 by the last packet's SNR
  uint8_t  signalMask();             // which of the four bars are lit right now (bit 0 = shortest): the level, or its animation
  bool     signalAnimating();        // a check is out, or the bars are filling
  uint8_t  signalCheckMins();        // ask nearby repeaters this often when nothing is heard; 0 = never
  void     setSignalCheckMins(uint8_t mins);
  void     applyTheme();          // colours, tick and vibration of ui_settings.themeId
  const ThemeSpec& themeSpec();
  void     testNotify();
  void     lock();                // show the lock face
  void     reboot();
  void     rebootDiscard();          // restart WITHOUT saving contacts (after deleting them on purpose)
  bool     animationsOk();           // screen lit and booted: screen changes animate
  void     powerOffPrompt();         // "Power off?" screen (side button held, or Settings)
  bool     powerOff(const char* why); // saves, then cuts the battery; false if USB is in
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
