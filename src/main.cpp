// Boot, the main loop, and turning mesh events into alerts.

#include <Arduino.h>
#include <Wire.h>
#include <SPIFFS.h>
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
static uint32_t s_prefsDirtyAt = 0, s_uiDirtyAt = 0;
static uint32_t s_kbFlashUntil = 0;
static bool s_radioOk = false;

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
  if (!battery.present()) return "--";
  snprintf(b, sizeof(b), "%u%%", battery.percent());
  return b;
}
uint8_t app::batteryPct() { return battery.present() ? battery.percent() : 0; }
bool app::charging() { return battery.present() && battery.charging(); }
uint16_t app::batteryMv() { return battery.present() ? battery.millivolts() : 0; }
bool app::radioOk() { return s_radioOk; }

uint16_t app::unread() {
  static uint32_t gen = 0;
  static uint16_t cached = 0;
  if (gen != history.gen) { gen = history.gen; cached = history.totalUnread(); }
  return cached;
}

void app::applyDisplay() {
  dimmer.setFull(ui_settings.brightness);
  dimmer.setTimes(ui_settings.dimSecs * 1000UL, ui_settings.sleepSecs * 1000UL);
}
void app::applySound() { jingle.setVolume(ui_settings.sound ? ui_settings.volume : 0); }
void app::applyHaptics() { haptic.setMode(ui_settings.vibeMode); }
void app::reboot() { if (g_node) g_node->savePrefsNow(); ui_settings.save(); delay(200); ESP.restart(); }
void app::lock() { if (!nav.top() || !nav.top()->isLock()) nav.push(makeLockView()); }

void app::testNotify() {
  haptic.buzz();
  if (ui_settings.sound) jingle.play(&jingles::DM);
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

void gpsPower(bool on) {
  if (on) { expander.enableRail(EXP_GPS_EN, 20); gps.begin(); }
  else expander.digitalWrite(EXP_GPS_EN, LOW);
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

static void alert(const char* title, const char* text, const Jingle* sound) {
  if (ui_settings.wakeOnMessage) dimmer.wake();
  nav.banner(title, text);
  if (quietHours()) return;
  if (ui_settings.vibrate) haptic.buzz();
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
      if (room ? !ui_settings.notifyRoom : !ui_settings.notifyDM) return;
      char text[200];
      if (room) snprintf(text, sizeof(text), "%s: %s", m->sender, m->text);
      else strlcpy(text, m->text, sizeof(text));
      alert(c->name, text, room ? &jingles::MSG : &jingles::DM);
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
      if (!ui_settings.notifyChannel && !mention) return;
      if (ui_settings.channelMentionsOnly && !mention) return;
      char title[48], text[200];
      snprintf(title, sizeof(title), "%s%s", ch.name, mention ? "  @you" : "");
      snprintf(text, sizeof(text), "%s: %s", m->sender, m->text);
      alert(title, text, mention ? &jingles::ALERT : &jingles::MSG);
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

// ---- boot screen ---------------------------------------------------------------------------------
// The INW mark and wordmark, with a thin progress bar underneath. Steps that fail
// are listed below the bar; everything else only goes to the serial log.
constexpr int BOOT_STEPS = 12;
static int s_bootStep = 0, s_bootErrY = 196;

static void drawBootLogo() {
  display.fillScreen(theme.bg);
  // A small mesh: five nodes, each linked to its neighbours.
  static const int16_t NX[] = {88, 128, 156, 112, 70}, NY[] = {58, 44, 90, 126, 108};
  static const uint8_t LINKS[][2] = {{0,1},{1,2},{2,3},{3,4},{4,0},{0,2},{1,3}};
  for (auto& l : LINKS) display.drawLine(NX[l[0]], NY[l[0]], NX[l[1]], NY[l[1]], theme.greenDim);
  for (int i = 0; i < 5; i++) {
    display.fillCircle(NX[i], NY[i], i == 2 ? 9 : 6, theme.green);
    display.drawCircle(NX[i], NY[i], i == 2 ? 13 : 9, theme.greenDim);
  }
  display.setFont(&fonts::FreeSansBold24pt7b);
  display.setTextSize(2);
  display.setTextColor(theme.greenDim);
  display.drawString("INW", 195, 30);            // offset copy underneath reads as glow
  display.setTextColor(theme.green);
  display.drawString("INW", 192, 27);
  display.setTextSize(1);
  display.setFont(&fonts::FreeSans12pt7b);
  display.setTextColor(theme.txt);
  display.drawString("M E S H", 196, 128);
  display.setFont(&fonts::Font2);
  display.setTextColor(theme.dim);
  display.drawString("inland northwest  //  " FW_VERSION, 196, 158);
  display.drawRect(90, 184, 300, 5, theme.line);
}

static void bootStep(const char* what, bool ok, const char* detail = nullptr) {
  s_bootStep = min(s_bootStep + 1, BOOT_STEPS);
  display.fillRect(91, 185, 298 * s_bootStep / BOOT_STEPS, 3, theme.green);
  Serial.printf("[boot] %-18s %s %s\n", what, ok ? "ok" : "FAILED", detail ? detail : "");
  if (ok || s_bootErrY > 210) return;
  display.setFont(&fonts::Font2);
  display.setTextColor(theme.amber, theme.bg);
  char line[64];
  snprintf(line, sizeof(line), "%s: not responding", what);
  display.drawString(line, (L::W - display.textWidth(line)) / 2, s_bootErrY);
  s_bootErrY += 14;
}

static void bootNote(const char* msg) {      // a long step the user should know about
  display.setFont(&fonts::Font2);
  display.fillRect(0, 194, L::W, 28, theme.bg);
  display.setTextColor(theme.amber, theme.bg);
  display.drawString(msg, (L::W - display.textWidth(msg)) / 2, 198);
}

void setup() {
  Serial.begin(115200);
  delay(150);
  Serial.println("\n[INW] boot " FW_VERSION);
  logs.add(LOG_INFO, "boot %s", FW_VERSION);
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
    if (!ui_settings.gpsOn) expander.digitalWrite(EXP_GPS_EN, LOW);
  }

  display.init();
  display.setRotation(TFT_ROTATION);
  theme.init(display);
  if (digitalRead(PIN_BUTTON) == LOW) {        // BOOT held: hardware self test
    backlight.begin(PIN_TFT_BL);
    backlight.setLevel(12);
    bringup::scanToSerial(Wire);
    bringup::report(display, Wire);
    while (digitalRead(PIN_BUTTON) == LOW) delay(10);
    delay(3000);
  }
  drawBootLogo();
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
    display.fillRect(0, 194, L::W, 28, theme.bg);
  }
  bootStep("storage", fsOk);
  const bool sdOk = sdMount();
  bootStep("sd card", true, sdOk ? "mounted" : "none");   // no card is normal
  char report[96];
  importBeforeNode(report, sizeof(report));
  bootStep("restore", true, report);
  bootStep("messages", history.begin());

  board.battReader = [] { return battery.millivolts(); };
  s_radioOk = nodeBegin();
  char rinfo[48] = "";
  if (s_radioOk) snprintf(rinfo, sizeof(rinfo), "%.3f MHz sf%u", g_node->prefs().freq, g_node->prefs().sf);
  bootStep("radio", s_radioOk, rinfo);
  logs.add(s_radioOk ? LOG_INFO : LOG_ERROR, s_radioOk ? "radio up" : "radio init failed");
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
  if (ui_settings.bootJingle && ui_settings.sound) jingle.play(&jingles::BOOT);

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
  if (!ui_settings.gpsOn || !gps.update()) return;
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
  const bool btnPress = btn && !btnWas;
  btnWas = btn;

  if (detents || press || anyKey || btnPress) {
    const bool wasAsleep = dimmer.asleep();
    dimmer.note();
    if (wasAsleep) {
      // The first touch only wakes the screen.
      nav.invalidate();
    } else if (btnPress) {
      app::lock();
    } else {
      if (detents && ui_settings.scrollTick) haptic.tick();
      else if ((anyKey || press) && ui_settings.keyHaptics) haptic.tick();
      if (detents) nav.rotate(detents);
      if (press) nav.press();
      for (uint8_t i = 0; i < nchars; i++) {
        View* v = nav.top();
        if (chars[i] == '\n' && v && !v->wantsAllKeys() && !v->isHome()) nav.press();
        else nav.key(chars[i]);
      }
      if (backspace) nav.backspace();
    }
  }

  // Screen went dark: next wake lands on the lock face.
  static bool wasAsleep = false;
  if (dimmer.asleep() && !wasAsleep && ui_settings.lockOnSleep) app::lock();
  wasAsleep = dimmer.asleep();

  gpsTick();
  wifi::tick();
  { static bool w = false; if (wifi::connected() != w) { w = wifi::connected(); nav.statusChanged(); } }
  battery.tick(millis());
  nodeLoop();
  nav.tick();
  if (!dimmer.asleep() || nav.overlayActive()) nav.draw();
  dimmer.tick();
  jingle.tick();

  // Keyboard light follows the screen (or flashes for a message).
  {
    static uint8_t prev = 1;
    uint8_t kb = dimmer.asleep() ? 0 : dimmer.dimmed() ? ui_settings.kbBacklight / 6 : ui_settings.kbBacklight;
    if (s_kbFlashUntil) { if ((int32_t)(millis() - s_kbFlashUntil) < 0) kb = 255; else s_kbFlashUntil = 0; }
    if (kb != prev) { keyboard.setBacklight(kb); prev = kb; }
  }

  // Phone link changes show in the status bar.
  static bool bleWas = false;
  if (bleConnected() != bleWas) { bleWas = bleConnected(); nav.statusChanged(); if (bleWas) nav.toast("phone connected"); }

  if (s_prefsDirtyAt && millis() - s_prefsDirtyAt > 3000) { s_prefsDirtyAt = 0; if (g_node) g_node->savePrefsNow(); }
  if (s_uiDirtyAt && millis() - s_uiDirtyAt > 2000) { s_uiDirtyAt = 0; ui_settings.save(); }
  if (g_shotAt && (int32_t)(millis() - g_shotAt) >= 0) { g_shotAt = 0; takeScreenshot(); }

  autoAdvertTick();
  sdBackupTick();
  localBackupTick();
  delay(2);
}
