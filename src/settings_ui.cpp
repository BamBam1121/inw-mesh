// Settings grid and its menus. Mesh values are saved a few seconds after the
// last change so spinning a value doesn't wear the flash.

#include "app.h"
#include "node.h"
#include "history.h"
#include "dataio.h"
#include "haptic.h"
#include "gps.h"
#include "backlight.h"
#include "netwifi.h"
#include "power.h"
#include "battery.h"
#include "notify.h"
#include "ota.h"
#include "logstore.h"
#include <SPIFFS.h>
#include <SD.h>

extern void markPrefsDirty();
extern void markUiDirty();
extern void gpsPower(bool on);
extern void deviceInfoPage();
extern void logsPage();
extern LogStore logs;
extern void joinChannelFlow();

static NodePrefs& P() { return g_node->prefs(); }

// Radios stay off while battery saver is on; say why instead of silently ignoring.
static bool saverBlocks() {
  if (!power::saver()) return false;
  nav.toast("battery saver is on - turn it off in Battery first", 3000);
  return true;
}

// ---- profile ------------------------------------------------------------------------------
static void profileMenu() {
  auto* m = new MenuView("Profile");
  m->rebuild = [](MenuView& v) {
    v.value("name", []() -> String { return String(P().node_name); }, [] {
      prompt("Node name", "how you appear on the mesh", P().node_name, 31, [](const String& s) {
        if (!s.length()) return;
        strlcpy(P().node_name, s.c_str(), sizeof(P().node_name));
        g_node->savePrefsNow();
        nav.toast("saved - advert to tell the mesh");
      });
    });
    v.info("public key", []() -> String { char h[20]; mesh::Utils::toHex(h, g_node->self_id.pub_key, 8); return String(h) + "..."; });
    v.header("position");
    v.info("advert position", []() -> String {
      if (!P().node_lat && !P().node_lon) return String("not set");
      return String(P().node_lat, 5) + ", " + String(P().node_lon, 5); });
    v.toggle("share position in adverts", [] { return P().advert_loc_policy != ADVERT_LOC_NONE; },
             [] { P().advert_loc_policy = P().advert_loc_policy ? ADVERT_LOC_NONE : ADVERT_LOC_SHARE; markPrefsDirty(); });
    v.toggle("follow gps live", [] { return ui_settings.gpsLivePosition; },
             [] { ui_settings.gpsLivePosition = !ui_settings.gpsLivePosition; markUiDirty(); });
    v.action("set from gps now", [] {
      if (!gps.hasFix()) { nav.toast("no gps fix yet"); return; }
      P().node_lat = gps.fix().lat; P().node_lon = gps.fix().lon;
      sensors.node_lat = P().node_lat; sensors.node_lon = P().node_lon;
      g_node->savePrefsNow(); nav.toast("position set");
    });
    v.action("enter position", [] {
      prompt("Position", "lat,lon  e.g. 47.6588,-117.4260", "", 30, [](const String& s) {
        const int c = s.indexOf(',');
        if (c < 0) { nav.toast("use lat,lon"); return; }
        const double la = s.substring(0, c).toDouble(), lo = s.substring(c + 1).toDouble();
        if (fabs(la) > 90 || fabs(lo) > 180 || (la == 0 && lo == 0)) { nav.toast("not a valid position"); return; }
        ui_settings.gpsLivePosition = false; markUiDirty();
        P().node_lat = la; P().node_lon = lo; sensors.node_lat = la; sensors.node_lon = lo;
        g_node->savePrefsNow(); nav.toast("position set (gps follow off)");
      });
    });
    v.action("clear position", [] {
      P().node_lat = 0; P().node_lon = 0; sensors.node_lat = 0; sensors.node_lon = 0;
      ui_settings.gpsLivePosition = false; markUiDirty();
      g_node->savePrefsNow(); nav.toast("cleared");
    });
    v.header("announce");
    v.action("advert nearby now", [] { nav.toast(g_node->advertZeroHop() ? "sent" : "failed"); });
    v.action("advert whole mesh now", [] { nav.toast(g_node->advertFlood() ? "sent" : "failed"); });
    v.adjust("auto advert (flood) every", []() -> String {
      return ui_settings.autoAdvertHours ? String(ui_settings.autoAdvertHours) + " h" : String("off"); },
      [](int d) { ui_settings.autoAdvertHours = constrain(ui_settings.autoAdvertHours + d, 0, 48); markUiDirty(); });
  };
  m->rebuild(*m);
  nav.push(m);
}

// ---- radio & mesh -----------------------------------------------------------------------------
static const float BWS[] = {7.8f, 10.4f, 15.6f, 20.8f, 31.25f, 41.7f, 62.5f, 125.0f, 250.0f, 500.0f};

static void radioChanged() { g_node->applyRadio(); markPrefsDirty(); }

static void radioMenu() {
  auto* m = new MenuView("Radio & Mesh");
  m->rebuild = [](MenuView& v) {
    v.action("INW mesh preset  910.525 / 62.5 / SF7 / CR5", [] {
      P().freq = 910.525f; P().bw = 62.5f; P().sf = 7; P().cr = 5;
      radioChanged(); nav.toast("radio set for the inw mesh");
    });
    v.adjust("frequency", []() -> String { return String(P().freq, 4) + " MHz"; },
             [](int d) { P().freq = constrain(P().freq + d * 0.0125f, 150.0f, 960.0f); radioChanged(); });
    v.action("type a frequency", [] {
      prompt("Frequency", "MHz, e.g. 910.525", String(P().freq, 4), 10, [](const String& s) {
        const float f = s.toFloat();
        if (f < 150 || f > 960) { nav.toast("out of range"); return; }
        P().freq = f; radioChanged(); nav.toast("frequency set");
      });
    });
    v.adjust("bandwidth", []() -> String { return String(P().bw, 2) + " kHz"; }, [](int d) {
      int i = 0;
      for (int k = 0; k < 10; k++) if (BWS[k] <= P().bw + 0.01f) i = k;
      P().bw = BWS[constrain(i + d, 0, 9)]; radioChanged();
    });
    v.adjust("spreading factor", []() -> String { return String(P().sf); }, [](int d) { P().sf = constrain(P().sf + d, 5, 12); radioChanged(); });
    v.adjust("coding rate", []() -> String { return "4/" + String(P().cr); }, [](int d) { P().cr = constrain(P().cr + d, 5, 8); radioChanged(); });
    v.adjust("tx power", []() -> String { return String(P().tx_power_dbm) + " dBm"; },
             [](int d) { P().tx_power_dbm = constrain(P().tx_power_dbm + d, -9, 22); radioChanged(); });
    v.toggle("rx boosted gain", [] { return P().rx_boosted_gain != 0; },
             [] { P().rx_boosted_gain = !P().rx_boosted_gain; radioChanged(); });
    v.header("mesh");
    v.toggle("client repeat (forward packets)", [] { return P().isRepeatEn(); }, [] {
      P().setRepeatEn(!P().isRepeatEn()); markPrefsDirty();
      nav.toast(P().isRepeatEn() ? "repeating: this node now relays traffic" : "repeat off");
    });
    v.adjust("path hash size", []() -> String { return String(P().path_hash_mode + 1) + " byte"; },
             [](int d) { P().path_hash_mode = constrain(P().path_hash_mode + d, 0, 2); markPrefsDirty(); });
    v.adjust("extra acks", []() -> String { return String(P().multi_acks); },
             [](int d) { P().multi_acks = constrain(P().multi_acks + d, 0, 2); markPrefsDirty(); });
    v.adjust("airtime factor", []() -> String { return String(P().airtime_factor, 1); },
             [](int d) { P().airtime_factor = constrain(P().airtime_factor + d * 0.5f, 0.0f, 9.0f); markPrefsDirty(); });
    v.adjust("rx delay base", []() -> String { return String(P().rx_delay_base, 1); },
             [](int d) { P().rx_delay_base = constrain(P().rx_delay_base + d * 0.5f, 0.0f, 20.0f); markPrefsDirty(); });
  };
  m->rebuild(*m);
  nav.push(m);
}

// ---- channels ------------------------------------------------------------------------------------
static void channelMenu(int idx) {
  ChannelDetails ch;
  if (!g_node->getChannel(idx, ch) || !ch.name[0]) return;
  auto* m = new MenuView(ch.name);
  uint8_t secret[16];
  memcpy(secret, ch.channel.secret, 16);
  m->action("open chat", [idx] { app::openThreadForChannel(idx); });
  m->value("notifications", [secret]() -> String { return notifyModeName(notifyMode(ConvKey::channel(secret))); }, [secret] {
    const ConvKey k = ConvKey::channel(secret);
    setNotifyMode(k, (notifyMode(k) + 1) % NM_COUNT);      // press to cycle
  });
  m->info("key", [secret]() -> String { char h[40]; mesh::Utils::toHex(h, secret, 16); return String(h); });
  m->info("messages", [secret]() -> String { return String(history.count(ConvKey::channel(secret))); });
  m->action("mark all read", [secret] { history.markRead(ConvKey::channel(secret)); nav.toast("done"); });
  m->action("clear history", [secret] {
    confirm("Clear channel history?", "removes these messages from this device",
            [secret] { history.clearConv(ConvKey::channel(secret)); nav.toast("cleared"); });
  });
  m->action("leave channel", [idx] {
    confirm("Leave channel?", "you can re-join it with the same name or key",
            [idx] { g_node->removeChannel(idx); nav.pop(); nav.toast("left"); });
  });
  nav.push(m);
}

static void channelsMenu() {
  auto* m = new MenuView("Channels");
  m->rebuild = [](MenuView& v) {
    int n = 0;
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
      ChannelDetails ch;
      if (!g_node->getChannel(i, ch) || !ch.name[0]) continue;
      n++;
      const uint8_t* s = ch.channel.secret;
      ConvKey k = ConvKey::channel(s);
      v.submenu(ch.name, [i] { channelMenu(i); }, [k]() -> String { const uint16_t u = history.unread(k); return u ? String(u) + " new" : String(""); });
    }
    v.header(String(n) + " of " + String(MAX_GROUP_CHANNELS) + " slots used");
    v.action("+ join #hashtag channel", [] { joinChannelFlow(); });
  };
  m->rebuild(*m);
  nav.push(m);
}

// ---- auto add ------------------------------------------------------------------------------------
#ifndef AUTO_ADD_OVERWRITE_OLDEST
#define AUTO_ADD_OVERWRITE_OLDEST (1 << 0)
#define AUTO_ADD_CHAT             (1 << 1)
#define AUTO_ADD_REPEATER         (1 << 2)
#define AUTO_ADD_ROOM_SERVER      (1 << 3)
#define AUTO_ADD_SENSOR           (1 << 4)
#endif

// Forget contacts not heard for a while. Each choice shows how many it would
// remove before anything happens; favourites and contacts with no heard time are
// always kept; the SD mirror is refreshed first so the list can be recovered
// (Backups -> recover missing contacts).
static void tidyContactsMenu() {
  auto* m = new MenuView("Tidy old contacts");
  m->rebuild = [](MenuView& v) {
    if (!app::timeValid()) { v.info("clock not set yet", []() -> String { return String("needs the time to judge 'old'"); }); return; }
    v.info("contacts", []() -> String { return String(g_node->getNumContacts()); });
    v.header("not heard in...");
    static const uint16_t DAYS[] = {30, 90, 180, 365};
    for (uint16_t d : DAYS) {
      const int n = g_node->countStale(d);
      const String label = String(d) + " days: " + String(n) + (n == 1 ? " contact" : " contacts");
      v.action(label, [d, n] {
        if (n <= 0) { nav.toast("nothing that old"); return; }
        confirm("Forget " + String(n) + " contacts?", "not heard in " + String(d) + " days. favourites are kept. backed up to sd first.",
                [d] {
                  nav.busy("backing up, then tidying...");
                  sdBackupNow();
                  const int gone = g_node->forgetStale(d);
                  logs.add(LOG_INFO, "tidied %d contacts not heard in %u days", gone, d);
                  nav.toast((String("forgot ") + gone + " contacts").c_str(), 3000);
                  nav.pop();
                });
      });
    }
    v.info("kept always", []() -> String { return String("favourites, and contacts never heard by time"); });
  };
  m->rebuild(*m);
  nav.push(m);
}

static void autoAddMenu() {
  auto* m = new MenuView("Contacts & auto-add");
  auto addBit = [](MenuView& v, const char* label, uint8_t b) {
    v.toggle(label, [b] { return (P().autoadd_config & b) != 0; }, [b] { P().autoadd_config ^= b; markPrefsDirty(); });
  };
  m->rebuild = [addBit](MenuView& v) {
    v.toggle("add everyone automatically", [] { return (P().manual_add_contacts & 1) == 0; },
             [] { P().manual_add_contacts ^= 1; markPrefsDirty(); });
    if (P().manual_add_contacts & 1) {
      v.header("only add these automatically");
      addBit(v, "chat users", AUTO_ADD_CHAT);
      addBit(v, "repeaters", AUTO_ADD_REPEATER);
      addBit(v, "room servers", AUTO_ADD_ROOM_SERVER);
      addBit(v, "sensors", AUTO_ADD_SENSOR);
    }
    addBit(v, "overwrite oldest when full", AUTO_ADD_OVERWRITE_OLDEST);
    v.adjust("max hops away", []() -> String { return P().autoadd_max_hops ? String(P().autoadd_max_hops - 1) : String("any"); },
             [](int d) { P().autoadd_max_hops = constrain(P().autoadd_max_hops + d, 0, 64); markPrefsDirty(); });
    v.info("contacts", []() -> String { return String(g_node->getNumContacts()) + " / " + String(MAX_CONTACTS); });
    v.action("import contacts from sd export", [] { nav.busy("importing, one moment..."); nav.toast(importJsonNow(), 4000); });
    v.action("tidy old contacts", [] { tidyContactsMenu(); });
  };
  m->rebuild(*m);
  nav.push(m);
}

// ---- bluetooth --------------------------------------------------------------------------------------
static void bluetoothMenu() {
  auto* m = new MenuView("Bluetooth (phone app)");
  m->toggle("bluetooth", [] { return bleEnabled(); }, [] {
    if (saverBlocks()) return;
    ui_settings.ble = !bleEnabled();
    bleSetEnabled(ui_settings.ble);
    markUiDirty();
  });
  m->info("status", []() -> String { return String(bleConnected() ? "phone connected" : bleEnabled() ? "waiting for phone" : "off"); });
  m->info("pairing pin", []() -> String { char b[10]; snprintf(b, sizeof(b), "%06lu", (unsigned long)blePin()); return String(b); });
  m->info("device name", []() -> String { return String(BLE_NAME_PREFIX) + P().node_name; });
  m->action("set pairing pin", [] {
    prompt("Pairing PIN", "6 digits (applies after reboot)", "", 6, [](const String& s) {
      const long v = s.toInt();
      if (s.length() != 6 || v < 100000) { nav.toast("use 6 digits, not starting with 0"); return; }
      P().ble_pin = (uint32_t)v; g_node->savePrefsNow(); nav.toast("saved - reboot to apply");
    });
  });
  nav.push(m);
}


// ---- wi-fi -------------------------------------------------------------------------------------
class WifiScanView : public View {
public:
  WifiScanView() { wifi::startScan(); }
  void tick() override { if (millis() - _last > 500) { _last = millis(); dirty = true; } }
  void rotate(int d) override { const int n = wifi::scanCount(); if (n) { _f = ((_f + d) % n + n) % n; dirty = true; } }
  void key(char c) override { if (c == 'r') { wifi::startScan(); _f = 0; } else if (c == '\n') press(); }
  void press() override {
    if (!wifi::scanDone() || !wifi::scanCount()) return;
    const String ss = wifi::scanSsid(_f);
    if (!ss.length()) return;
    if (wifi::scanOpen(_f)) { wifi::save(ss.c_str(), ""); nav.pop(); nav.toast("joining open network"); return; }
    prompt("Password", ss, "", 63, [ss](const String& pw) {
      wifi::save(ss.c_str(), pw.c_str());
      nav.pop();                          // back out of the scan list
      nav.toast("saved - connecting");
    }, true);
  }
  void draw(Canvas& g) override {
    const Theme& t = nav.theme();
    drawHeader(g, "Wi-Fi networks", wifi::scanDone() ? "r = rescan" : "scanning...");
    const int n = wifi::scanCount();
    if (!wifi::scanDone()) { g.setTextColor(t.dim, t.bg); g.drawString("looking for networks...", 14, L::BODY_Y + 10); return; }
    if (!n) { g.setTextColor(t.dim, t.bg); g.drawString("nothing found. r to rescan", 14, L::BODY_Y + 10); return; }
    const int visible = 8;
    if (_f < _top) _top = _f;
    if (_f >= _top + visible) _top = _f - visible + 1;
    for (int i = _top; i < n && i < _top + visible; i++) {
      const int y = L::BODY_Y + (i - _top) * L::ROW_H;
      const bool on = i == _f;
      const uint16_t bg = on ? t.focus : t.bg;
      if (on) { g.fillRect(0, y, L::W, L::ROW_H, bg); g.fillRect(0, y, 3, L::ROW_H, t.green); }
      char nm[40];
      sanitize(wifi::scanSsid(i), nm, sizeof(nm));
      g.setTextColor(on ? t.green : t.white, bg);
      g.drawString(nm[0] ? nm : "(hidden)", 12, y + 2);
      char r[24];
      snprintf(r, sizeof(r), "%s %d dBm", wifi::scanOpen(i) ? "open" : "", wifi::scanRssi(i));
      g.setTextColor(t.dim, bg);
      g.drawString(r, L::W - 12 - g.textWidth(r), y + 2);
    }
    drawScrollbar(g, n, _top, visible, L::BODY_Y, L::H - L::BODY_Y);
  }
private:
  int _f = 0, _top = 0;
  uint32_t _last = 0;
};

static void wifiMenu() {
  auto* m = new MenuView("Wi-Fi");
  m->rebuild = [](MenuView& v) {
    v.toggle("wi-fi", [] { return wifi::enabled(); }, [] {
      if (saverBlocks()) return;
      ui_settings.wifiOn = !wifi::enabled(); wifi::setEnabled(ui_settings.wifiOn); markUiDirty(); nav.statusChanged(); });
    v.info("status", []() -> String { return String(wifi::statusText()); });
    v.action("scan + join a network", [] { nav.push(new WifiScanView()); });
    if (wifi::savedCount()) v.header("saved networks");
    for (uint8_t i = 0; i < wifi::savedCount(); i++) {
      const String ss = wifi::savedSsid(i);
      v.action(ss, [i, ss] { confirm("Forget " + ss + "?", "", [i] { wifi::forget(i); nav.toast("forgotten"); }); });
    }
    v.header("map tiles");
    v.toggle("download tiles while viewing map", [] { return ui_settings.tileFetch; },
             [] { ui_settings.tileFetch = !ui_settings.tileFetch; markUiDirty(); });
    v.value("tile source", []() -> String {
      static const char* S[] = {"openstreetmap", "wadamesh proxy", "custom url"};
      return String(S[ui_settings.tileSource % 3]); },
      [] { ui_settings.tileSource = (ui_settings.tileSource + 1) % 3; markUiDirty(); });
    v.action("set custom tile url", [] {
      prompt("Tile server", "base url; /{z}/{x}/{y}.png is added", ui_settings.tileUrl, 90, [](const String& u) {
        strlcpy(ui_settings.tileUrl, u.c_str(), sizeof(ui_settings.tileUrl));
        ui_settings.tileSource = 2; markUiDirty();
      });
    });
    v.info("tiles this session", []() -> String {
      return String(wifi::tilesFetched()) + " ok, " + String(wifi::tilesFailed()) + " failed (http " + String(wifi::lastHttpCode()) + ")"; });
    v.header("time");
    v.toggle("set clock from internet", [] { return ui_settings.ntpSync; },
             [] { ui_settings.ntpSync = !ui_settings.ntpSync; markUiDirty(); });
  };
  m->rebuild(*m);
  nav.push(m);
}

// ---- gps / clock ----------------------------------------------------------------------------------------
static void gpsMenu() {
  auto* m = new MenuView("GPS");
  m->toggle("gps receiver", [] { return ui_settings.gpsOn && !power::saver(); },
            [] { if (saverBlocks()) return; ui_settings.gpsOn = !ui_settings.gpsOn; gpsPower(ui_settings.gpsOn); markUiDirty(); });
  m->toggle("advert position follows gps", [] { return ui_settings.gpsLivePosition; },
            [] { ui_settings.gpsLivePosition = !ui_settings.gpsLivePosition; markUiDirty(); });
  m->toggle("set clock from gps", [] { return ui_settings.gpsSetsClock; },
            [] { ui_settings.gpsSetsClock = !ui_settings.gpsSetsClock; markUiDirty(); });
  m->info("fix", []() -> String { return gps.hasFix() ? String(gps.fix().satellites) + " sats" : String("searching"); });
  m->info("position", []() -> String { return gps.hasFix() ? String(gps.fix().lat, 5) + ", " + String(gps.fix().lon, 5) : String("-"); });
  nav.push(m);
}

static void clockMenu() {
  auto* m = new MenuView("Clock & time");
  m->info("now", []() -> String { return app::timeValid() ? String(clockText(app::now(), true)) : String("not set"); });
  m->adjust("time zone", []() -> String {
    const int t = ui_settings.tzMinutes;
    char b[16]; snprintf(b, sizeof(b), "UTC%c%d:%02d", t < 0 ? '-' : '+', abs(t) / 60, abs(t) % 60); return String(b); },
    [](int d) { ui_settings.tzMinutes = constrain(ui_settings.tzMinutes + d * 30, -720, 840); markUiDirty(); nav.statusChanged(); });
  m->toggle("24-hour clock", [] { return ui_settings.clock24; },
            [] { ui_settings.clock24 = !ui_settings.clock24; markUiDirty(); nav.statusChanged(); });
  m->toggle("set from gps", [] { return ui_settings.gpsSetsClock; },
            [] { ui_settings.gpsSetsClock = !ui_settings.gpsSetsClock; markUiDirty(); });
  m->action("set time by hand", [] {
    prompt("Set time", "local: YYYY-MM-DD HH:MM", "", 16, [](const String& s) {
      struct tm tm = {};
      if (sscanf(s.c_str(), "%d-%d-%d %d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min) != 5) {
        nav.toast("use YYYY-MM-DD HH:MM"); return;
      }
      tm.tm_year -= 1900; tm.tm_mon -= 1;
      // mktime in UTC (TZ is unset on this device), then undo the local offset.
      const time_t local = mktime(&tm);
      if (local < 1700000000) { nav.toast("that date looks wrong"); return; }
      app::setTime((uint32_t)(local - ui_settings.tzMinutes * 60));
      nav.toast("clock set");
    });
  });
  nav.push(m);
}

// ---- display / sound ---------------------------------------------------------------------------------
static void displayMenu() {
  auto* m = new MenuView("Display");
  m->adjust("brightness", []() -> String { return String(ui_settings.brightness) + " / 16"; },
            [](int d) { ui_settings.brightness = constrain(ui_settings.brightness + d, 1, 16); app::applyDisplay(); markUiDirty(); });
  m->adjust("dim after", []() -> String { return String(ui_settings.dimSecs) + " s"; },
            [](int d) { ui_settings.dimSecs = constrain((int)ui_settings.dimSecs + d * 5, 5, 600); app::applyDisplay(); markUiDirty(); });
  m->adjust("screen off after", []() -> String { return String(ui_settings.sleepSecs) + " s"; },
            [](int d) { ui_settings.sleepSecs = constrain((int)ui_settings.sleepSecs + d * 10, 10, 1800); app::applyDisplay(); markUiDirty(); });
  m->adjust("keyboard light", []() -> String { return String(ui_settings.kbBacklight * 100 / 255) + "%"; },
            [](int d) { ui_settings.kbBacklight = constrain((int)ui_settings.kbBacklight + d * 25, 0, 255); app::applyDisplay(); markUiDirty(); });
  m->toggle("lock face after screen off", [] { return ui_settings.lockOnSleep; },
            [] { ui_settings.lockOnSleep = !ui_settings.lockOnSleep; markUiDirty(); });
  m->toggle("unlock with wheel press only", [] { return ui_settings.wheelUnlock; },
            [] { ui_settings.wheelUnlock = !ui_settings.wheelUnlock; markUiDirty(); });
  m->toggle("wake screen on message", [] { return ui_settings.wakeOnMessage; },
            [] { ui_settings.wakeOnMessage = !ui_settings.wakeOnMessage; markUiDirty(); });
  nav.push(m);
}

static void previewSound(const Jingle* j) {
  if (!ui_settings.sound) { nav.toast("sounds are off (Sound settings)"); return; }
  jingle.play(j);
}

static void themeMenu() {
  auto* m = new MenuView("Theme");
  m->rebuild = [](MenuView& v) {
    for (uint8_t i = 0; i < THEME_COUNT; i++) {
      v.value(THEMES[i].name, [i]() -> String { return ui_settings.themeId == i ? String("active") : String(""); }, [i] {
        ui_settings.themeId = i;
        app::applyTheme();
        markUiDirty();
        if (ui_settings.sound) jingle.play(THEMES[i].msg);
        if (ui_settings.vibrate) haptic.pattern(THEMES[i].vibeMsg.seq, THEMES[i].vibeMsg.n);
        nav.toast(THEMES[i].blurb, 3000);
      });
    }
    v.header("preview this theme");
    v.action("boot sound", [] { previewSound(app::themeSpec().boot); });
    v.action("message", [] { previewSound(app::themeSpec().msg);
      haptic.pattern(app::themeSpec().vibeMsg.seq, app::themeSpec().vibeMsg.n); });
    v.action("direct message", [] { previewSound(app::themeSpec().dm);
      haptic.pattern(app::themeSpec().vibeDm.seq, app::themeSpec().vibeDm.n); });
    v.action("@mention", [] { previewSound(app::themeSpec().mention);
      haptic.pattern(app::themeSpec().vibeMention.seq, app::themeSpec().vibeMention.n); });
    v.action("see the lock screen", [] { app::lock(); });
  };
  m->rebuild(*m);
  nav.push(m);
}

static void soundMenu() {
  auto* m = new MenuView("Sound & vibration");
  m->header("vibration");
  m->toggle("vibrate on messages", [] { return ui_settings.vibrate; },
            [] { ui_settings.vibrate = !ui_settings.vibrate; markUiDirty(); });
  m->adjust("strength", []() -> String { return String(Haptic::modeName(ui_settings.vibeMode)); }, [](int d) {
    ui_settings.vibeMode = constrain(ui_settings.vibeMode + d, 0, Haptic::CONFIG_COUNT - 1);
    app::applyHaptics(); haptic.buzz(); markUiDirty();
  });
  m->toggle("key clicks", [] { return ui_settings.keyHaptics; },
            [] { ui_settings.keyHaptics = !ui_settings.keyHaptics; markUiDirty(); });
  m->toggle("scroll tick", [] { return ui_settings.scrollTick; },
            [] { ui_settings.scrollTick = !ui_settings.scrollTick; markUiDirty(); });
  m->header("sound");
  m->toggle("sounds", [] { return ui_settings.sound; }, [] { ui_settings.sound = !ui_settings.sound; app::applySound(); markUiDirty(); });
  m->adjust("volume", []() -> String { return String(ui_settings.volume) + "%"; },
            [](int d) { ui_settings.volume = constrain(ui_settings.volume + d * 5, 0, 100); app::applySound(); markUiDirty(); });
  m->toggle("boot jingle", [] { return ui_settings.bootJingle; },
            [] { ui_settings.bootJingle = !ui_settings.bootJingle; markUiDirty(); });
  m->action("test", [] { app::testNotify(); });
  nav.push(m);
}

static void notifyMenu() {
  auto* m = new MenuView("Notifications");
  auto tg = [](MenuView& v, const char* label, bool* f) {
    v.toggle(label, [f] { return *f; }, [f] { *f = !*f; markUiDirty(); nav.statusChanged(); });
  };
  tg(*m, "do not disturb", &ui_settings.dnd);
  tg(*m, "direct messages", &ui_settings.notifyDM);
  tg(*m, "channel messages", &ui_settings.notifyChannel);
  tg(*m, "  only when @mentioned", &ui_settings.channelMentionsOnly);
  tg(*m, "room messages", &ui_settings.notifyRoom);
  m->info("per chat", []() -> String { return String("a channel or contact's menu overrides these"); });
  tg(*m, "new contacts", &ui_settings.notifyNewContact);
  tg(*m, "light keyboard on message", &ui_settings.kbFlash);
  m->header("quiet hours");
  tg(*m, "quiet hours", &ui_settings.dndSchedule);
  m->adjust("from", []() -> String { return String(ui_settings.dndStart) + ":00"; },
            [](int d) { ui_settings.dndStart = (ui_settings.dndStart + d + 24) % 24; markUiDirty(); });
  m->adjust("until", []() -> String { return String(ui_settings.dndEnd) + ":00"; },
            [](int d) { ui_settings.dndEnd = (ui_settings.dndEnd + d + 24) % 24; markUiDirty(); });
  nav.push(m);
}

static void quickRepliesMenu() {
  auto* m = new MenuView("Quick replies");
  m->rebuild = [](MenuView& v) {
    for (uint8_t i = 0; i < UiSettings::QUICK_MAX; i++) {
      v.value(String(i + 1), [i]() -> String { return String(ui_settings.quick[i][0] ? ui_settings.quick[i] : "(empty)"); }, [i] {
        prompt("Quick reply " + String(i + 1), "empty to remove", ui_settings.quick[i], 30, [i](const String& s) {
          strlcpy(ui_settings.quick[i], s.c_str(), sizeof(ui_settings.quick[i]));
          markUiDirty();
        });
      });
    }
  };
  m->rebuild(*m);
  nav.push(m);
}

static void messagesMenu() {
  auto* m = new MenuView("Messages");
  auto tg = [](MenuView& v, const char* label, bool* f) {
    v.toggle(label, [f] { return *f; }, [f] { *f = !*f; markUiDirty(); });
  };
  tg(*m, "retry direct messages", &ui_settings.autoRetry);
  tg(*m, "flood on last retry", &ui_settings.autoResetPath);
  tg(*m, "show hops", &ui_settings.showHops);
  tg(*m, "show signal (snr)", &ui_settings.showSnr);
  tg(*m, "compact (irc style)", &ui_settings.compactChat);
  tg(*m, "ignore 1-character posts", &ui_settings.ignoreOneChar);
  tg(*m, "distances in miles", &ui_settings.miles);
  m->submenu("quick replies", [] { quickRepliesMenu(); });
  m->info("stored", []() -> String { return String(history.size()) + " / " + String(History::CAP); });
  m->action("clear all chat history", [] {
    confirm("Clear ALL chats?", "every stored message on this device is removed", [] { history.clearAll(); nav.toast("cleared"); });
  });
  nav.push(m);
}

static void telemetryMenu() {
  auto* m = new MenuView("Telemetry");
  static const char* MODE[] = {"nobody", "favourites", "everyone"};
  auto row = [](MenuView& v, const char* label, uint8_t* f) {
    v.value(label, [f]() -> String { return String(MODE[*f % 3]); }, [f] { *f = (*f + 1) % 3; markPrefsDirty(); });
  };
  m->info("answer requests from...", []() -> String { return String(""); });
  row(*m, "battery + basics", &P().telemetry_mode_base);
  row(*m, "location", &P().telemetry_mode_loc);
  row(*m, "environment", &P().telemetry_mode_env);
  nav.push(m);
}

static void backupsMenu() {
  auto* m = new MenuView("Backups");
  m->info("sd card", []() -> String { return sdMount() ? String((unsigned long)(sdFreeBytes() / 1048576ULL)) + " MB free" : String("not found"); });
  m->info("last sd backup", []() -> String { return ui_settings.lastSdBackup ? String(timeAgo(ui_settings.lastSdBackup)) + " ago" : String("never"); });
  m->action("back up to sd now", [] { nav.busy("backing up..."); nav.toast(sdBackupNow(), 3500); });
  m->action("export meshcore json (includes key!)", [] {
    confirm("Export includes your private key", "anyone with the file can be you on the mesh. keep it safe.",
            [] { nav.busy("exporting..."); nav.toast(exportJson(), 4000); });
  });
  m->action("import contacts + channels from sd", [] { nav.busy("importing, one moment..."); nav.toast(importJsonNow(), 4000); });
  m->action("recover missing contacts from backups", [] { nav.busy("checking backups..."); nav.toast(recoverMissingContacts(), 4000); });
  m->action("restore contacts from sd mirror", [] {
    confirm("Restore from SD?", "replaces contacts + channels with /inw on the card, then reboots", [] {
      if (!sdMount() || !SD.exists("/inw/contacts3")) { nav.toast("no /inw backup on sd"); return; }
      inwStoreFlush(10000);   // a queued save landing after the removes would undo them
      SPIFFS.remove("/contacts3.bak"); SPIFFS.remove("/channels2.bak");
      SPIFFS.remove("/contacts3"); SPIFFS.remove("/channels2");
      nav.toast("restoring, rebooting");
      delay(800);
      app::rebootDiscard();   // importBeforeNode() pulls the mirror back in on boot
    });
  });
  m->info("auto backup", []() -> String { return String("once a day to /inw on the sd"); });
  nav.push(m);
}

// ---- battery -------------------------------------------------------------------------------------
extern Battery battery;

static void batteryMenu() {
  auto* m = new MenuView("Battery");
  m->info("charge", []() -> String {
    String s = String(app::batteryPct()) + "%  " + String(app::batteryMv()) + " mV";
    if (power::holding()) s += "  held";
    else if (app::charging()) s += "  charging";
    else if (battery.pluggedIn()) s += "  plugged in";
    return s; });
  // "82% health" first: "1244 of 1500 mAh (82%)" reads like a charge level.
  m->info("battery health", []() -> String {
    const uint16_t f = battery.fullChargeMah();
    return f ? String(min(100, f * 100 / Battery::DESIGN_MAH)) + "% health  (holds " + String(f) +
               " of " + String(Battery::DESIGN_MAH) + " mAh when new)" : String("--"); });
  m->action("reset battery learning", [] {
    confirm("Reset the gauge's learning?", "use this if health looks wrong on a new battery. it re-learns over the next full charge and discharge.",
            [] { nav.toast(battery.relearn() ? "reset - charge to full, then run it low" : "the gauge refused", 4000); });
  });
  m->header("optimised charging");
  m->toggle("hold at 80% until needed", [] { return ui_settings.smartCharge; }, [] {
    ui_settings.smartCharge = !ui_settings.smartCharge;
    if (!ui_settings.smartCharge) power::chargeFullNow();
    markUiDirty(); });
  m->info("status", []() -> String { return String(power::chargeStatus()); });
  m->action("charge to 100% now", [] { power::chargeFullNow(); nav.toast("charging to full this time"); });
  m->header("battery saver");
  m->toggle("battery saver", [] { return power::saver(); }, [] { power::setSaver(!power::saver()); });
  m->toggle("turn on automatically", [] { return ui_settings.autoSaver; },
            [] { ui_settings.autoSaver = !ui_settings.autoSaver; markUiDirty(); });
  m->adjust("turn on at", []() -> String { return String(ui_settings.saverPct) + "%"; },
            [](int d) { ui_settings.saverPct = constrain(ui_settings.saverPct + d * 5, 5, 50); markUiDirty(); });
  m->info("what it does", []() -> String { return String("gps, bluetooth, wi-fi off, dim screen"); });
  nav.push(m);
}

static void systemMenu() {
  auto* m = new MenuView("System");
  m->header("updates");
  m->info("version", []() -> String { return String(FW_VERSION); });
  if (ota::supported()) {
    m->action("check for updates", [] {
      nav.busy("checking for updates...");
      const ota::Info info = ota::check();
      if (!info.ok) { nav.toast(info.error, 4000); return; }
      if (!info.newer) { nav.toast((String("up to date (") + FW_VERSION + ")").c_str(), 3000); return; }
      confirm(String("Update to ") + info.version + "?",
              String(info.notes[0] ? info.notes : "a new version is ready") + ". takes about a minute.",
              [info] { nav.toast(ota::install(info), 5000); });
    });
  } else {
    m->info("wi-fi updates", []() -> String { return String("need one usb reinstall first"); });
    m->action("how to enable wi-fi updates", [] {
      sdBackupNow();      // also refreshes the NVS safety copy
      nav.push(new TextPageView("Enable Wi-Fi updates", [](std::vector<String>& out) {
        out.push_back("This pager has the older one-slot layout.");
        out.push_back("One reinstall over USB adds the second slot.");
        out.push_back("");
        out.push_back("# what is kept");
        out.push_back("keys, channels, radio + ui settings: always");
        out.push_back(sdMounted() ? "contacts + messages: yes, backed up to sd just now"
                                  : "contacts + messages: only with an sd card");
        out.push_back("");
        out.push_back("# steps");
        out.push_back("1. plug into a computer, open the website");
        out.push_back("2. choose First install (not Update)");
        out.push_back("3. first boot sets up storage, a few minutes");
      }, 120000));
    });
  }
  m->toggle("check on start (wi-fi)", [] { return ui_settings.autoUpdateCheck; },
            [] { ui_settings.autoUpdateCheck = !ui_settings.autoUpdateCheck; markUiDirty(); });
  m->toggle("beta updates (every build)", [] { return ui_settings.betaUpdates; }, [] {
    ui_settings.betaUpdates = !ui_settings.betaUpdates; markUiDirty();
    nav.toast(ui_settings.betaUpdates ? "beta: you get builds before release" : "stable releases only", 3000);
  });
  m->header("device");
  m->action("usb flash mode (for the web installer)", [] {
    confirm("Enter USB flash mode?", "the screen goes dark until you install from the website or power-cycle.",
            [] { app::rebootToFlashMode(); });
  });
  m->action("device info", [] { deviceInfoPage(); });
  m->action("log", [] { logsPage(); });
  m->action("reboot", [] { confirm("Reboot?", "", [] { app::reboot(); }); });
  m->action("reset screen/sound settings", [] {
    confirm("Reset UI settings?", "mesh identity, contacts and channels are kept", [] {
      const uint8_t done = ui_settings.importDone;
      ui_settings = UiSettings();
      ui_settings.importDone = done;
      ui_settings.save();
      app::applyDisplay(); app::applySound(); app::applyHaptics();
      nav.toast("defaults restored");
    });
  });
  m->action("forget all contacts (keeps key)", [] {
    confirm("Forget ALL contacts?", "backed up to sd first. your identity is kept.", [] {
      sdBackupNow();          // also waits for any queued save to land
      SPIFFS.remove("/contacts3"); SPIFFS.remove("/contacts3.bak");
      nav.toast("rebooting");
      delay(800);
      app::rebootDiscard();   // saving on the way out would write them all back
    });
  });
  nav.push(m);
}

static void aboutPage() {
  nav.push(new TextPageView("About", [](std::vector<String>& out) {
    out.push_back("Squatch Mesh firmware " FW_VERSION);
    out.push_back("# for the LilyGo T-Lora Pager, made in the Inland Northwest");
    out.push_back("mesh engine   MeshCore " FIRMWARE_VERSION " (companion)");
    out.push_back("graphics      LovyanGFX");
    out.push_back("ideas from    Wadamesh (layouts, map, tools)");
    out.push_back("");
    out.push_back("# keys");
    out.push_back("turn / press  move / select     backspace  back");
    out.push_back("home: m c p t s   jump to an app   l  lock");
    out.push_back("orange (hold) numbers + symbols   caps  shift lock");
    out.push_back("map: turn zoom, wasd pan, n next node, c me");
  }, 60000));
}

// ---- the grid ------------------------------------------------------------------------------------------
struct Tile { const char* label; const char* icon; void (*open)(); String (*sub)(); };

static String subProfile() { return g_node ? String(P().node_name) : String(""); }
static String subRadio()   { return g_node ? String(P().freq, 3) + (P().isRepeatEn() ? "  rpt" : "") : String(""); }
static String subChans()   { int n = 0; if (g_node) for (int i = 0; i < MAX_GROUP_CHANNELS; i++) { ChannelDetails c; if (g_node->getChannel(i, c) && c.name[0]) n++; } return String(n); }
static String subBle()     { return bleConnected() ? "linked" : bleEnabled() ? "on" : "off"; }
static String subWifi()    { return !wifi::enabled() ? "off" : wifi::connected() ? String(wifi::ssid()) : String("searching"); }
static String subGps()     { return !ui_settings.gpsOn ? "off" : gps.hasFix() ? "fix" : "searching"; }
static String subNotify()  { return ui_settings.dnd ? "dnd" : "on"; }
static String subNone()    { return ""; }
static String subTheme()   { return app::themeSpec().name; }
static String subBattery() { return power::saver() ? String(app::batteryPct()) + "% saver" : power::holding() ? String("held 80%") : String(app::batteryPct()) + "%"; }

static const Tile TILES[] = {
  {"Profile", "@", profileMenu, subProfile},
  {"Radio & Mesh", "~", radioMenu, subRadio},
  {"Channels", "#", channelsMenu, subChans},
  {"Contacts", "+", autoAddMenu, subNone},
  {"Bluetooth", "B", bluetoothMenu, subBle},
  {"Wi-Fi", "w", wifiMenu, subWifi},
  {"GPS", "o", gpsMenu, subGps},
  {"Clock", "t", clockMenu, subNone},
  {"Display", "*", displayMenu, subNone},
  {"Theme", "^", themeMenu, subTheme},
  {"Sound", "v", soundMenu, subNone},
  {"Notifications", "!", notifyMenu, subNotify},
  {"Messages", "=", messagesMenu, subNone},
  {"Telemetry", "%", telemetryMenu, subNone},
  {"Backups", "S", backupsMenu, subNone},
  {"Battery", "b", batteryMenu, subBattery},
  {"System", "&", systemMenu, subNone},
  {"About", "i", aboutPage, subNone},
};
static constexpr int TILE_N = sizeof(TILES) / sizeof(TILES[0]);

class SettingsGrid : public View {
public:
  void rotate(int d) override { _f = ((_f + d) % TILE_N + TILE_N) % TILE_N; dirty = true; }
  void press() override {
    // Without a running node only the device-side sections make sense.
    static const bool NEEDS_NODE[TILE_N] = {1,1,1,1,1,0,0,0,0,0,0,0,0,1,1,0,0,0};
    if (g_node || !NEEDS_NODE[_f]) TILES[_f].open(); else nav.toast("radio not running");
  }
  void key(char c) override {
    if (c == '\n') { press(); return; }
    const char lc = tolower(c);
    for (int k = 1; k <= TILE_N; k++) {
      const int i = (_f + k) % TILE_N;
      if (tolower(TILES[i].label[0]) == lc) { _f = i; dirty = true; return; }
    }
  }
  void tick() override { if (millis() - _last > 2000) { _last = millis(); dirty = true; } }
  void draw(Canvas& g) override {
    const Theme& t = nav.theme();
    char pos[12];
    snprintf(pos, sizeof(pos), "%d/%d", _f + 1, TILE_N);
    drawHeader(g, "Settings", pos);
    const int cols = 2, tw = 230, th = 52, gap = 6, rows = 3;
    const int row = _f / cols;
    if (row < _top) _top = row;
    if (row >= _top + rows) _top = row - rows + 1;
    for (int r = _top; r < _top + rows; r++) {
      for (int c = 0; c < cols; c++) {
        const int i = r * cols + c;
        if (i >= TILE_N) break;
        const int x = 8 + c * (tw + gap), y = L::BODY_Y + 4 + (r - _top) * (th + gap);
        const bool on = i == _f;
        g.fillRoundRect(x, y, tw, th, 8, on ? t.focus : t.panel);
        g.drawRoundRect(x, y, tw, th, 8, on ? t.green : t.line);
        g.fillCircle(x + 26, y + th / 2, 15, on ? t.green : t.greenDim);
        g.setTextColor(t.bg, on ? t.green : t.greenDim);
        g.drawString(TILES[i].icon, x + 26 - g.textWidth(TILES[i].icon) / 2, y + th / 2 - 8);
        g.setTextColor(on ? t.green : t.white, on ? t.focus : t.panel);
        g.drawString(TILES[i].label, x + 50, y + 9);
        String s = TILES[i].sub();
        if (s.length()) {
          g.setTextColor(t.dim, on ? t.focus : t.panel);
          g.drawString(s, x + 50, y + 28);
        }
      }
    }
    drawScrollbar(g, (TILE_N + 1) / 2, _top, rows, L::BODY_Y, L::H - L::BODY_Y);
  }
private:
  int _f = 0, _top = 0;
  uint32_t _last = 0;
};

void app::openSettings() { nav.push(new SettingsGrid()); }
