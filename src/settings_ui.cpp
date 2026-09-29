// Settings grid and its menus. Mesh values are saved a few seconds after the
// last change so spinning a value doesn't wear the flash.

#include "app.h"
#include "node.h"
#include "history.h"
#include "dataio.h"
#include "haptic.h"
#if BOARD_HAS_TOUCH
#include "hwcheck.h"   // src/tdeck
#endif
#include "gps.h"
#include "backlight.h"
#include "netwifi.h"
#include "power.h"
#include "battery.h"
#include "notify.h"
#include "ota.h"
#include "board_pins.h"
#if BOARD_HAS_REPORTS
#include "bugreport.h"
#endif
#include "logstore.h"
#include "regional.h"
#include "regions.h"
#include <algorithm>
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

// ---- where you are: region preset, time zone, first-start setup -------------------------------
// The preset the radio is on now, or -1 for custom settings.
static int currentRegion() {
  if (!g_node) return -1;
  for (int i = 0; i < regional::REGION_COUNT; i++) {
    const regional::Region& r = regional::REGIONS[i];
    if (fabsf(P().freq - r.freq) < 0.0006f && fabsf(P().bw - r.bw) < 0.05f && P().sf == r.sf && P().cr == r.cr &&
        (r.hashMode < 0 || P().path_hash_mode == r.hashMode)) return i;
  }
  return -1;
}
static String regionName() { const int i = currentRegion(); return i < 0 ? String("custom") : String(regional::REGIONS[i].name); }

static void applyRegion(int i) {
  const regional::Region& r = regional::REGIONS[i];
  P().freq = r.freq; P().bw = r.bw; P().sf = r.sf; P().cr = r.cr;
  if (r.hashMode >= 0) P().path_hash_mode = r.hashMode;
  radioChanged();
}

// The zone's name without its example cities, for the narrow spots.
static String zoneShort() {
  String n = regional::zoneName();
  const int b = n.indexOf(" (");
  return b > 0 ? n.substring(0, b) : n;
}
static void setZone(uint8_t z) { ui_settings.tzZone = z; markUiDirty(); nav.statusChanged(); }

// next: during setup, where picking one (or keeping what's there) goes on to.
static void regionMenu(const char* title, std::function<void()> next) {
  auto* m = new MenuView(title);
  if (next) m->action("keep this radio  (" + String(P().freq, 3) + " MHz)", next);
  for (int i = 0; i < regional::REGION_COUNT; i++)
    m->toggle(regional::REGIONS[i].name, [i] { return currentRegion() == i; }, [i, next] {
      applyRegion(i);
      // A new pager's map opens over Spokane until the GPS has a fix: open it over
      // the region picked instead.
      if (next && fabsf(ui_settings.mapLat - 47.6588f) < 0.001f && fabsf(ui_settings.mapLon + 117.4260f) < 0.001f) {
        ui_settings.mapLat = regional::REGIONS[i].lat; ui_settings.mapLon = regional::REGIONS[i].lon;
        ui_settings.mapZoom = regional::REGIONS[i].zoom; markUiDirty();
      }
      if (next) next(); else nav.toast((String("radio set: ") + regional::REGIONS[i].name).c_str());
    });
  nav.push(m);
}

static void zoneMenu(const char* title, std::function<void()> next) {
  auto* m = new MenuView(title);
  if (next) m->action("keep  " + regional::zoneName(), next);
  for (int i = 0; i < regional::ZONE_COUNT; i++)
    m->toggle(regional::ZONES[i].name, [i] { return ui_settings.tzZone == i + 1; }, [i, next] {
      setZone(i + 1);
      if (next) next();
    });
  if (!next)
    m->adjust("or a fixed offset", [] { return ui_settings.tzZone ? String("-") : regional::fixedOffsetName(); },
              [](int d) {
                if (ui_settings.tzZone) ui_settings.tzMinutes = regional::offsetMin(app::now());   // start from the zone's
                else ui_settings.tzMinutes = constrain(ui_settings.tzMinutes + d * 30, -720, 840);
                setZone(0);
              });
  nav.push(m);
}

// Shown once, on a pager's first start (and once after the update that brought it):
// its radio region, time zone, and clock and units. Each step can keep what's there;
// backing out of it leaves it for the next start.
static View* s_setupBelow = nullptr;
static void setupUnits() {
  auto* m = new MenuView("Setup 3/3  clock and units");
  m->toggle("24-hour clock", [] { return ui_settings.clock24; },
            [] { ui_settings.clock24 = !ui_settings.clock24; markUiDirty(); nav.statusChanged(); });
  m->toggle("distances in miles", [] { return ui_settings.miles; },
            [] { ui_settings.miles = !ui_settings.miles; markUiDirty(); });
  m->action("done", [] {
    ui_settings.setupDone = 1;
    markUiDirty();
    nav.popTo(s_setupBelow);
    nav.toast("all set");
  });
  nav.push(m);
}
static void setupZone() { zoneMenu("Setup 2/3  time zone", setupUnits); }
void startSetup() {
  s_setupBelow = nav.top();
  if (g_node) regionMenu("Setup 1/3  your radio region", setupZone);
  else setupZone();
}

static void scopeMenu(const uint8_t* secret);   // region scope, below

static void radioMenu() {
  auto* m = new MenuView("Radio & Mesh");
  m->rebuild = [](MenuView& v) {
    v.submenu("region preset", [] { regionMenu("Region presets", nullptr); }, [] { return regionName(); });
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
    v.submenu("default region scope", [] { scopeMenu(nullptr); }, []() -> String {
      const char* d = regions::defaultName();
      return *d ? String(d) : String("none");
    });
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
// ---- region scope ----------------------------------------------------------------------------------
// Where messages flood: everywhere (plain flood), or only through the repeaters that
// serve a region (regions.h). A repeater passes a region's messages on only if it
// has that exact region (spelling and capitals), so the safe way to choose is from
// what the repeaters in range say they serve: RegionScanView asks them.

// What the repeaters in range say they carry (regions::Scan), as the app's
// Discover Regions: every region with how many carry it, to pick one; and how many
// still pass messages with no region. pick(name) gets the region chosen.
class RegionScanView : public MenuView {
public:
  explicit RegionScanView(std::function<void(const char*)> pick) : MenuView("Discover regions"), _pick(pick) {
    refreshMs = 500;
    _scan.start();
    refresh();
  }
  void tick() override {
    MenuView::tick();
    if (_scan.tick()) refresh();
  }

private:
  void refresh() {
    const int f = _focus, s = _scroll;
    _rows.clear();
    if (_scan.failed()) {
      info("radio busy", [] { return String("try again in a moment"); });
    } else {
      const String state = _scan.done() ? String("done") : _scan.asking() ? String("asking a repeater...") : String("listening...");
      info(state, [this] { return String(_scan.answered) + " answered"; });
    }
    for (int i = 0; i < _scan.count(); i++) {
      const String nm = _scan.name(i);
      const int c = _scan.servedBy(i);
      value(nm, [c] { return String(c) + (c == 1 ? " repeater" : " repeaters"); },
            [this, nm] { auto fn = _pick; nav.pop(); fn(nm.c_str()); });
    }
    if (_scan.answered) {
      const int w = _scan.wholeMesh, a = _scan.answered;
      info("no region (unscoped)", [w, a] { return String(w) + " of " + String(a) + " pass it"; });
    }
    if (_scan.done() && !_scan.answered)
      info(_scan.silent ? "no answer" : "no repeaters in range", [] { return String("try closer to one"); });
    if (_scan.added) {
      const int a = _scan.added;
      info("added to contacts", [a] { return String(a) + (a == 1 ? " repeater" : " repeaters"); });
    }
    if (_scan.full) {
      const int u = _scan.full;
      info("contacts full, skipped", [u] { return String(u) + (u == 1 ? " repeater" : " repeaters"); });
    }
    _focus = constrain(f, 0, max(0, (int)_rows.size() - 1));
    _scroll = s;
    dirty = true;
  }

  std::function<void(const char*)> _pick;
  regions::Scan _scan;
};

// The app's Set Region Scope (secret: that channel) and Default Region Scope
// (nullptr): pick from the regions added, clear it, discover what the repeaters
// in range carry, or add one by name. A channel with no region follows the
// default; the default with none is plain flood.
static void regionListMenu();
static void scopeMenu(const uint8_t* secret) {
  struct Who { bool channel; uint8_t s[16]; } who{secret != nullptr, {0}};
  if (secret) memcpy(who.s, secret, 16);
  auto* m = new MenuView(secret ? "Set region scope" : "Default region scope");
  m->rebuild = [who](MenuView& v) {
    auto current = [who]() -> String {
      return who.channel ? String(regions::forChannel(who.s)) : String(regions::defaultName());
    };
    auto set = [who](const char* name) {
      if (who.channel) regions::setForChannel(who.s, name); else regions::setDefault(name);
      if (*name) nav.toast((String("region scope: ") + name).c_str());
      else if (who.channel) nav.toast(*regions::defaultName() ? (String("scope cleared: default ") + regions::defaultName()).c_str()
                                                             : "scope cleared: no region");
      else nav.toast("no default region: flood");
    };
    auto setAndClose = [set](const char* name) { nav.pop(); set(name); };
    v.header("only repeaters that carry it pass it on");
    const char* d = regions::defaultName();
    const String none = who.channel ? (*d ? String("clear scope (default ") + d + ")" : String("clear scope (no region)"))
                                    : String("none: flood");
    v.toggle(none, [current] { return current().length() == 0; }, [setAndClose] { setAndClose(""); });
    char names[regions::LIST_MAX][regions::NAME_LEN + 1];
    const int n = regions::list(names, regions::LIST_MAX);
    for (int i = 0; i < n; i++) {
      const String nm = names[i];
      v.toggle(nm, [current, nm] { return current() == nm; }, [setAndClose, nm] { setAndClose(nm.c_str()); });
    }
    v.action("discover regions", [setAndClose] {
      nav.push(new RegionScanView([setAndClose](const char* c) { setAndClose(c); }));
    });
    v.action("+ add a region", [set] {
      prompt("Add a region", "its name, exactly as the repeaters have it", "", regions::NAME_LEN, [set](const String& s) {
        char name[regions::NAME_LEN + 1];
        const char* err = nullptr;
        if (!s.length()) return;                 // nothing typed: nothing changes
        if (!regions::clean(s.c_str(), name, sizeof(name), &err)) { nav.toast(err); return; }
        // A name no repeater near here carries means messages that go nowhere.
        const String nm = name;
        confirm("Use " + nm + "?", "repeaters that don't carry it drop these messages. spelling and capitals must match",
                [set, nm] { regions::add(nm.c_str()); nav.pop(); set(nm.c_str()); });
      });
    });
    if (n) v.action("edit the list", [] { regionListMenu(); });
  };
  m->rebuild(*m);
  nav.push(m);
}

// The regions added, to remove ones no longer wanted. Removing one from the list
// leaves any channel or default using it as it is.
static void regionListMenu() {
  auto* m = new MenuView("Regions added");
  m->rebuild = [](MenuView& v) {
    char names[regions::LIST_MAX][regions::NAME_LEN + 1];
    const int n = regions::list(names, regions::LIST_MAX);
    if (!n) v.info("none yet", [] { return String(""); });
    for (int i = 0; i < n; i++) {
      const String nm = names[i];
      v.action("remove " + nm, [nm] {
        confirm("Remove " + nm + "?", "from the list; channels using it keep it", [nm] { regions::remove(nm.c_str()); nav.toast("removed"); });
      });
    }
  };
  m->rebuild(*m);
  nav.push(m);
}

// Opened from a channel's chat (its message menu, or the T-Deck's header).
void openChannelRegionScope(const uint8_t* secret16) { scopeMenu(secret16); }

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
  m->value("region scope", [secret]() -> String {
    const char* own = regions::forChannel(secret);
    if (*own) return String(own);
    return *regions::defaultName() ? String("default ") + regions::defaultName() : String("none");
  }, [secret] { scopeMenu(secret); });
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
  void rotate(int d) override { const int n = wifi::scanCount(); if (n) { _f = ((_f + d) % n + n) % n; _finger = false; dirty = true; } }
  void key(char c) override { if (c == 'r') { wifi::startScan(); _f = 0; _top = 0; } else if (c == '\n') press(); }
  // A finger: tap a network to join it, drag to scroll, tap the header's right
  // side to scan again.
  bool touch(const TouchEvent& e) override {
    const int n = wifi::scanDone() ? wifi::scanCount() : 0;
    switch (e.type) {
      case TouchEvent::Down: _dragAcc = 0; return false;
      case TouchEvent::Drag: {
        const int was = _top;
        const bool shown = !_finger;
        _finger = true;
        _dragAcc += e.dy;
        while (_dragAcc <= -L::ROW_H && _top < max(0, n - visible())) { _top++; _dragAcc += L::ROW_H; }
        while (_dragAcc >= L::ROW_H && _top > 0) { _top--; _dragAcc -= L::ROW_H; }
        return _top != was || shown;           // redraw only when something moved
      }
      case TouchEvent::Tap: {
        _finger = true;
        if (e.y < L::BODY_Y) {
          if (e.x < L::W / 2 || !wifi::scanDone()) return false;
          key('r');
          return true;
        }
        const int i = _top + (e.y - L::BODY_Y) / L::ROW_H;
        if (i >= n) return false;
        _f = i;
        press();
        return true;
      }
      default: return false;
    }
  }
  void press() override {
    if (!wifi::scanDone() || !wifi::scanCount()) return;
    const String ss = wifi::scanSsid(_f);
    if (!ss.length()) return;
    if (wifi::scanEnterprise(_f)) { nav.toast("needs a username login (work/school): not supported"); return; }
    if (wifi::scanOpen(_f)) { wifi::save(ss.c_str(), ""); nav.pop(); nav.toast("joining open network"); return; }
    prompt("Password", ss, "", 63, [ss](const String& pw) {
      wifi::save(ss.c_str(), pw.c_str());
      nav.pop();                          // back out of the scan list
      nav.toast("saved - connecting");
    }, true);
  }
  void draw(Canvas& g) override {
    const Theme& t = nav.theme();
    const bool touch = BOARD_HAS_TOUCH;
    drawHeader(g, "Wi-Fi networks", wifi::scanDone() ? (touch ? "2.4 GHz  tap: rescan" : "2.4 GHz only   r = rescan") : "scanning...");
    const int n = wifi::scanCount();
    if (!wifi::scanDone()) { g.setTextColor(t.dim, t.bg); g.drawString("looking for networks...", 14, L::BODY_Y + 10); return; }
    if (!n) { g.setTextColor(t.dim, t.bg); g.drawString(touch ? "nothing found. tap the header to rescan" : "nothing found. r to rescan", 14, L::BODY_Y + 10); return; }
    const int visible = this->visible();
    if (!_finger) {
      if (_f < _top) _top = _f;
      if (_f >= _top + visible) _top = _f - visible + 1;
    }
    const int ty = (L::ROW_H - 16) / 2;      // text centred in the row
    for (int i = _top; i < n && i < _top + visible; i++) {
      const int y = L::BODY_Y + (i - _top) * L::ROW_H;
      const bool on = i == _f && !_finger;
      const uint16_t bg = on ? t.focus : t.bg;
      if (on) { g.fillRect(0, y, L::W, L::ROW_H, bg); g.fillRect(0, y, 3, L::ROW_H, t.green); }
      char nm[40];
      sanitize(wifi::scanSsid(i), nm, sizeof(nm));
      char r[24];
      snprintf(r, sizeof(r), "%s %d dBm", wifi::scanOpen(i) ? "open" : wifi::scanEnterprise(i) ? "login" : "", wifi::scanRssi(i));
      g.setTextColor(on ? t.green : t.white, bg);
      drawUtf8(g, nm[0] ? nm : "(hidden)", 12, y + ty, L::W - 34 - g.textWidth(r));
      g.setTextColor(t.dim, bg);
      g.drawString(r, L::W - 12 - g.textWidth(r), y + ty);
    }
    drawScrollbar(g, n, _top, visible, L::BODY_Y, L::H - L::BODY_Y);
  }
private:
  // Eight rows on the pager; as many as fit where rows are taller (the T-Deck).
  static int visible() { return min(8, (L::H - L::BODY_Y) / L::ROW_H); }
  int _f = 0, _top = 0, _dragAcc = 0;
  bool _finger = BOARD_HAS_TOUCH;
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
      v.value(ss, [i]() -> String { return String(wifi::savedState(i)); },
              [i, ss] { confirm("Forget " + ss + "?", "to fix a password, scan + join it again", [i] { wifi::forget(i); nav.toast("forgotten"); }); });
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
  m->submenu("time zone", [] { zoneMenu("Time zone", nullptr); }, [] { return zoneShort(); });
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
      app::setTime((uint32_t)(local - regional::offsetMin(local) * 60));
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
#if !BOARD_HAS_TOUCH   // a touchscreen unlocks with a swipe up
  m->toggle("unlock with wheel press only", [] { return ui_settings.wheelUnlock; },
            [] { ui_settings.wheelUnlock = !ui_settings.wheelUnlock; markUiDirty(); });
#endif
  m->toggle("wake screen on message", [] { return ui_settings.wakeOnMessage; },
            [] { ui_settings.wakeOnMessage = !ui_settings.wakeOnMessage; markUiDirty(); });
  // The lock screen's sasquatch and his speech bubble (squatch_talk.h).
  m->toggle("sasquatch talks", [] { return !ui_settings.squatchQuiet; },
            [] { ui_settings.squatchQuiet = !ui_settings.squatchQuiet; markUiDirty(); });
#if BOARD_HAS_TOUCH
  // For a unit that comes out different from the ones this was made from: put it
  // right here instead of needing another build. The touch test shows the result.
  m->header("screen + touch");
  auto flag = [m](const char* label, uint8_t b) {
    m->toggle(label, [b] { return (ui_settings.orient & b) != 0; },
              [b] { ui_settings.orient ^= b; app::applyDisplay(); markUiDirty(); });
  };
  flag("screen upside down", 1);
  flag("touch mirrored left-right", 2);
  flag("touch mirrored up-down", 4);
  flag("trackball reversed", 8);
  flag("colours inverted", 16);
  m->toggle("double tap to wake", [] { return ui_settings.tapWake; },
            [] { ui_settings.tapWake = !ui_settings.tapWake; markUiDirty(); });
  m->action("touch test", [] { openTouchTest(); });
#endif
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
  auto* m = new MenuView(BOARD_HAS_HAPTIC ? "Sound & vibration" : "Sound");
#if BOARD_HAS_HAPTIC
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
#endif
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
  m->toggle("  with repeater id size (2B)", [] { return !ui_settings.hideHashBytes; },
            [] { ui_settings.hideHashBytes = !ui_settings.hideHashBytes; markUiDirty(); });
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
#if BOARD_HAS_CHARGER_IC
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
#endif
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
  m->action("run first-start setup", [] { startSetup(); });
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
#ifdef OTA_BOARD
  // This board's updates go in by themselves once it's idle (ota.h).
  m->toggle("install updates by itself", [] { return ota::autoInstall(); }, [] {
    ota::setAutoInstall(!ota::autoInstall());
    nav.toast(ota::autoInstall() ? "updates install when it's idle, on wi-fi" : "it will ask before updating", 3000);
  });
#endif
#if BOARD_HAS_REPORTS
  // Crashes and errors go to the developer (bugreport.h): no messages, names or places.
  m->toggle("send problem reports", [] { return report::enabled(); }, [] {
    report::setEnabled(!report::enabled());
    nav.toast(report::enabled() ? "crashes and errors go to the developer - no messages, names or places"
                                : "problem reports off", 3500);
  });
#endif
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
#if BOARD_HAS_POWER_OFF
  m->action("power off", [] { app::powerOffPrompt(); });
#endif
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
#if BOARD_HAS_TOUCH
    out.push_back("tap  open / select      drag  scroll");
    out.push_back("swipe in from the left, or tap <   back");
    out.push_back("trackball  move + click   hold it  lock");
    out.push_back("home: m c p t s   jump to an app   l  lock");
    out.push_back("map: drag pan, wasd pan, n next node, c me");
#else
    out.push_back("turn / press  move / select     backspace  back");
    out.push_back("home: m c p t s   jump to an app   l  lock");
    out.push_back("orange (hold) numbers + symbols   caps  shift lock");
    out.push_back("map: turn zoom, wasd pan, n next node, c me");
#endif
  }, 60000));
}

// ---- the grid ------------------------------------------------------------------------------------------
struct Tile { const char* label; const char* icon; void (*open)(); String (*sub)(); };

static String subProfile() { return g_node ? String(P().node_name) : String(""); }
static String subRadio()   { return g_node ? String(P().freq, 3) + (P().isRepeatEn() ? "  rpt" : "") : String(""); }
static String subChans()   { int n = 0; if (g_node) for (int i = 0; i < MAX_GROUP_CHANNELS; i++) { ChannelDetails c; if (g_node->getChannel(i, c) && c.name[0]) n++; } return String(n); }
static String subBle()     { return bleConnected() ? "linked" : bleEnabled() ? "on" : "off"; }
static String subWifi()    { return String(wifi::shortStatus()); }
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
  {"Clock", "t", clockMenu, zoneShort},
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
  void rotate(int d) override { _f = ((_f + d) % TILE_N + TILE_N) % TILE_N; _finger = false; dirty = true; }
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
      if (tolower(TILES[i].label[0]) == lc) { _f = i; _finger = false; dirty = true; return; }
    }
  }
  // A finger: tap a tile to open it, drag to scroll. The highlight is the wheel's
  // (or trackball's) and stays hidden while a finger is doing the work.
  bool touch(const TouchEvent& e) override {
    const int maxTop = max(0, ROWS_ALL - ROWS);
    switch (e.type) {
      case TouchEvent::Down: _dragAcc = 0; return false;
      case TouchEvent::Drag: {
        const int was = _top;
        const bool shown = !_finger;
        _finger = true;
        _dragAcc += e.dy;
        while (_dragAcc <= -STEP && _top < maxTop) { _top++; _dragAcc += STEP; }
        while (_dragAcc >= STEP && _top > 0) { _top--; _dragAcc -= STEP; }
        return _top != was || shown;           // redraw only when something moved
      }
      case TouchEvent::Tap: {
        _finger = true;
        if (e.y < L::BODY_Y + 4) return false;
        const int r = _top + (e.y - L::BODY_Y - 4) / STEP, c = (e.x - 8) / (TW + GAP);
        const int i = r * COLS + c;
        if (c < 0 || c >= COLS || r >= _top + ROWS || i >= TILE_N) return false;
        _f = i;
        press();
        return true;
      }
      default: return false;
    }
  }
  void tick() override { if (millis() - _last > 2000) { _last = millis(); dirty = true; } }
  void draw(Canvas& g) override {
    const Theme& t = nav.theme();
    char pos[12];
    snprintf(pos, sizeof(pos), "%d/%d", _f + 1, TILE_N);
    drawHeader(g, "Settings", _finger ? nullptr : pos);
    const int row = _f / COLS;
    if (!_finger) {                               // keep the highlight in view
      if (row < _top) _top = row;
      if (row >= _top + ROWS) _top = row - ROWS + 1;
    }
    // Narrow screens (the T-Deck): the icon and text move in to fit the tile.
    const int ix = NARROW ? 21 : 26, ir = NARROW ? 13 : 15, tx = NARROW ? 40 : 50;
    for (int r = _top; r < _top + ROWS; r++) {
      for (int c = 0; c < COLS; c++) {
        const int i = r * COLS + c;
        if (i >= TILE_N) break;
        const int x = 8 + c * (TW + GAP), y = L::BODY_Y + 4 + (r - _top) * STEP;
        const bool on = i == _f && !_finger;
        g.fillRoundRect(x, y, TW, TH, 8, on ? t.focus : t.panel);
        g.drawRoundRect(x, y, TW, TH, 8, on ? t.green : t.line);
        g.fillCircle(x + ix, y + TH / 2, ir, on ? t.green : t.greenDim);
        g.setTextColor(t.bg, on ? t.green : t.greenDim);
        g.drawString(TILES[i].icon, x + ix - g.textWidth(TILES[i].icon) / 2, y + TH / 2 - 8);
        g.setTextColor(on ? t.green : t.white, on ? t.focus : t.panel);
        if (NARROW) drawUtf8(g, TILES[i].label, x + tx, y + 9, TW - tx - 4);
        else g.drawString(TILES[i].label, x + tx, y + 9);
        String s = TILES[i].sub();
        if (s.length()) {
          g.setTextColor(t.dim, on ? t.focus : t.panel);
          if (NARROW) drawUtf8(g, s.c_str(), x + tx, y + 28, TW - tx - 4);
          else g.drawString(s, x + tx, y + 28);
        }
      }
    }
    drawScrollbar(g, ROWS_ALL, _top, ROWS, L::BODY_Y, L::H - L::BODY_Y);
  }
private:
  static constexpr bool NARROW = L::W < 400;
  static constexpr int COLS = 2, ROWS = 3, GAP = 6, TH = 52, STEP = TH + GAP;
  static constexpr int TW = NARROW ? (L::W - 16 - GAP) / 2 : 230;
  static constexpr int ROWS_ALL = (TILE_N + COLS - 1) / COLS;
  int _f = 0, _top = 0, _dragAcc = 0;
  bool _finger = BOARD_HAS_TOUCH;           // a touchscreen board opens with no highlight
  uint32_t _last = 0;
};

void app::openSettings() { nav.push(new SettingsGrid()); }
