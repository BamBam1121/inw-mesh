// Boot, the main loop, and turning mesh events into alerts.

#include <Arduino.h>
#include <Wire.h>
#include <SPIFFS.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <Preferences.h>
#include <soc/rtc_cntl_reg.h>
#include "power.h"
#include "notify.h"
#include "statusbar.h"
#include "ota.h"
#include <SD.h>
#include <time.h>
#include "board_pins.h"
#include "display_config.h"
#include "io_expander.h"
#include "bringup.h"
#include "backlight.h"
#include "rotary.h"
#include "keyboard.h"
#include "gps.h"
#include "battery.h"
#include "rtc.h"
#include "haptic.h"
#include "es8311_codec.h"
#include "audio_jingle.h"
#include "logstore.h"
#include "theme.h"
#include "app.h"
#include "node.h"
#include "history.h"
#include "dataio.h"
#include "fieldtools.h"
#include "netwifi.h"

// Mesh callbacks (decrypt, verify, then our history write) run on the loop task;
// give it room rather than finding the edge of the default 8 KB in the field.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

// ---- hardware ---------------------------------------------------------------------
LGFX          display;
Theme         theme;
static IoExpander expander;
static Backlight  backlight;
IdleDimmer    dimmer;
static Rotary rotary;
Keyboard      keyboard;
Haptic        haptic;
Gps           gps;
Es8311        codec;
JinglePlayer  jingle;
Battery       battery;
Rtc           rtc;
LogStore      logs;

extern ConvKey g_openConv;
View* makeHomeView();
View* makeLockView();

uint32_t g_shotAt = 0;
extern char g_screenTitle[32];   // ui.cpp: the last header drawn
static uint32_t s_prefsDirtyAt = 0, s_uiDirtyAt = 0;
static uint32_t s_kbFlashUntil = 0;
static bool s_radioOk = false;
static char s_radioFault[64] = "radio not responding";

void markPrefsDirty() { s_prefsDirtyAt = millis() | 1; }
void markUiDirty()    { s_uiDirtyAt = millis() | 1; }

// ---- clock ----------------------------------------------------------------------------
static uint32_t epochFrom(uint16_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi, uint8_t s) {
  struct tm tm = {};
  tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d;
  tm.tm_hour = h; tm.tm_min = mi; tm.tm_sec = s;
  return (uint32_t)mktime(&tm);        // no TZ set on this device: this is UTC
}

static void writeHardwareRtc(uint32_t epoch) {
  const time_t t = epoch;
  struct tm tm;
  gmtime_r(&t, &tm);
  RtcTime r;
  r.year = tm.tm_year + 1900; r.month = tm.tm_mon + 1; r.day = tm.tm_mday;
  r.hour = tm.tm_hour; r.minute = tm.tm_min; r.second = tm.tm_sec;
  rtc.set(r);
}

uint32_t app::now() { return rtc_clock.getCurrentTime(); }
bool app::timeValid() { return app::now() > 1735689600UL; }     // after 2025-01-01
void app::setTime(uint32_t epoch) { rtc_clock.setCurrentTime(epoch); nav.statusChanged(); }

// ---- app helpers ----------------------------------------------------------------------------
const char* app::batteryText() {
  static char b[12];
  if (!battery.present() || !battery.hasReading()) return "--";
  snprintf(b, sizeof(b), "%u%%", battery.percent());
  return b;
}
uint8_t app::batteryPct() { return battery.present() ? battery.percent() : 0; }
bool app::charging() { return battery.present() && battery.charging(); }
bool app::pluggedIn() { return battery.present() && battery.pluggedIn(); }
uint16_t app::batteryMv() { return battery.present() ? battery.millivolts() : 0; }
bool app::radioOk() { return s_radioOk; }
const char* app::radioFault() { return s_radioFault; }

static bool channelJoined(const ConvKey& k) {
  if (!g_node) return false;
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    ChannelDetails ch;
    if (g_node->getChannel(i, ch) && ch.name[0] && ConvKey::channel(ch.channel.secret) == k) return true;
  }
  return false;
}

// The badge counts only what the Messages list can show. History keeps a channel's
// messages after you leave it, and those used to count too, so the badge could say
// 2 with nothing there to open. Rechecked every few seconds as well, because
// leaving a channel changes what counts without adding a message.
uint16_t app::unread() {
  static uint32_t gen = 0, at = 0;
  static uint16_t cached = 0;
  if (gen == history.gen && at && millis() - at < 5000) return cached;
  gen = history.gen;
  at = millis() | 1;
  ConvKey keys[64];
  const uint16_t n = history.conversations(keys, 64);
  uint16_t total = 0;
  for (uint16_t i = 0; i < n; i++) {
    if (keys[i].type == CONV_CHANNEL && !channelJoined(keys[i])) continue;
    total += history.unread(keys[i]);
  }
  cached = total;
  return cached;
}

void app::applyDisplay() {
  if (power::saver()) {             // dimmer, and asleep sooner; the saved settings are untouched
    dimmer.setFull(min<uint8_t>(ui_settings.brightness, 4));
    dimmer.setTimes(min<uint16_t>(ui_settings.dimSecs, 10) * 1000UL, min<uint16_t>(ui_settings.sleepSecs, 30) * 1000UL);
    return;
  }
  dimmer.setFull(ui_settings.brightness);
  dimmer.setTimes(ui_settings.dimSecs * 1000UL, ui_settings.sleepSecs * 1000UL);
}
void app::applySound() { jingle.setVolume(ui_settings.sound ? ui_settings.volume : 0); }
void app::applyHaptics() { haptic.setMode(ui_settings.vibeMode); }

const ThemeSpec& app::themeSpec() { return THEMES[ui_settings.themeId < THEME_COUNT ? ui_settings.themeId : 0]; }

void app::applyTheme() {
  const ThemeSpec& th = themeSpec();
  theme.apply(display, th.palette, th.style);
  haptic.setPattern(th.vibeMsg.seq, th.vibeMsg.n);
  haptic.setTick(th.tickEffect, th.tickClamp);
  nav.invalidate();
}
// Contact/channel saves are written by a background task (tools/patch_meshcore.py);
// these wait for it, and report what it finished.
bool inwStoreFlush(uint32_t ms);
void inwStoreTick();
void inwSetUserBusy(bool busy);

static uint32_t s_hizUntil = 0;   // "batt hiz" test running until then

void app::reboot() {
  if (g_node) {
    if (g_node->hasPendingWork()) g_node->saveContactsNow();   // contact saves are batched; don't drop one
    g_node->savePrefsNow();
  }
  inwStoreFlush(10000);
  ui_settings.save(); delay(200); ESP.restart();
}
// For after contacts were deleted on purpose: saving the ones still in memory
// would write straight back what was just forgotten.
void app::rebootDiscard() {
  inwStoreFlush(10000);
  ui_settings.save(); delay(200); ESP.restart();
}

// Full-screen progress for slow storage jobs (contact saves, backups). Called from
// inside them, so it draws straight to the panel rather than through the view stack.
// done == total == 0 means finished.
void inwProgress(const char* what, uint32_t done, uint32_t total) {
  static uint32_t last = 0;
  static bool shown = false;
  if (!total) {
    if (shown) { shown = false; nav.invalidate(); }
    return;
  }
  if (dimmer.asleep()) return;
  if (shown && done < total && millis() - last < 80) return;
  last = millis();
  shown = true;
  Canvas& g = nav.canvas();
  const Theme& t = theme;
  g.fillScreen(t.bg);
  g.setTextDatum(textdatum_t::top_center);
  g.setFont(&fonts::Font4);
  g.setTextColor(t.green, t.bg);
  g.drawString(what, L::W / 2, 40);
  g.setFont(&fonts::Font2);
  g.setTextColor(t.dim, t.bg);
  g.drawString("please wait, don't power off", L::W / 2, 76);
  const int x = 30, y = 112, w = L::W - 60, h = 34;
  const int fill = (int)((uint64_t)(w - 8) * min(done, total) / total);
  g.drawRoundRect(x, y, w, h, 8, t.green);
  g.fillRoundRect(x + 4, y + 4, max(fill, 8), h - 8, 5, t.green);
  char pct[40];
  snprintf(pct, sizeof(pct), "%u%%  (%lu / %lu kB)", (unsigned)(100ULL * min(done, total) / total),
           (unsigned long)(done / 1024), (unsigned long)((total + 1023) / 1024));
  g.setTextColor(t.txt, t.bg);
  g.drawString(pct, L::W / 2, y + h + 14);
  g.setTextDatum(textdatum_t::top_left);
  g.pushSprite(nav.display(), 0, 0);
}
void app::lock() { if (!nav.top() || !nav.top()->isLock()) nav.push(makeLockView()); }

static bool quietHours();
// Plugged in: the theme's charge chime and one tap, like a phone. Quiet hours
// keep it silent; the screen still shows the charge mark.
void app::pluggedInFeedback() {
  nav.statusChanged();
  if (quietHours()) return;
  if (ui_settings.vibrate) { static const uint8_t TAP[] = {47}; haptic.pattern(TAP, 1); }
  if (ui_settings.sound) jingle.play(themeSpec().charge);
}

void app::testNotify() {
  haptic.pattern(app::themeSpec().vibeDm.seq, app::themeSpec().vibeDm.n);
  if (ui_settings.sound) jingle.play(app::themeSpec().dm);
  nav.banner("Test", "this is what a new message looks like");
}

bool app::myPosition(double& lat, double& lon) {
  if (gps.hasFix()) { lat = gps.fix().lat; lon = gps.fix().lon; return true; }
  if (g_node && (g_node->prefs().node_lat != 0 || g_node->prefs().node_lon != 0)) {
    lat = g_node->prefs().node_lat; lon = g_node->prefs().node_lon; return true;
  }
  return false;
}

double app::distanceKm(double lat1, double lon1, double lat2, double lon2) {
  const double r = 6371.0, p = M_PI / 180.0;
  const double a = 0.5 - cos((lat2 - lat1) * p) / 2 + cos(lat1 * p) * cos(lat2 * p) * (1 - cos((lon2 - lon1) * p)) / 2;
  return 2 * r * asin(sqrt(a));
}

const char* app::fmtDistance(double km) {
  static char b[16];
  if (ui_settings.miles) {
    const double mi = km * 0.621371;
    if (mi < 0.1) snprintf(b, sizeof(b), "%d ft", (int)(mi * 5280));
    else if (mi < 10) snprintf(b, sizeof(b), "%.1f mi", mi);
    else snprintf(b, sizeof(b), "%d mi", (int)lround(mi));
  } else {
    if (km < 1) snprintf(b, sizeof(b), "%d m", (int)(km * 1000));
    else if (km < 10) snprintf(b, sizeof(b), "%.1f km", km);
    else snprintf(b, sizeof(b), "%d km", (int)lround(km));
  }
  return b;
}

// Background contact saves and the daily backup stall the pager for seconds behind
// a progress screen. Hold them until the screen is off so they never land while
// someone is using it. If the screen has somehow been on for a whole day, go ahead
// rather than sit on a day of changes.
static uint32_t s_lastDarkAt = 0;
bool inwCanSaveNow() {
  return dimmer.asleep() || millis() - s_lastDarkAt > 24UL * 3600UL * 1000UL;
}

static bool s_gpsRail = true;   // bringup powers every rail
void gpsPower(bool on) {
  if (on == s_gpsRail) return;
  s_gpsRail = on;
  if (on) { expander.enableRail(EXP_GPS_EN, 20); gps.begin(); }
  else expander.digitalWrite(EXP_GPS_EN, LOW);
}

// GPS draws ~25 mA, more than the rest of the pager put together once the screen
// is off, and it used to stay on around the clock. Keep it on while the screen is
// on; with the screen off, give it a two-minute window every half hour so the
// clock and the advert position stay fresh.
static void gpsSchedule() {
  // Range test, SOS and the trail need a live position whatever the settings say.
  // When the last of them stops, hand the rail back to the normal rules.
  static bool fieldHad = false;
  if (field::wantsGps()) { fieldHad = true; gpsPower(true); return; }
  if (fieldHad) { fieldHad = false; if (!ui_settings.gpsOn || power::saver()) gpsPower(false); }
  if (!ui_settings.gpsOn || power::saver()) return;   // those paths switch it themselves
  const bool window = millis() % 1800000UL < 120000UL;
  gpsPower(!dimmer.asleep() || window);
}

// ---- alerts ------------------------------------------------------------------------------------
static bool quietHours() {
  if (ui_settings.dnd) return true;
  if (!ui_settings.dndSchedule || !app::timeValid()) return false;
  const time_t t = (time_t)app::now() + ui_settings.tzMinutes * 60;
  struct tm tm;
  gmtime_r(&t, &tm);
  const uint8_t h = tm.tm_hour, a = ui_settings.dndStart, b = ui_settings.dndEnd;
  return a <= b ? (h >= a && h < b) : (h >= a || h < b);
}

enum class AlertKind : uint8_t { Msg, Dm, Mention };

static void alert(const char* title, const char* text, AlertKind kind, bool silent = false) {
  const ThemeSpec& th = app::themeSpec();
  const Jingle* sound = kind == AlertKind::Dm ? th.dm : kind == AlertKind::Mention ? th.mention : th.msg;
  const VibePattern& vibe = kind == AlertKind::Dm ? th.vibeDm : kind == AlertKind::Mention ? th.vibeMention : th.vibeMsg;
  if (ui_settings.wakeOnMessage && !silent) dimmer.wake();
  nav.banner(title, text);
  if (silent || quietHours()) return;
  if (ui_settings.vibrate) haptic.pattern(vibe.seq, vibe.n);
  if (ui_settings.sound) jingle.play(sound);
  if (ui_settings.kbFlash) { keyboard.setBacklight(255); s_kbFlashUntil = millis() + 2500; }
}

static void onNodeEvent(NodeEvent e, const void* arg) {
  switch (e) {
    case NodeEvent::DirectMsg:
    case NodeEvent::RoomMsg: {
      const ContactInfo* c = (const ContactInfo*)arg;
      const ConvKey k = ConvKey::contact(c->id.pub_key);
      HistMsg* m = history.last(k);
      if (!m) return;
      const bool open = g_openConv == k && !dimmer.asleep();
      if (open) { history.markRead(k); if (ui_settings.keyHaptics) haptic.tick(); return; }
      const bool room = e == NodeEvent::RoomMsg;
      const uint8_t mode = notifyMode(k);
      if (mode == NM_MUTED) return;
      if (mode == NM_MENTIONS && !(m->flags & HF_MENTION)) return;
      if (mode == NM_DEFAULT && (room ? !ui_settings.notifyRoom : !ui_settings.notifyDM)) return;
      char text[200];
      if (room) snprintf(text, sizeof(text), "%s: %s", m->sender, m->text);
      else strlcpy(text, m->text, sizeof(text));
      alert(c->name, text, room ? AlertKind::Msg : AlertKind::Dm, mode == NM_SILENT);
      break;
    }
    case NodeEvent::ChannelMsg: {
      const int idx = *(const int*)arg;
      ChannelDetails ch;
      if (!g_node || idx < 0 || !g_node->getChannel(idx, ch)) return;
      const ConvKey k = ConvKey::channel(ch.channel.secret);
      HistMsg* m = history.last(k);
      if (!m) return;
      if (ui_settings.ignoreOneChar && strlen(m->text) <= 1) { if (g_openConv == k) history.markRead(k); return; }
      if (g_openConv == k && !dimmer.asleep()) { history.markRead(k); return; }
      const bool mention = m->flags & HF_MENTION;
      const uint8_t mode = notifyMode(k);
      if (mode == NM_MUTED) return;
      if (mode == NM_MENTIONS && !mention) return;
      if (mode == NM_DEFAULT) {                     // the global channel settings
        if (!ui_settings.notifyChannel && !mention) return;
        if (ui_settings.channelMentionsOnly && !mention) return;
      }
      char title[48], text[200];
      snprintf(title, sizeof(title), "%s%s", ch.name, mention ? "  @you" : "");
      snprintf(text, sizeof(text), "%s: %s", m->sender, m->text);
      alert(title, text, mention ? AlertKind::Mention : AlertKind::Msg, mode == NM_SILENT);
      break;
    }
    case NodeEvent::NewContact:
      if (ui_settings.notifyNewContact) nav.banner("New contact", ((const ContactInfo*)arg)->name, 3000);
      break;
    case NodeEvent::Failed: {
      HistMsg* m = history.find(*(const uint32_t*)arg);
      if (m && !(g_openConv == m->conv)) nav.toast("a message was not delivered");
      break;
    }
    case NodeEvent::LoginOk:   nav.toast(g_node->loginIsAdmin() ? "logged in as admin" : "logged in"); break;
    case NodeEvent::LoginFail: nav.toast("login failed or timed out"); break;
    default: break;
  }
  nav.statusChanged();
}

// ---- screenshot -----------------------------------------------------------------------------------
static void takeScreenshot() {
  if (!sdMount()) { nav.toast("no sd card"); return; }
  if (!SD.exists("/screenshots")) SD.mkdir("/screenshots");
  char path[48];
  snprintf(path, sizeof(path), "/screenshots/inw_%lu.bmp", (unsigned long)(app::timeValid() ? app::now() : millis()));
  File f = SD.open(path, FILE_WRITE);
  if (!f) { nav.toast("sd write failed"); return; }
  const int W = L::W, H = L::H;
  const uint32_t rowBytes = W * 3, dataSize = rowBytes * H, fileSize = 54 + dataSize;
  uint8_t hdr[54] = { 'B', 'M' };
  auto put32 = [&](int o, uint32_t v) { hdr[o] = v; hdr[o + 1] = v >> 8; hdr[o + 2] = v >> 16; hdr[o + 3] = v >> 24; };
  put32(2, fileSize); put32(10, 54); put32(14, 40); put32(18, W); put32(22, H);
  hdr[26] = 1; hdr[28] = 24; put32(34, dataSize);
  f.write(hdr, 54);
  static uint8_t row[L::W * 3];
  lgfx::rgb888_t px[L::W];
  for (int y = H - 1; y >= 0; y--) {
    display.readRect(0, y, W, 1, px);
    for (int x = 0; x < W; x++) { row[x * 3] = px[x].b; row[x * 3 + 1] = px[x].g; row[x * 3 + 2] = px[x].r; }
    f.write(row, rowBytes);
  }
  f.close();
  nav.toast("screenshot saved to sd");
}

// ---- USB commands ------------------------------------------------------------------------------------
// A few line commands on the USB serial port, for capturing documentation
// screenshots from a computer (tools/capture_screens.py):
//   shot        stream the screen: "SHOT565 480 222\n" then W*H big-endian RGB565 words
//   theme N     switch theme
//   key C       press a key (\n for enter)
//   home, lock, wheel +N / -N, press
//   dfu         restart into the ROM's USB download mode, ready for esptool or
//               the web installer (flash with --before no_reset)

// The flag that sends the chip into download mode sits in the RTC domain, which
// the battery keeps alive, so clear it as soon as we're running: one trip into
// flash mode can never turn into a device that always boots there.
static void clearForceDownloadBoot() { REG_WRITE(RTC_CNTL_OPTION1_REG, 0); }

// Restarts into the chip's own download mode: no BOOT/RESET buttons needed.
void app::rebootToFlashMode() {
  if (g_node) {
    if (g_node->hasPendingWork()) g_node->saveContactsNow();
    g_node->savePrefsNow();
  }
  inwStoreFlush(10000);
  ui_settings.save();
  Serial.println("[INW] restarting into usb flash mode");
  Serial.flush();
  delay(200);
  REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
  esp_restart();
}

static void usbCommands() {
  static char line[64];
  static uint8_t n = 0;
  while (Serial.available()) {
    const char c = Serial.read();
    if (c != '\n' && c != '\r') { if (n < sizeof(line) - 1) line[n++] = c; continue; }
    line[n] = 0;
    n = 0;
    if (!line[0]) continue;
    // Anyone with a USB cable could send "press" or "key" to get past the lock
    // screen, or "shot" to read what's on it. While locked (or dark, which locks)
    // only commands that don't reveal or unlock anything are accepted.
    const bool locked = dimmer.asleep() || (nav.top() && nav.top()->isLock());
    if (locked && (!strcmp(line, "shot") || !strcmp(line, "home") || !strcmp(line, "press") ||
                   !strncmp(line, "key ", 4) || !strncmp(line, "wheel ", 6) || !strncmp(line, "theme ", 6))) {
      Serial.println("[usb] pager is locked: unlock it on the device first");
      continue;
    }
    // One line the installer (and anyone with a serial monitor) can ask for, so
    // nobody has to catch the boot report as it scrolls past. Answered without
    // waking the screen.
    // Diagnostic for the "a website update wipes my settings" report: how full
    // NVS is, what it holds, and the settings most likely to be noticed missing.
    if (!strcmp(line, "nvs")) {
      nvs_stats_t st;
      if (nvs_get_stats(nullptr, &st) == ESP_OK)
        Serial.printf("[nvs] used %d / %d entries (%d free, %d namespaces)\n",
                      st.used_entries, st.total_entries, st.free_entries, st.namespace_count);
      else
        Serial.println("[nvs] stats unavailable");
      Preferences p;
      if (p.begin("inw-ui", true)) {
        Serial.printf("[nvs] inw-ui ver=%u blob=%u free=%u\n", p.getUChar("ver", 0),
                      (unsigned)p.getBytesLength("blob"), (unsigned)p.freeEntries());
        p.end();
      } else Serial.println("[nvs] inw-ui missing");
      if (p.begin("inw-keep", true)) {
        Serial.printf("[nvs] inw-keep id=%u ch=%u pr=%u\n", (unsigned)p.getBytesLength("id"),
                      (unsigned)p.getBytesLength("ch"), (unsigned)p.getBytesLength("pr"));
        p.end();
      } else Serial.println("[nvs] inw-keep missing");
      Serial.printf("[ui] theme=%u bright=%u wifi=%d beta=%d lockOnSleep=%d wheelUnlock=%d vol=%u tz=%d\n",
                    ui_settings.themeId, ui_settings.brightness, ui_settings.wifiOn,
                    ui_settings.betaUpdates, ui_settings.lockOnSleep, ui_settings.wheelUnlock,
                    ui_settings.volume, ui_settings.tzMinutes);
      Serial.printf("[files] channels2=%d prefs=%d identity=%d contacts3=%d\n",
                    (int)(SPIFFS.exists("/channels2") ? SPIFFS.open("/channels2").size() : -1),
                    (int)(SPIFFS.exists("/prefs.json") ? SPIFFS.open("/prefs.json").size() : -1),
                    (int)(SPIFFS.exists("/identity/_main.id") ? SPIFFS.open("/identity/_main.id").size() : -1),
                    (int)(SPIFFS.exists("/contacts3") ? SPIFFS.open("/contacts3").size() : -1));
      continue;
    }
    // "set <name> <value>" for the handful of settings worth putting back over
    // USB after NVS has been wiped. Refused while locked, like "theme": a cable
    // must not be able to turn the lock screen off.
    if (!strncmp(line, "set ", 4)) {
      if (locked) { Serial.println("[usb] pager is locked: unlock it on the device first"); continue; }
      char name[24] = "";
      long v = 0;
      if (sscanf(line + 4, "%23s %ld", name, &v) != 2) { Serial.println("[set] usage: set <name> <value>"); continue; }
      UiSettings& u = ui_settings;
      bool known = true;
      if (!strcmp(name, "theme") && v >= 0 && v < THEME_COUNT) u.themeId = v;
      else if (!strcmp(name, "bright") && v >= 1 && v <= 16) u.brightness = v;
      else if (!strcmp(name, "vol") && v >= 0 && v <= 100) u.volume = v;
      else if (!strcmp(name, "wifi")) u.wifiOn = v != 0;
      else if (!strcmp(name, "beta")) u.betaUpdates = v != 0;
      else if (!strcmp(name, "lock")) u.lockOnSleep = v != 0;
      else if (!strcmp(name, "wheel")) u.wheelUnlock = v != 0;
      else if (!strcmp(name, "tz")) u.tzMinutes = v;
      else known = false;
      if (!known) { Serial.printf("[set] unknown setting '%s'\n", name); continue; }
      u.save();
      app::applyTheme(); app::applyDisplay(); app::applySound();
      markUiDirty(); nav.invalidate();
      Serial.printf("[set] %s = %ld\n", name, v);
      continue;
    }
    // How much SPIFFS is in use and how many files are in it: MeshCore keeps one
    // advert blob per contact, and SPIFFS gets slow when a directory grows.
    if (!strcmp(line, "fs")) {
      Serial.printf("[fs] spiffs %u / %u bytes used\n",
                    (unsigned)SPIFFS.usedBytes(), (unsigned)SPIFFS.totalBytes());
      File root = SPIFFS.open("/");
      int files = 0; size_t bytes = 0;
      for (File f = root.openNextFile(); f; f = root.openNextFile()) { files++; bytes += f.size(); }
      Serial.printf("[fs] %d files, %u bytes\n", files, (unsigned)bytes);
      continue;
    }
    // The browser installer sends this before it resets the pager, so nothing
    // learned since the last lazy write is lost to the flash. Cheap enough to
    // run on demand: contacts are only written if there is something pending.
    if (!strcmp(line, "save")) {
      if (g_node) {
        if (g_node->hasPendingWork()) g_node->saveContactsNow();
        g_node->savePrefsNow();
      }
      ui_settings.save();
      const bool landed = inwStoreFlush(10000);      // "ok" means on flash, not just queued
      Serial.printf("[save] %s contacts=%d\n", landed ? "ok" : "slow", g_node ? g_node->getNumContacts() : -1);
      continue;
    }
    if (!strcmp(line, "backup")) {          // same job as Settings -> back up to sd now
      Serial.printf("[backup] %s\n", sdBackupNow(true));
      continue;
    }
    if (!strcmp(line, "log")) {             // the on-device log, including "slow" stalls
      for (uint8_t i = 0; i < logs.count(); i++) Serial.printf("[log] %s\n", logs.line(i));
      Serial.println("[log] end");
      continue;
    }
    if (!strcmp(line, "batt")) { battery.report(); continue; }
    // Run from the battery with the cable still in, to check the figure on
    // battery. Turns itself back off after the given minutes (at most 30).
    if (!strncmp(line, "batt hiz ", 9)) {
      const int min = atoi(line + 9);
      if (min > 0) { battery.setHiZ(true); s_hizUntil = (millis() + constrain(min, 1, 30) * 60000UL) | 1; }
      else { battery.setHiZ(false); s_hizUntil = 0; }
      battery.report();
      continue;
    }
    if (!strcmp(line, "status")) {
      Serial.printf("[status] fw=%s radio=%s radio_ok=%d contacts=%d\n",
                    FW_VERSION, radio_chip, s_radioOk ? 1 : 0,
                    g_node ? g_node->getNumContacts() : -1);
      continue;
    }
    dimmer.note();
    if (!strcmp(line, "stores")) {
      storeReport();
    } else if (!strcmp(line, "recover")) {
      Serial.printf("[stores] %s\n", recoverMissingContacts());
      storeReport();
    } else if (!strcmp(line, "shot")) {
      // Compose the current screen into the canvas and send that: exact
      // colours, unlike reading the panel back.
      View* v = nav.top();
      Canvas& g = nav.canvas();
      g.fillScreen(theme.bg);
      if (v && !v->isLock()) drawStatusBar(g, theme);
      if (v) v->draw(g);
      nav.drawOverlays(g);
      Serial.flush();
      Serial.printf("SHOT565 %d %d\n", L::W, L::H);
      Serial.write((const uint8_t*)g.getBuffer(), L::W * L::H * 2);
      Serial.flush();
    } else if (!strncmp(line, "theme ", 6)) {
      const int t = atoi(line + 6);
      if (t >= 0 && t < THEME_COUNT) { ui_settings.themeId = t; app::applyTheme(); markUiDirty(); }
    } else if (!strncmp(line, "key ", 4)) {
      nav.key(line[4] == '\\' && line[5] == 'n' ? '\n' : line[4]);
    } else if (!strcmp(line, "home")) {
      nav.popToHome();
    } else if (!strcmp(line, "lock")) {
      app::lock();
    } else if (!strncmp(line, "wheel ", 6)) {
      nav.rotate(atoi(line + 6));
    } else if (!strcmp(line, "press")) {
      nav.press();
    } else if (!strcmp(line, "dfu")) {
      app::rebootToFlashMode();
    }
    nav.invalidate();
  }
}

// ---- boot screen ---------------------------------------------------------------------------------
// The INW mark and wordmark, with a thin progress bar underneath. Steps that fail
// are listed below the bar; everything else only goes to the serial log.
constexpr int BOOT_STEPS = 12;
static int s_bootStep = 0, s_bootErrY = 196;

// The panel shares its SPI bus with the SD card and the radio, and the boot
// animation draws from its own task while setup() is busy restoring from SD or
// starting the radio. The SD and radio drivers hold this SPIClass's transaction
// lock for every exchange, so taking the same lock around each boot-screen draw
// keeps a frame from ever landing in the middle of an SD write. Only display
// calls go inside one: anything that itself uses inw_spi would deadlock.
struct BootBusLock {
  BootBusLock()  { inw_spi.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0)); }
  ~BootBusLock() { inw_spi.endTransaction(); }
};

// A small mesh: five nodes, each linked to its neighbours. While booting, a
// packet hops round the outer ring so a slow step never looks like a freeze.
static const int16_t LOGO_NX[] = {88, 128, 156, 112, 70}, LOGO_NY[] = {58, 44, 90, 126, 108};
static const uint8_t LOGO_LINKS[][2] = {{0,1},{1,2},{2,3},{3,4},{4,0},{0,2},{1,3}};
constexpr int LOGO_X = 54, LOGO_Y = 28, LOGO_W = 124, LOGO_H = 118;   // covers every ring
constexpr uint32_t HOP_MS = 420;

// ox/oy shift the drawing into a sprite; animate=false is the still mark.
static void drawLogoMark(lgfx::LovyanGFX& g, int ox, int oy, uint32_t ms, bool animate) {
  const int hop = (ms / HOP_MS) % 5;                    // links 0..4 are the outer ring
  const float f = (ms % HOP_MS) / (float)HOP_MS;
  for (int i = 0; i < 7; i++) {
    auto& l = LOGO_LINKS[i];
    g.drawLine(LOGO_NX[l[0]] + ox, LOGO_NY[l[0]] + oy, LOGO_NX[l[1]] + ox, LOGO_NY[l[1]] + oy,
               animate && i == hop ? theme.green : theme.greenDim);
  }
  for (int i = 0; i < 5; i++) {
    const bool big = i == 2;
    // the node the packet just reached flares for the first part of the next hop
    const bool lit = animate && i == LOGO_LINKS[(hop + 4) % 5][1] && f < 0.45f;
    g.fillCircle(LOGO_NX[i] + ox, LOGO_NY[i] + oy, big ? 9 : 6, lit ? theme.txt : theme.green);
    g.drawCircle(LOGO_NX[i] + ox, LOGO_NY[i] + oy, (big ? 13 : 9) + (lit ? 1 : 0), lit ? theme.green : theme.greenDim);
  }
  if (animate) {
    auto& l = LOGO_LINKS[hop];
    const int px = LOGO_NX[l[0]] + (LOGO_NX[l[1]] - LOGO_NX[l[0]]) * f + ox;
    const int py = LOGO_NY[l[0]] + (LOGO_NY[l[1]] - LOGO_NY[l[0]]) * f + oy;
    g.fillCircle(px, py, 3, theme.txt);
  }
}

static volatile bool s_animRun = false;
static SemaphoreHandle_t s_animDone = nullptr;

static void bootAnimTask(void*) {
  Canvas spr;
  spr.setColorDepth(16);
  if (spr.createSprite(LOGO_W, LOGO_H)) {
    const uint32_t t0 = millis();
    while (s_animRun) {
      spr.fillScreen(theme.bg);
      drawLogoMark(spr, -LOGO_X, -LOGO_Y, millis() - t0, true);
      { BootBusLock lock; spr.pushSprite(&display, LOGO_X, LOGO_Y); }
      vTaskDelay(pdMS_TO_TICKS(40));
    }
    spr.fillScreen(theme.bg);                            // leave the still mark behind
    drawLogoMark(spr, -LOGO_X, -LOGO_Y, 0, false);
    { BootBusLock lock; spr.pushSprite(&display, LOGO_X, LOGO_Y); }
    spr.deleteSprite();
  }
  xSemaphoreGive(s_animDone);
  vTaskDelete(nullptr);
}

static void bootAnimStart() {
  s_animDone = xSemaphoreCreateBinary();
  if (!s_animDone) return;                               // no animation, boot goes on as before
  s_animRun = true;
  // core 0, away from setup(); if it can't start, the still logo is already up
  if (xTaskCreatePinnedToCore(bootAnimTask, "bootanim", 4096, nullptr, 1, nullptr, 0) != pdPASS) {
    s_animRun = false;
    vSemaphoreDelete(s_animDone);
    s_animDone = nullptr;
  }
}

static void bootAnimStop() {                             // before anything else owns the screen
  if (!s_animDone) return;
  s_animRun = false;
  xSemaphoreTake(s_animDone, pdMS_TO_TICKS(2000));
}

static void drawBootLogo() {
  display.fillScreen(theme.bg);
  drawLogoMark(display, 0, 0, 0, false);
  display.setFont(&fonts::FreeSansBold24pt7b);
  display.setTextSize(1);                         // "SQUATCH" at size 2 would run off the screen
  display.setTextColor(theme.greenDim);
  display.drawString("SQUATCH", 199, 49);         // offset copy underneath reads as glow
  display.setTextColor(theme.green);
  display.drawString("SQUATCH", 196, 46);
  display.setFont(&fonts::FreeSans12pt7b);
  display.setTextColor(theme.txt);
  display.drawString("M E S H", 198, 106);
  display.setFont(&fonts::Font2);
  display.setTextColor(theme.dim);
  display.drawString("inland northwest  //  " FW_VERSION, 198, 146);
  display.drawRect(90, 184, 300, 5, theme.line);
}

static uint32_t s_bootT0 = 0, s_bootStepAt = 0;   // so a slow boot says which step was slow
static const char* s_stepName[BOOT_STEPS] = {};
static uint32_t s_stepMs[BOOT_STEPS] = {};

static void bootTimingReport() {
  Serial.printf("[boot] took %lums total:", (unsigned long)(millis() - s_bootT0));
  for (int i = 0; i < BOOT_STEPS; i++)
    if (s_stepName[i]) Serial.printf("  %s %lums", s_stepName[i], (unsigned long)s_stepMs[i]);
  Serial.println();
}

static void bootStep(const char* what, bool ok, const char* detail = nullptr) {
  s_bootStep = min(s_bootStep + 1, BOOT_STEPS);
  const uint32_t at = millis();
  const uint32_t took = at - (s_bootStepAt ? s_bootStepAt : s_bootT0);
  Serial.printf("[boot] %-18s %s %5lums (%lums in) %s\n", what, ok ? "ok" : "FAILED",
                (unsigned long)took, (unsigned long)(at - s_bootT0), detail ? detail : "");
  s_bootStepAt = at;
  // Native USB re-enumerates during a reset, so the early lines above are lost to
  // anyone watching from a PC. Keep them and print the lot once at the end.
  if (s_bootStep <= BOOT_STEPS) { s_stepName[s_bootStep - 1] = what; s_stepMs[s_bootStep - 1] = took; }
  BootBusLock lock;
  display.fillRect(91, 185, 298 * s_bootStep / BOOT_STEPS, 3, theme.green);
  if (ok || s_bootErrY > 210) return;
  display.setFont(&fonts::Font2);
  display.setTextColor(theme.amber, theme.bg);
  char line[64];
  snprintf(line, sizeof(line), "%s: %s", what, detail && detail[0] ? detail : "not responding");
  display.drawString(line, (L::W - display.textWidth(line)) / 2, s_bootErrY);
  s_bootErrY += 14;
}

static void bootNote(const char* msg) {      // a long step the user should know about
  BootBusLock lock;
  display.setFont(&fonts::Font2);
  display.fillRect(0, 194, L::W, 28, theme.bg);
  display.setTextColor(theme.amber, theme.bg);
  display.drawString(msg, (L::W - display.textWidth(msg)) / 2, 198);
}

void setup() {
  clearForceDownloadBoot();     // first thing: one trip into flash mode stays one trip
  s_bootT0 = millis();
  Serial.begin(115200);
  delay(150);
  Serial.println("\n[INW] boot " FW_VERSION);
  static const char* RESET[] = {"unknown", "power on", "external", "software", "crash",
                                 "interrupt watchdog", "task watchdog", "watchdog", "deep sleep",
                                 "brownout", "sdio"};
  const int rr = (int)esp_reset_reason();
  logs.add(rr == ESP_RST_POWERON || rr == ESP_RST_SW ? LOG_INFO : LOG_WARN, "boot %s, last reset: %s",
           FW_VERSION, rr < 11 ? RESET[rr] : "?");
  ui_settings.load();

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  rotary.begin(PIN_ROTARY_A, PIN_ROTARY_B, PIN_ROTARY_PRESS);
  // Every chip-select on the shared SPI bus idles high before anything talks on
  // it, or a floating radio/NFC select answers the SD card's traffic.
  for (int cs : { PIN_LORA_CS, PIN_NFC_CS, PIN_SD_CS }) { pinMode(cs, OUTPUT); digitalWrite(cs, HIGH); }

  const bool railsOk = expander.begin(Wire);
  if (railsOk) {
    bringup::powerAllRails(expander);
    expander.enableRail(EXP_LORA_EN, 20);
    if (!ui_settings.gpsOn) gpsPower(false);
  }

  display.init();
  display.setRotation(TFT_ROTATION);
  app::applyTheme();
  if (digitalRead(PIN_BUTTON) == LOW) {        // BOOT held: hardware self test
    backlight.begin(PIN_TFT_BL);
    backlight.setLevel(12);
    bringup::scanToSerial(Wire);
    bringup::report(display, Wire);
    while (digitalRead(PIN_BUTTON) == LOW) delay(10);
    delay(3000);
  }
  drawBootLogo();
  bootAnimStart();
  backlight.begin(PIN_TFT_BL);
  dimmer.begin(&backlight, ui_settings.brightness, 3, ui_settings.dimSecs * 1000UL, ui_settings.sleepSecs * 1000UL);

  bootStep("power rails", railsOk);
  haptic.begin(Wire);
  haptic.setMode(ui_settings.vibeMode);
  bootStep("vibration", haptic.ok());
  bootStep("keyboard", keyboard.begin(Wire));
  keyboard.setBacklight(ui_settings.kbBacklight);
  bootStep("battery gauge", battery.begin(Wire));
  battery.tick(millis());
  if (battery.present())
    logs.add(battery.configured() ? LOG_WARN : LOG_INFO, "battery %u%% (gauge %u%%) %umV, pack %umAh%s", battery.percent(),
             battery.gaugePercent(), battery.millivolts(), battery.designNow(),
             battery.configured() ? " (was set wrong, fixed)" : "");
  const bool audioOk = codec.begin(Wire);
  jingle.begin(&codec);
  // The amp is powered only while a sound plays (it hisses and drains otherwise).
  jingle.setAmp([](bool on) { expander.digitalWrite(EXP_AMP_EN, on ? HIGH : LOW); });
  if (railsOk) expander.digitalWrite(EXP_AMP_EN, LOW);
  app::applySound();
  bootStep("audio", audioOk);
  if (ui_settings.gpsOn) gps.begin();

  // Clock: the board RTC first, so the mesh has real time from the first packet.
  const bool rtcOk = rtc.begin(Wire);
  RtcTime rt;
  if (rtcOk && rtc.read(rt) && rt.year >= 2025) rtc_clock.setQuiet(epochFrom(rt.year, rt.month, rt.day, rt.hour, rt.minute, rt.second));
  rtc_clock.onSet = writeHardwareRtc;
  bootStep("clock", rtcOk, app::timeValid() ? clockText(app::now(), true) : "not set");

  bool fsOk = SPIFFS.begin(false);
  if (!fsOk) {
    // First boot on this partition table: the store has to be formatted once.
    bootNote("first start: preparing storage, this takes a few minutes");
    fsOk = SPIFFS.begin(true);
    BootBusLock lock;
    display.fillRect(0, 194, L::W, 28, theme.bg);
  }
  bootStep("storage", fsOk);
  const bool sdOk = sdMount();
  bootStep("sd card", true, sdOk ? "mounted" : "none");   // no card is normal
  // NVS came up empty (wiped, or another firmware had the board): bring the
  // preferences back from the copies rather than starting at defaults.
  if (fsOk && !ui_settings.cameFromNvs()) {
    const char* from = ui_settings.restoreIfWiped(sdOk);
    if (from) {
      app::applyTheme();
      app::applyDisplay();
      app::applySound();
      dimmer.setFull(ui_settings.brightness);
      keyboard.setBacklight(ui_settings.kbBacklight);
      logs.add(LOG_WARN, "settings were empty, restored from %s", from);
    }
    // No bootStep here: it would make the progress bar jump on the rare boot
    // that restores. The log line says what happened.
    Serial.printf("[boot] settings           %s\n", from ? from : "defaults (no copy to restore)");
  } else if (fsOk) {
    ui_settings.saveMirror();               // keep the copy current from the first boot
  }
  char report[96];
  importBeforeNode(report, sizeof(report));
  bootStep("restore", true, report);
  bootStep("messages", history.begin());

  board.battReader = [] { return battery.millivolts(); };
  s_radioOk = nodeBegin();
  char rinfo[48] = "";
  // radio_chip says which of the pager's two radios answered.
  if (s_radioOk) snprintf(rinfo, sizeof(rinfo), "%s  %.3f MHz sf%u", radio_chip, g_node->prefs().freq, g_node->prefs().sf);
  else strlcpy(rinfo, "not responding", sizeof(rinfo));
  bootStep("radio", s_radioOk, rinfo);
  if (s_radioOk) logs.add(LOG_INFO, "radio up");
  else logs.add(LOG_ERROR, "radio init failed: %s", s_radioFault);
  if (s_radioOk) keepEssentials();
  if (s_radioOk) {
    importPrefsAfterNode(report, sizeof(report));
    bootStep("import", true, report);
    g_node->onEvent = onNodeEvent;
    char info[64];
    snprintf(info, sizeof(info), "%s  %d contacts", g_node->name(), g_node->getNumContacts());
    bootStep("mesh node", true, info);
    logs.add(LOG_INFO, "%s", info);
  }
  wifi::begin();
  s_bootStep = BOOT_STEPS - 1;
  bootStep("ready", true);
  bootAnimStop();
  bootTimingReport();
  if (ui_settings.bootJingle && ui_settings.sound) jingle.play(app::themeSpec().boot);

  nav.begin(&display, &theme);
  nav.push(makeHomeView());
  // A new node is named after its key prefix; ask for a real name once.
  if (g_node) {
    char hex[10];
    mesh::Utils::toHex(hex, g_node->self_id.pub_key, 4);
    if (!strcmp(g_node->name(), hex)) {
      prompt("Name this node", "how you appear on the mesh", "", 31, [](const String& s) {
        if (!s.length()) return;
        strlcpy(g_node->prefs().node_name, s.c_str(), sizeof(g_node->prefs().node_name));
        g_node->savePrefsNow();
        g_node->advertZeroHop();
      });
    }
  }

  // Hold the logo a moment (longer if something failed); any key skips it.
  const uint32_t until = millis() + (s_bootErrY > 196 ? 5000 : 1200);
  while ((int32_t)(millis() - until) < 0) {
    jingle.tick();
    if (rotary.takeDetents() || rotary.takePress()) break;
    KeyEvent ev;
    if (keyboard.read(ev) && ev.pressed) break;
    if (g_node) nodeLoop();
    delay(5);
  }
  nav.invalidate();
}

// ---- loop -------------------------------------------------------------------------------------------
static void gpsTick() {
  if ((!ui_settings.gpsOn || power::saver()) && !field::wantsGps()) return;
  const uint32_t t0 = millis(), b0 = gps.bytesRead;
  const bool parsed = gps.update();
  if (millis() - t0 > 100)
    logs.add(LOG_WARN, "gps read %lums, %lu bytes", (unsigned long)(millis() - t0), (unsigned long)(gps.bytesRead - b0));
  if (!parsed) return;
  static bool hadFix = false;
  static uint32_t lastClock = 0, lastPosSave = 0;
  static double savedLat = 0, savedLon = 0;
  const GpsFix& f = gps.fix();
  if (gps.hasFix() != hadFix) {
    hadFix = gps.hasFix();
    logs.add(LOG_INFO, hadFix ? "gps fix" : "gps fix lost");
    nav.statusChanged();
  }
  if (!gps.hasFix()) return;
  if (ui_settings.gpsSetsClock && f.year >= 2025 && (!lastClock || millis() - lastClock > 3600000UL)) {
    const uint32_t e = epochFrom(f.year, f.month, f.day, f.hour, f.minute, f.second);
    if (!app::timeValid() || labs((long)e - (long)app::now()) > 5) app::setTime(e);
    lastClock = millis();
  }
  if (ui_settings.gpsLivePosition && g_node) {
    sensors.node_lat = f.lat; sensors.node_lon = f.lon;
    NodePrefs& p = g_node->prefs();
    p.node_lat = f.lat; p.node_lon = f.lon;
    // Persist only when it actually moved, and not more than every 10 minutes.
    if ((!lastPosSave || millis() - lastPosSave > 600000UL) &&
        app::distanceKm(savedLat, savedLon, f.lat, f.lon) > 0.05) {
      savedLat = f.lat; savedLon = f.lon; lastPosSave = millis();
      g_node->savePrefsNow();
    }
  }
}

static void autoAdvertTick() {
  static uint32_t bootAdvert = 8000, last = 0;
  if (!g_node) return;
  if (bootAdvert && millis() > bootAdvert) { bootAdvert = 0; g_node->advertZeroHop(); last = millis(); }
  if (ui_settings.autoAdvertHours && millis() - last > ui_settings.autoAdvertHours * 3600000UL) {
    last = millis();
    g_node->advertFlood();
    logs.add(LOG_INFO, "auto flood advert");
  }
}

void loop() {
  // Anything that holds the loop long enough to feel like a freeze gets logged
  // with where the time went.
  const uint32_t tLoop = millis();
  uint32_t lapAt = tLoop;
  uint16_t laps[8] = {};
  auto lap = [&](uint8_t i) { const uint32_t now = millis(); laps[i] = now - lapAt; lapAt = now; };
  const int8_t detents = rotary.takeDetents();
  const bool press = rotary.takePress();
  bool backspace = false, anyKey = false;
  char chars[16];
  uint8_t nchars = 0;
  KeyEvent ev;
  while (keyboard.read(ev)) {
    if (!ev.pressed) continue;
    anyKey = true;
    if (ev.index == KEY_IDX_BACKSPACE) backspace = true;
    else if (ev.ch && nchars < sizeof(chars)) chars[nchars++] = ev.ch;
  }
  static bool btnWas = false;
  const bool btn = digitalRead(PIN_BUTTON) == LOW;
  bool btnPress = btn && !btnWas;
  btnWas = btn;
  // Five fast presses arm an SOS (field tools). That press then only wakes the
  // screen to show the countdown, instead of locking it again.
  if (btnPress) {
    View* before = nav.top();
    field::sosNoteButton();
    if (nav.top() != before) { btnPress = false; dimmer.note(); nav.invalidate(); }
  }

  if (dimmer.asleep()) {
    // Screen off: only the side button wakes it. Keys and the wheel get pressed in
    // a pocket, and each stray wake lit the screen and let the next bump unlock it.
    // Their events were read above so they don't pile up; here they are dropped.
    if (btnPress) { dimmer.note(); if (ui_settings.lockOnSleep) app::lock(); nav.invalidate(); }
  } else if (detents || press || anyKey || btnPress) {
    // With wheel-only unlock, on the lock screen only a wheel press counts as someone
    // using it, so stray keys can't keep a pocketed screen lit.
    const bool onLock = nav.top() && nav.top()->isLock();
    if (!onLock || !ui_settings.wheelUnlock || press || btnPress) dimmer.note();
    if (btnPress) {
      // Like a phone: the side button locks and turns the screen off at once.
      app::lock();
      dimmer.sleepNow();
    } else {
      if (detents && ui_settings.scrollTick) haptic.tick();
      else if ((anyKey || press) && ui_settings.keyHaptics) haptic.tick();
      // Which input, on which screen, took the time (the "in" column of the slow log).
      char before[32];
      strlcpy(before, g_screenTitle, sizeof(before));
      const uint32_t tIn = millis();
      const char* what = detents ? "wheel" : press ? "press" : backspace ? "backspace" : "key";
      if (detents) nav.rotate(detents);
      if (press) nav.press();
      for (uint8_t i = 0; i < nchars; i++) {
        View* v = nav.top();
        if (chars[i] == '\n' && v && !v->wantsAllKeys() && !v->isHome() && !v->isLock()) nav.press();
        else nav.key(chars[i]);
      }
      if (backspace) nav.backspace();
      if (millis() - tIn > 120)
        Serial.printf("[W] input slow: %s on '%s' took %lums\n", what, before, (unsigned long)(millis() - tIn));
    }
  }

  // Screen went dark: next wake lands on the lock face.
  static bool wasAsleep = false;
  if (dimmer.asleep() && !wasAsleep && ui_settings.lockOnSleep) app::lock();
  wasAsleep = dimmer.asleep();

  lap(0);
  gpsTick();
  lap(1);
  wifi::tick();
  { static bool w = false; if (wifi::connected() != w) { w = wifi::connected(); nav.statusChanged(); } }
  lap(2);
  battery.tick(millis());
  if (s_hizUntil && (int32_t)(millis() - s_hizUntil) > 0) { s_hizUntil = 0; battery.setHiZ(false); Serial.println("[batt] charger input back on"); }
  power::tick();
  ota::tick();
  nodeLoop();
  lap(3);
  nav.tick();
  lap(4);
  if (!dimmer.asleep() || nav.overlayActive()) nav.draw();
  if (!dimmer.asleep() && nav.top() && !nav.top()->isLock()) animateBatteryIcon(nav.display(), theme);
  lap(5);
  dimmer.tick();
  jingle.tick();

  // The panel follows the backlight: once it has been dark a moment it also gets
  // its sleep command, which saves more than the backlight alone.
  {
    static bool panelOff = false;
    static uint32_t darkSince = 0;
    if (dimmer.asleep()) {
      s_lastDarkAt = millis();
      if (!darkSince) darkSince = millis() | 1;
      if (!panelOff && millis() - darkSince > 1000) { display.sleep(); panelOff = true; }
    } else {
      darkSince = 0;
      if (panelOff) { display.wakeup(); panelOff = false; nav.invalidate(); }
    }
  }
  // Woken onto the lock screen and left alone (a message, a bump of the button):
  // back to sleep in 10 s instead of waiting out the dim and sleep timers.
  if (!dimmer.asleep() && nav.top() && nav.top()->isLock() && dimmer.idleFor() > 10000UL) dimmer.sleepNow();
  gpsSchedule();

  // Keyboard light follows the screen (or flashes for a message).
  {
    static uint8_t prev = 1;
    uint8_t kb = dimmer.asleep() || power::saver() ? 0 : dimmer.dimmed() ? ui_settings.kbBacklight / 6 : ui_settings.kbBacklight;
    if (s_kbFlashUntil) { if ((int32_t)(millis() - s_kbFlashUntil) < 0) kb = 255; else s_kbFlashUntil = 0; }
    if (kb != prev) { keyboard.setBacklight(kb); prev = kb; }
  }

  // Phone link changes show in the status bar.
  static bool bleWas = false;
  if (bleConnected() != bleWas) { bleWas = bleConnected(); nav.statusChanged(); if (bleWas) nav.toast("phone connected"); }

  if (s_prefsDirtyAt && millis() - s_prefsDirtyAt > 3000) { s_prefsDirtyAt = 0; if (g_node) g_node->savePrefsNow(); }
  // No contacts flush here on purpose. MeshCore already writes them lazily, five
  // seconds after anything changes them (LAZY_CONTACTS_WRITE_DELAY in MyMesh),
  // so a timer of ours only added a second full-file write on top of that - and
  // hasPendingWork() is also true for queued outbound packets, so it fired far
  // more often than intended. Each write is the whole table, about a second at
  // 1000+ contacts, with the screen frozen for it.
  if (s_uiDirtyAt && millis() - s_uiDirtyAt > 2000) { s_uiDirtyAt = 0; ui_settings.save(); }
  if (g_shotAt && (int32_t)(millis() - g_shotAt) >= 0) { g_shotAt = 0; takeScreenshot(); }
  usbCommands();

  lap(6);
  autoAdvertTick(); sdBackupTick(); inwStoreTick(); field::tick();
  // Background flash writes wait while someone is using the pager (they stall
  // both cores for a moment each).
  inwSetUserBusy(!dimmer.asleep() && dimmer.idleFor() < 4000);
  lap(7);
  const uint32_t total = millis() - tLoop;
  if (total > 150) {
    logs.add(LOG_WARN, "slow %lu: in%u gps%u wf%u msh%u tk%u drw%u x%u bk%u",
             (unsigned long)total, laps[0], laps[1], laps[2], laps[3], laps[4], laps[5], laps[6], laps[7]);
    extern char g_screenTitle[32];
    if (laps[5] > 150) Serial.printf("[W] draw slow: %ums on '%s'%s\n", laps[5], g_screenTitle, dimmer.asleep() ? " (screen off)" : "");
  }
  // Screen off and nothing playing: poll gently. It used to spin every 2 ms with
  // the screen dark. The radio holds a received packet until it's read, so 30 ms
  // loses nothing and the CPU idles in between. A connected phone keeps the fast
  // pace so syncing stays quick.
  delay(dimmer.asleep() && !jingle.playing() && !bleConnected() ? 30 : 2);
}
