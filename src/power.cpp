#include "power.h"
#include <math.h>
#include "app.h"
#include "battery.h"
#include "netwifi.h"
#include "node.h"
#include "settings.h"
#include "logstore.h"

extern Battery battery;
extern LogStore logs;
extern void gpsPower(bool on);
extern void markUiDirty();

namespace power {

static bool s_saver = false, s_dismissed = false, s_wifiWas = false, s_bleWas = false;

static bool s_plugged = false;
static uint32_t s_plugAt = 0;          // millis
static bool s_planned = false;         // this charge is long enough to hold
static uint32_t s_releaseAt = 0;       // epoch: stop holding and finish to 100%
static bool s_released = false;

static constexpr uint8_t HOLD_PCT = 80, REHOLD_BELOW = 75;
static constexpr uint32_t FINISH_BEFORE_S = 2UL * 3600UL;    // 80 -> 100 with a slow taper

static int localMinute() {
  const int64_t t = (int64_t)app::now() + ui_settings.tzMinutes * 60;
  return (int)(((t % 86400) + 86400) % 86400 / 60);
}

// ---- learning the unplug time ----------------------------------------------------------
// Minute-of-day is circular (23:50 and 00:10 are close), so average on a circle.
// Slots hold minute + 1 so 0 can mean empty.
int predictedUnplugMin() {
  double sx = 0, sy = 0;
  int n = 0;
  for (uint8_t i = 0; i < UiSettings::UNPLUG_N; i++) {
    const uint16_t v = ui_settings.unplugMin[i];
    if (!v) continue;
    const double a = (v - 1) * 2.0 * M_PI / 1440.0;
    sx += cos(a); sy += sin(a); n++;
  }
  if (n < 4) return -1;
  const double r = sqrt(sx * sx + sy * sy) / n;      // 1 = always the same time
  if (r < 0.85) return -1;                          // no routine (spread over ~2 h)
  double a = atan2(sy, sx);
  if (a < 0) a += 2.0 * M_PI;
  return (int)lround(a * 1440.0 / (2.0 * M_PI)) % 1440;
}

uint8_t unplugSamples() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < UiSettings::UNPLUG_N; i++) if (ui_settings.unplugMin[i]) n++;
  return n;
}

static void recordUnplug() {
  if (!app::timeValid()) return;
  ui_settings.unplugMin[ui_settings.unplugPos] = localMinute() + 1;
  ui_settings.unplugPos = (ui_settings.unplugPos + 1) % UiSettings::UNPLUG_N;
  markUiDirty();
}

// ---- optimised charging -----------------------------------------------------------------
static void planCharge() {
  s_planned = false;
  if (!ui_settings.smartCharge || !app::timeValid()) return;
  const int pred = predictedUnplugMin();
  if (pred < 0) return;
  const int until = (pred - localMinute() + 1440) % 1440;
  // Only a long charge ahead is worth holding: overnight, not a top-up at lunch.
  if (until < 3 * 60 || until > 14 * 60) return;
  s_planned = true;
  s_releaseAt = app::now() + until * 60UL - FINISH_BEFORE_S;
  s_released = false;
  logs.add(LOG_INFO, "optimised charging: hold at %u%%, full by %02d:%02d", HOLD_PCT, pred / 60, pred % 60);
}

static void chargeTick() {
  const bool plugged = battery.pluggedIn();
  if (plugged && !s_plugged) { s_plugged = true; s_plugAt = millis(); planCharge(); }
  if (!plugged && s_plugged) {
    s_plugged = false;
    battery.holdCharge(false);
    if (millis() - s_plugAt > 60UL * 60UL * 1000UL) recordUnplug();   // real charges only
    s_planned = false;
  }
  if (!plugged) return;

  const bool want = ui_settings.smartCharge && s_planned && !s_released;
  if (!want) { battery.holdCharge(false); return; }
  if (app::now() >= s_releaseAt) {
    s_released = true;
    battery.holdCharge(false);
    logs.add(LOG_INFO, "optimised charging: finishing to 100%%");
    return;
  }
  const uint8_t pct = battery.percent();
  if (!battery.chargeHeld() && pct >= HOLD_PCT) battery.holdCharge(true);
  else if (battery.chargeHeld() && pct < REHOLD_BELOW) battery.holdCharge(false);
}

bool holding() { return battery.chargeHeld(); }
void chargeFullNow() { s_released = true; battery.holdCharge(false); }

const char* chargeStatus() {
  static char b[64];
  const int pred = predictedUnplugMin();
  if (!ui_settings.smartCharge) return "off";
  if (holding() && pred >= 0) { snprintf(b, sizeof(b), "held at %u%%, full by %02d:%02d", HOLD_PCT, pred / 60, pred % 60); return b; }
  if (s_plugged && s_planned && !s_released) return "on, will hold at 80%";
  if (pred < 0) { snprintf(b, sizeof(b), "learning your routine (%u of 4+ unplugs)", unplugSamples()); return b; }
  snprintf(b, sizeof(b), "ready, you usually unplug ~%02d:%02d", pred / 60, pred % 60);
  return b;
}

// ---- battery saver ------------------------------------------------------------------------
static void enterSaver(bool automatic) {
  if (s_saver) return;
  s_saver = true;
  s_wifiWas = wifi::enabled();
  s_bleWas = bleEnabled();
  if (s_wifiWas) wifi::setEnabled(false);
  if (s_bleWas) bleSetEnabled(false);
  if (ui_settings.gpsOn) gpsPower(false);
  app::applyDisplay();
  nav.statusChanged();
  logs.add(LOG_INFO, "battery saver on at %u%%%s", battery.percent(), automatic ? "" : " (manual)");
  nav.banner("Battery saver on", "gps, bluetooth and wi-fi are off. messages still work.", 6000);
}

static void exitSaver(const char* why) {
  if (!s_saver) return;
  s_saver = false;
  if (s_wifiWas) wifi::setEnabled(true);
  if (s_bleWas) bleSetEnabled(true);
  if (ui_settings.gpsOn) gpsPower(true);
  app::applyDisplay();
  nav.statusChanged();
  logs.add(LOG_INFO, "battery saver off: %s", why);
  nav.toast("battery saver off", 3000);
}

bool saver() { return s_saver; }

void setSaver(bool on) {
  if (on) { s_dismissed = false; enterSaver(false); }
  else { exitSaver("turned off"); s_dismissed = true; }
}

static void saverTick() {
  const uint8_t pct = battery.percent();
  const bool plugged = battery.pluggedIn();
  if (s_dismissed && (plugged || pct > ui_settings.saverPct + 5)) s_dismissed = false;
  if (!s_saver && ui_settings.autoSaver && !s_dismissed && !plugged && pct && pct <= ui_settings.saverPct) enterSaver(true);
  if (s_saver && plugged && pct >= 80) exitSaver("charged to 80%");
}

void tick() {
  static uint32_t last = 0;
  if (!battery.present() || !battery.hasReading() || millis() - last < 5000 || millis() < 15000) return;
  last = millis();
  chargeTick();
  saverTick();
}

}  // namespace power
