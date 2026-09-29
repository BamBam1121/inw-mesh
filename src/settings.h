// UI-side settings in NVS. Anything the mesh needs (name, radio, position,
// auto-add, telemetry, repeat, BLE pin) lives in MeshCore's NodePrefs instead,
// so the phone app and the pager edit the same values.

#pragma once
#include <Arduino.h>

struct UiSettings {
  static constexpr uint8_t VERSION = 3;
  static constexpr uint8_t QUICK_MAX = 8;

  // clock
  int16_t  tzMinutes   = 0;         // fixed offset, used when tzZone is 0 (UTC until setup picks a zone)
  bool     clock24     = false;
  bool     gpsSetsClock = true;
  // display
  uint8_t  brightness  = 12;        // 1..16
  uint16_t dimSecs     = 20;
  uint16_t sleepSecs   = 60;
  uint8_t  kbBacklight = 180;
  bool     lockOnSleep = true;      // wake to the lock face, not straight into a menu
  // haptics + sound
  bool     vibrate     = true;
  uint8_t  vibeMode    = 5;         // DRV2605 config index; 5 == on-screen "6"
  bool     keyHaptics  = true;
  bool     scrollTick  = true;
  bool     sound       = true;
  uint8_t  volume      = 60;
  bool     bootJingle  = true;
  // notifications
  bool     notifyDM      = true;
  bool     notifyChannel = true;
  bool     notifyRoom    = true;
  bool     notifyNewContact = false;
  bool     channelMentionsOnly = false;
  bool     dnd           = false;
  bool     wakeOnMessage = true;
  bool     kbFlash       = true;      // light the keyboard on a new message
  bool     dndSchedule   = false;     // quiet hours
  uint8_t  dndStart      = 22, dndEnd = 7;
  // messaging
  bool     autoRetry     = true;
  bool     autoResetPath = true;
  bool     showHops      = true;
  bool     showSnr       = false;
  bool     compactChat   = false;     // IRC-style lines instead of bubbles
  bool     ignoreOneChar = false;     // drop 1-character channel noise
  bool     miles         = true;      // distances in miles
  // radios
  bool     ble           = true;
  bool     gpsOn         = true;
  bool     gpsLivePosition = true;  // keep the advert position following the GPS
  uint8_t  autoAdvertHours = 0;     // flood advert every N hours, 0 = off
  // map
  uint8_t  mapZoom       = 12;
  float    mapLat        = 47.6588f; // Spokane until the GPS says otherwise
  float    mapLon        = -117.4260f;
  bool     mapLabels     = true;
  // housekeeping
  uint8_t  importDone    = 0;       // bit0 files, bit1 prefs from JSON
  uint32_t lastSdBackup  = 0;
  char     quick[QUICK_MAX][32] = {
    "OK", "On my way", "Copy that", "Yes", "No", "Call me when you can", "Where are you?", "73"
  };
  // ---- appended fields: add new settings BELOW here only (load() reads an
  // older, shorter blob as a prefix, so existing settings survive upgrades) ----
  bool     wifiOn        = false;
  bool     tileFetch     = true;      // fetch missing map tiles while on Wi-Fi
  uint8_t  tileSource    = 0;         // 0 OSM (https), 1 wadamesh proxy (jpg), 2 custom
  char     tileUrl[96]   = "";        // custom: base URL, {z}/{x}/{y} appended
  bool     ntpSync       = true;
  uint8_t  themeId       = 0;         // index into THEMES
  // battery
  bool     smartCharge   = true;      // hold at 80% until shortly before the usual unplug time
  bool     autoSaver     = true;      // battery saver switches itself on when low
  uint8_t  saverPct      = 20;
  static constexpr uint8_t UNPLUG_N = 14;
  uint16_t unplugMin[UNPLUG_N] = {};  // local minute-of-day of recent unplugs (0 = empty slot)
  uint8_t  unplugPos     = 0;
  bool     autoUpdateCheck = true;    // look for a new release once per boot on Wi-Fi
  bool     betaUpdates   = false;     // follow every build, not only tagged releases
  bool     wheelUnlock   = true;      // lock screen: only a wheel press unlocks (pocket-safe)
  // where it is (regional.h)
  uint8_t  tzZone        = 0;         // 0: the fixed tzMinutes offset; else 1 + index into regional::ZONES
  uint8_t  setupDone     = 0;         // the first-start setup (region, time zone, units) has been through
  // Shown unless turned off, and stored that way round on purpose: fields added here
  // land in what older builds saved as struct padding, which reads back as 0.
  bool     hideHashBytes = false;     // chat: leave the hash size ("2B") off the hop count
  uint8_t  io9Mode       = 0;         // top header IO9: 0 untouched, 1 flash on messages, 2 on while unread
  // Touchscreen boards, set by hand if a unit comes out different: bit 0 screen
  // upside down, 1 touch mirrored left-right, 2 touch mirrored up-down, 3 trackball
  // reversed, 4 colours inverted.
  uint8_t  orient        = 0;
  // Touchscreen boards: two taps on the dark screen wake it. Off by default, as a
  // pocket taps too; the trackball always wakes it. (In what 1.2.2-beta1 saved as
  // padding, so it reads back off.)
  bool     tapWake       = false;
  bool     squatchQuiet  = false;     // lock screen: the sasquatch's speech bubble off (it talks by default)

  void load();
  void save();
  void saveMirror();                        // copy beside the mesh stores
  bool cameFromNvs();                       // false when NVS came up empty
  const char* restoreIfWiped(bool sdReady); // where they came back from, or nullptr
};

extern UiSettings ui_settings;
