// The simulator's side of what main.cpp, node.cpp and the drivers provide on the
// pager: the globals and app:: functions the screens call, answered with a
// believable pager state (87% battery, a set clock, a mesh node with contacts).
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <SPIFFS.h>
#include <SD.h>
#include <time.h>
#include "sim.h"
#include "app.h"
#include "node.h"
#include "history.h"
#include "gps.h"
#include "battery.h"
#include "backlight.h"
#include "haptic.h"
#include "keyboard.h"
#include "rtc.h"
#include "audio_jingle.h"
#include "logstore.h"
#include "power.h"
#include "netwifi.h"
#include "fx.h"
#include "statusbar.h"

// ---- Arduino / ESP-IDF --------------------------------------------------------------
static uint32_t s_ms = 100000;
namespace sim { void setMillis(uint32_t ms) { s_ms = ms; } void advance(uint32_t ms) { s_ms += ms; } }
uint32_t millis() { return s_ms; }
uint32_t micros() { return s_ms * 1000u; }
void delay(uint32_t ms) { s_ms += ms; }
void delayMicroseconds(uint32_t) {}
void yield() {}
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t, uint8_t) {}
int digitalRead(uint8_t) { return HIGH; }
int analogRead(uint8_t) { return 0; }
void analogWrite(uint8_t, int) {}
uint32_t analogReadMilliVolts(uint8_t) { return 2000; }
void analogReadResolution(uint8_t) {}
static uint32_t s_rand = 12345;
long random(long n) { s_rand = s_rand * 1103515245u + 12345u; return n > 0 ? (long)((s_rand >> 8) % (uint32_t)n) : 0; }
long random(long a, long b) { return a + random(b - a); }
void randomSeed(unsigned long s) { s_rand = (uint32_t)s; }
uint32_t esp_random() { s_rand = s_rand * 1103515245u + 12345u; return s_rand; }
size_t strlcpy(char* d, const char* s, size_t n) {
  const size_t l = strlen(s);
  if (n) { const size_t k = l < n - 1 ? l : n - 1; memcpy(d, s, k); d[k] = 0; }
  return l;
}
size_t strlcat(char* d, const char* s, size_t n) {
  const size_t dl = strnlen(d, n);
  return dl == n ? n + strlen(s) : dl + strlcpy(d + dl, s, n - dl);
}
char* strcasestr(const char* h, const char* nd) {
  const size_t n = strlen(nd);
  for (; *h; h++) if (!strncasecmp(h, nd, n)) return (char*)h;
  return n ? nullptr : (char*)h;
}
EspClass ESP;
HardwareSerial Serial, Serial1;
SPIClass SPI;
TwoWire Wire;
SPIFFSFS SPIFFS;
SDFS SD;
size_t Print::print(const String& s) { return write(s.c_str()); }

// ---- hardware singletons, as main.cpp has them -------------------------------------------
LGFX display;
Theme theme;
IdleDimmer dimmer;
static Backlight s_backlight;
Keyboard keyboard;
Haptic haptic;
Gps gps;
Es8311 codec;
JinglePlayer jingle;
Battery battery;
Rtc rtc;
LogStore logs;

// The T-Deck's hardware check reads these (src/tdeck/hwcheck.cpp).
#include "touch_gt911.h"
#include "rotary.h"
TouchPanel touchPanel;
namespace tdeck_tb {
uint32_t simCounts[4] = {0, 0, 0, 0};
void counts(uint32_t out[4]) { for (int i = 0; i < 4; i++) out[i] = simCounts[i]; }
}
const char* radio_chip = "SX1262";
bool sdMounted() { return true; }
uint64_t sdFreeBytes() { return 29500ULL << 20; }

// ---- the mesh node ------------------------------------------------------------------------
static InwNode s_node;
InwNode* g_node = &s_node;
InwNode::InwNode() { memset(contacts, 0, sizeof(contacts)); memset(channels, 0, sizeof(channels)); memset(&self_id, 0x5A, sizeof(self_id)); }
bool bleEnabled() { return true; }
bool bleConnected() { return sim::phoneLinked; }
uint32_t blePin() { return 123456; }
void bleSetEnabled(bool) {}
SimSensors sensors;

// ---- app:: --------------------------------------------------------------------------------
uint32_t sim::epoch = 1790527260;   // Sep 27 2026, 9:41 am Pacific
bool sim::phoneLinked = false;
namespace app {
uint32_t now() { return sim::epoch + (millis() - 100000) / 1000; }
bool timeValid() { return true; }
void setTime(uint32_t e) { sim::epoch = e; }
const char* batteryText() { static char b[8]; snprintf(b, sizeof(b), "%u%%", battery.percent()); return b; }
uint8_t batteryPct() { return battery.percent(); }
bool charging() { return battery.charging(); }
bool pluggedIn() { return battery.pluggedIn(); }
void pluggedInFeedback() {}
void rebootToFlashMode() {}
uint16_t batteryMv() { return battery.millivolts(); }
bool radioOk() { return true; }
const char* radioFault() { return "radio not responding"; }
uint16_t unread() {
  ConvKey keys[64];
  const uint16_t n = history.conversations(keys, 64);
  uint16_t total = 0;
  for (uint16_t i = 0; i < n; i++) total += history.unread(keys[i]);
  return total;
}
void applyDisplay() {}
void applySound() {}
void applyHaptics() {}
const ThemeSpec& themeSpec() { return themes::spec(ui_settings.themeId); }
void applyTheme() {
  const ThemeSpec& th = themeSpec();
  theme.apply(display, th.palette, th.style);
  theme.own = themes::isCustom(ui_settings.themeId) && themes::valid(ui_settings.themeId);
  nav.invalidate();
}
void testNotify() {}
void lock() {}
void reboot() {}
void rebootDiscard() {}
bool animationsOk() { return false; }
void powerOffPrompt() {}
bool powerOff(const char*) { return false; }
bool myPosition(double& lat, double& lon) { lat = 47.6588; lon = -117.4260; return true; }
double distanceKm(double lat1, double lon1, double lat2, double lon2) {
  const double R = 6371.0, dl = (lat2 - lat1) * DEG_TO_RAD, dn = (lon2 - lon1) * DEG_TO_RAD;
  const double a = sin(dl / 2) * sin(dl / 2) + cos(lat1 * DEG_TO_RAD) * cos(lat2 * DEG_TO_RAD) * sin(dn / 2) * sin(dn / 2);
  return 2 * R * atan2(sqrt(a), sqrt(1 - a));
}
const char* fmtDistance(double km) {
  static char b[16];
  if (ui_settings.miles) snprintf(b, sizeof(b), "%.1f mi", km * 0.621371);
  else snprintf(b, sizeof(b), "%.1f km", km);
  return b;
}
#ifdef SIM_PAGER   // the T-Deck simulator compiles the real screens (build.py SCREENS)
void openTools() {}
void openMap(double, double, const char*) {}
void openSettings() {}
void openContacts() {}
void openContactDetail(const uint8_t*) {}
#endif
}  // namespace app
#ifdef SIM_PAGER
void openChannelRegionScope(const uint8_t*) {}   // settings_ui.cpp, which the pager sim doesn't build
#endif

void markUiDirty() {}
void markPrefsDirty() {}

// ---- the rest of the firmware's modules, as far as the screens ask ------------------------------
namespace power {
bool saver() { return false; }
bool holding() { return false; }
}
namespace wifi {
bool enabled() { return true; }
bool connected() { return sim::wifiOn; }
void setEnabled(bool) {}
const char* statusText() { return "Trailhead 192.168.1.40 -61 dBm"; }
const char* shortStatus() { return sim::wifiOn ? "Trailhead" : "off"; }
uint8_t savedCount() { return 1; }
const char* savedSsid(uint8_t) { return "Trailhead"; }
const char* savedState(uint8_t) { return "connected"; }
void save(const char*, const char*) {}
void forget(uint8_t) {}
void startScan() {}
bool scanDone() { return true; }
static const char* SSIDS[] = {"Trailhead", "Mulvey-5G-2.4", "Spokane Library", "CampusNet"};
int scanCount() { return 4; }
const char* scanSsid(int i) { return SSIDS[i & 3]; }
bool scanOpen(int i) { return i == 2; }
bool scanEnterprise(int i) { return i == 3; }
int scanRssi(int i) { return -48 - 9 * i; }
void requestTile(uint8_t, int32_t, int32_t) {}
bool pollTile(TileDone&) { return false; }
uint16_t tilesFetched() { return 0; }
uint16_t tilesFailed() { return 0; }
int lastHttpCode() { return 0; }
}
// Settings, map and contact screens (settings_ui.cpp, mapview.cpp, contacts.cpp).
namespace power { void setSaver(bool) {} }
#include "ota.h"
namespace ota {
bool supported() { return true; }
Info check() { Info i; strlcpy(i.error, "simulator", sizeof(i.error)); return i; }
const char* install(const Info&) { return "simulator"; }
static bool s_auto = true;
bool autoInstall() { return s_auto; }
void setAutoInstall(bool on) { s_auto = on; }
}
#include "bugreport.h"
namespace report {
static bool s_on = true;
bool enabled() { return s_on; }
void setEnabled(bool on) { s_on = on; }
bool sendLog(const char*) { return true; }
}
#include "dataio.h"
#include "fieldtools.h"
bool sdMount() { return true; }
const char* sdBackupNow(bool) { return "simulator"; }
const char* exportJson() { return "simulator"; }
const char* importJsonNow() { return "simulator"; }
const char* recoverMissingContacts() { return "simulator"; }
namespace field { const TrailPt* trail(size_t& n) { n = 0; return nullptr; } }
void gpsPower(bool) {}
namespace field { void openMenu() {} }
uint32_t g_shotAt = 0;
bool sim::wifiOn = true;
// The background flash writer (tools/patch_meshcore.py): nothing to write in a simulation.
bool inwQueueAppend(const char*, const uint8_t*, size_t) { return true; }
bool inwQueueReplace(const char*, const uint8_t*, size_t) { return true; }
void inwDropPath(const char*) {}
