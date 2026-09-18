#include "fieldtools.h"
#include <math.h>
#include <vector>
#include <Preferences.h>
#include <SD.h>
#include "app.h"
#include "node.h"
#include "history.h"
#include "dataio.h"
#include "gps.h"
#include "haptic.h"
#include "backlight.h"
#include "logstore.h"

extern LogStore logs;

namespace field {

// ---- shared ------------------------------------------------------------------------------
static bool fix(double& lat, double& lon) {
  if (!gps.hasFix()) return false;
  lat = gps.fix().lat; lon = gps.fix().lon;
  return true;
}

// Metres between two points (haversine; plenty for walking distances).
static double metres(double la1, double lo1, double la2, double lo2) {
  const double r = 6371000.0, d2r = M_PI / 180.0;
  const double dla = (la2 - la1) * d2r, dlo = (lo2 - lo1) * d2r;
  const double a = sin(dla / 2) * sin(dla / 2) + cos(la1 * d2r) * cos(la2 * d2r) * sin(dlo / 2) * sin(dlo / 2);
  return 2 * r * asin(sqrt(a));
}

static double bearing(double la1, double lo1, double la2, double lo2) {
  const double d2r = M_PI / 180.0;
  const double y = sin((lo2 - lo1) * d2r) * cos(la2 * d2r);
  const double x = cos(la1 * d2r) * sin(la2 * d2r) - sin(la1 * d2r) * cos(la2 * d2r) * cos((lo2 - lo1) * d2r);
  return fmod(atan2(y, x) / d2r + 360.0, 360.0);
}

static const char* compass(double deg) {
  static const char* P[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  return P[(int)((deg + 22.5) / 45.0) % 8];
}

static String distText(double m) {
  if (m < 1000) return String((int)lround(m)) + " m";
  return String(m / 1000.0, m < 10000 ? 2 : 1) + " km";
}

// A file under /inw/<dir> named for when it started, or null with no card.
static bool openLog(const char* dir, const char* stem, const char* header, char* path, size_t cap) {
  if (!sdMount()) return false;
  char d[24];
  snprintf(d, sizeof(d), "/inw/%s", dir);
  if (!SD.exists("/inw")) SD.mkdir("/inw");
  if (!SD.exists(d)) SD.mkdir(d);
  if (app::timeValid()) {
    const time_t t = (time_t)app::now() + ui_settings.tzMinutes * 60;
    struct tm tm;
    gmtime_r(&t, &tm);
    snprintf(path, cap, "%s/%s-%04d%02d%02d-%02d%02d.csv", d, stem, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
  } else {
    snprintf(path, cap, "%s/%s-%lu.csv", d, stem, (unsigned long)(millis() / 1000));
  }
  File f = SD.open(path, FILE_WRITE);
  if (!f) return false;
  f.println(header);
  f.close();
  return true;
}

static void appendLine(const char* path, const String& line) {
  if (!path[0] || !sdMounted()) return;
  File f = SD.open(path, FILE_APPEND);
  if (!f) return;
  f.println(line);
  f.close();
}

static String utcStamp() {
  if (!app::timeValid()) return String("");
  const time_t t = (time_t)app::now();
  struct tm tm;
  gmtime_r(&t, &tm);
  char b[24];
  snprintf(b, sizeof(b), "%04d-%02d-%02dT%02d:%02d:%02dZ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
  return String(b);
}

// ---- range test ------------------------------------------------------------------------------
static bool s_range = false;
static uint16_t s_rangeEvery = 30;          // seconds between discovers
static uint32_t s_rangeNext = 0, s_rangeAsked = 0;
static uint32_t s_rounds = 0, s_hitsTotal = 0, s_answered = 0;
static uint8_t s_lastHits = 0;
static int8_t s_bestSnr4 = -128;
static char s_rangeBest[36] = "";
static char s_rangePath[64] = "";
static const uint32_t LISTEN_MS = 10000;    // repeaters answer within a few seconds

bool rangeOn() { return s_range; }

static void rangeStart() {
  if (!g_node) { nav.toast("radio not running"); return; }
  if (!openLog("range", "range", "time_utc,lat,lon,sats,repeater,key,they_hear_us_db,we_hear_them_db,rssi_dbm", s_rangePath, sizeof(s_rangePath))) {
    nav.toast("needs an sd card for the log", 3000);
    return;
  }
  s_range = true;
  s_rounds = s_hitsTotal = s_answered = 0;
  s_lastHits = 0; s_bestSnr4 = -128; s_rangeBest[0] = 0;
  s_rangeNext = millis() + 1500;              // give the GPS a moment
  s_rangeAsked = 0;
  logs.add(LOG_INFO, "range test started: %s", s_rangePath);
}

static void rangeStop() {
  if (!s_range) return;
  s_range = false;
  logs.add(LOG_INFO, "range test stopped: %lu rounds, %lu answers", (unsigned long)s_rounds, (unsigned long)s_hitsTotal);
}

static void rangeTick() {
  if (!s_range || !g_node) return;
  const uint32_t now = millis();
  // Results in: log every repeater that answered this round.
  if (s_rangeAsked && now - s_rangeAsked >= LISTEN_MS) {
    s_rangeAsked = 0;
    s_rounds++;
    double la = 0, lo = 0;
    const bool haveFix = fix(la, lo);
    const String where = String(utcStamp()) + "," + (haveFix ? String(la, 6) : String("")) + "," +
                         (haveFix ? String(lo, 6) : String("")) + "," + String(gps.fix().satellites);
    const uint8_t n = g_node->discoveredCount;
    s_lastHits = n;
    if (n) s_answered++;
    for (uint8_t i = 0; i < n; i++) {
      const DiscoverHit& h = g_node->discovered[i];
      char nm[36], key[10];
      ContactInfo* c = g_node->contact(h.pub);
      if (c) sanitize(c->name, nm, sizeof(nm)); else strlcpy(nm, "(unknown)", sizeof(nm));
      for (char* p = nm; *p; p++) if (*p == ',' || *p == '"') *p = ' ';
      snprintf(key, sizeof(key), "%02x%02x%02x%02x", h.pub[0], h.pub[1], h.pub[2], h.pub[3]);
      appendLine(s_rangePath, where + "," + nm + "," + key + "," + String(h.theirSnr4 / 4.0, 1) + "," +
                              String(h.ourSnr4 / 4.0, 1) + "," + String(h.rssi));
      if (h.ourSnr4 >= s_bestSnr4) { s_bestSnr4 = h.ourSnr4; strlcpy(s_rangeBest, nm, sizeof(s_rangeBest)); }
    }
    s_hitsTotal += n;
    if (!n) appendLine(s_rangePath, where + ",none,,,,");
  }
  // Next round.
  if (!s_rangeAsked && (int32_t)(now - s_rangeNext) >= 0) {
    s_rangeNext = now + s_rangeEvery * 1000UL;
    if (g_node->discover()) s_rangeAsked = now;
  }
}

static void rangePage() {
  auto* v = new TextPageView("Range test", [](std::vector<String>& out) {
    out.push_back(s_range ? "RUNNING - leave it on, the screen can go off" : "stopped");
    out.push_back(String("discover every ") + s_rangeEvery + " s");
    double la, lo;
    out.push_back(fix(la, lo) ? String("gps  ") + String(la, 5) + ", " + String(lo, 5) + "  (" + gps.fix().satellites + " sats)"
                              : String("gps  searching for a fix..."));
    out.push_back(String("rounds ") + s_rounds + "   answered " + s_answered + "   answers " + s_hitsTotal);
    if (s_rangeAsked) out.push_back(String("listening... ") + (LISTEN_MS - (millis() - s_rangeAsked)) / 1000 + " s");
    else if (s_rounds) out.push_back(String("last round: ") + s_lastHits + (s_lastHits == 1 ? " repeater" : " repeaters"));
    if (s_rangeBest[0]) out.push_back(String("best heard: ") + s_rangeBest + "  " + String(s_bestSnr4 / 4.0, 1) + " dB");
    if (s_rangePath[0]) out.push_back(String("log  sd ") + s_rangePath);
    out.push_back("");
    out.push_back("press: start / stop");
  }, 1000);
  v->onPress = [] { if (s_range) rangeStop(); else rangeStart(); };
  nav.push(v);
}

// ---- SOS -------------------------------------------------------------------------------------
static bool s_sos = false;
static uint32_t s_sosNext = 0, s_sosSent = 0;
static const uint32_t SOS_EVERY_MS = 5UL * 60UL * 1000UL;

bool sosOn() { return s_sos; }

// The SOS channel is kept apart from the UI settings blob so adding it doesn't
// change that blob's layout (which the settings mirror and restore depend on).
static int sosChannel() {
  Preferences p;
  int idx = 0;
  if (p.begin("inw-field", true)) { idx = p.getInt("sosch", 0); p.end(); }
  ChannelDetails ch;
  if (!g_node || !g_node->getChannel(idx, ch) || !ch.name[0]) {
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) if (g_node && g_node->getChannel(i, ch) && ch.name[0]) return i;
    return -1;
  }
  return idx;
}
static void setSosChannel(int idx) {
  Preferences p;
  if (p.begin("inw-field", false)) { p.putInt("sosch", idx); p.end(); }
}

static void sosSend() {
  const int idx = sosChannel();
  if (idx < 0 || !g_node) { logs.add(LOG_ERROR, "sos: no channel to send on"); return; }
  ChannelDetails ch;
  g_node->getChannel(idx, ch);
  double la, lo;
  char text[140];
  if (fix(la, lo))
    snprintf(text, sizeof(text), "SOS - %s needs help. At %.5f,%.5f (gps, %u sats)", g_node->name(), la, lo, gps.fix().satellites);
  else if (g_node->prefs().node_lat || g_node->prefs().node_lon)
    snprintf(text, sizeof(text), "SOS - %s needs help. Last known %.5f,%.5f (no gps fix now)", g_node->name(),
             g_node->prefs().node_lat, g_node->prefs().node_lon);
  else
    snprintf(text, sizeof(text), "SOS - %s needs help. Position unknown (no gps fix)", g_node->name());
  const uint32_t id = history.add(ConvKey::channel(ch.channel.secret), HF_OUT, ST_SENDING, g_node->name(), text, app::now());
  g_node->sendChannel(idx, text, id);
  s_sosSent++;
  logs.add(LOG_WARN, "sos sent (%lu) on %s", (unsigned long)s_sosSent, ch.name);
}

static void sosStart() {
  if (sosChannel() < 0) { nav.toast("join a channel first - sos goes to a channel", 3000); return; }
  s_sos = true;
  s_sosSent = 0;
  sosSend();                                 // position may be old; the next send has a fresh fix
  s_sosNext = millis() + SOS_EVERY_MS;
  haptic.buzz(3);
  nav.banner("SOS on", "sent now and every 5 minutes. tools > field to stop.", 6000);
}

static void sosStop() {
  if (!s_sos) return;
  s_sos = false;
  logs.add(LOG_INFO, "sos stopped after %lu sends", (unsigned long)s_sosSent);
  nav.toast("sos stopped", 3000);
}

static void sosTick() {
  if (!s_sos || (int32_t)(millis() - s_sosNext) < 0) return;
  s_sosNext = millis() + SOS_EVERY_MS;
  sosSend();
}

// Countdown before an SOS goes out: a pocketed pager pressed five times shouldn't
// call for help. Any key, the wheel or backspace cancels. The countdown itself
// runs in field::tick(), not in this view: someone in a panic keeps pressing the
// side button, which locks the screen on top of this view, and a countdown that
// only ran while its view was on top would then never send.
static uint32_t s_armAt = 0;                 // millis the countdown started; 0 = not armed
static uint32_t armRemaining() {
  if (!s_armAt) return 0;
  const uint32_t e = millis() - s_armAt;
  return e >= 5000 ? 0 : (5000 - e + 999) / 1000;
}

class SosArmView : public View {
public:
  SosArmView() { haptic.buzz(2); }
  void tick() override {
    if (!s_armAt) { if (nav.top() == this) nav.pop(); return; }   // sent or cancelled
    const uint32_t left = armRemaining();
    if (left != _shown) { _shown = left; dirty = true; if (left) haptic.buzz(1); }
  }
  void draw(Canvas& g) override {
    const Theme& t = nav.theme();
    g.fillRect(0, L::HEAD_Y, L::W, L::H - L::HEAD_Y, t.red);
    g.setTextColor(t.white, t.red);
    g.setTextDatum(textdatum_t::middle_center);
    g.setFont(&fonts::Font4);
    g.drawString("SOS", L::W / 2, 70);
    char b[48];
    snprintf(b, sizeof(b), "sending in %lu", (unsigned long)armRemaining());
    g.drawString(b, L::W / 2, 110);
    g.setFont(&fonts::Font2);
    const int idx = sosChannel();
    ChannelDetails ch;
    if (idx >= 0 && g_node && g_node->getChannel(idx, ch)) {
      char nm[40];
      sanitize(ch.name, nm, sizeof(nm));
      snprintf(b, sizeof(b), "to channel %s, with your position", nm);
      g.drawString(b, L::W / 2, 145);
    }
    g.drawString("press any key to cancel", L::W / 2, 180);
    g.setTextDatum(textdatum_t::top_left);
  }
  void key(char) override { cancel(); }
  void press() override { cancel(); }
  void rotate(int) override { cancel(); }
  bool backspace() override { cancel(); return true; }
  bool wantsAllKeys() override { return true; }
private:
  void cancel() {
    if (!s_armAt) return;
    s_armAt = 0;
    logs.add(LOG_INFO, "sos countdown cancelled");
    nav.pop();
    nav.toast("sos cancelled");
  }
  uint32_t _shown = 99;
};

void sosArm() {
  if (s_sos) { nav.toast("sos is already on - tools > field to stop", 3000); return; }
  if (s_armAt) return;                       // already counting down
  s_armAt = millis() | 1;
  dimmer.wake();
  nav.push(new SosArmView());
}

static void armTick() {
  if (!s_armAt || armRemaining()) return;
  s_armAt = 0;                               // the view sees this and closes itself
  sosStart();
}

// Five presses of the side button inside four seconds.
void sosNoteButton() {
  static uint32_t t[5] = {};
  static uint8_t i = 0;
  const uint32_t now = millis();
  t[i] = now;
  i = (i + 1) % 5;
  const uint32_t oldest = t[i];
  if (oldest && now - oldest < 4000) {
    memset(t, 0, sizeof(t));
    sosArm();
  }
}

static void sosMenu() {
  auto* m = new MenuView("SOS beacon");
  m->rebuild = [](MenuView& v) {
    v.info("status", []() -> String { return s_sos ? String("ON - sent ") + s_sosSent + (s_sosSent == 1 ? " time" : " times") : String("off"); });
    if (s_sos) v.action("STOP SOS", [] { sosStop(); nav.pop(); });
    else v.action("send SOS now (5 s countdown)", [] { nav.pop(); sosArm(); });
    v.value("channel", []() -> String {
      const int idx = sosChannel();
      ChannelDetails ch;
      if (idx < 0 || !g_node->getChannel(idx, ch)) return String("none joined");
      char nm[40]; sanitize(ch.name, nm, sizeof(nm));
      return String(nm);
    }, [] {
      // Step to the next joined channel.
      int idx = sosChannel();
      for (int k = 1; k <= MAX_GROUP_CHANNELS; k++) {
        const int j = (idx + k) % MAX_GROUP_CHANNELS;
        ChannelDetails ch;
        if (g_node->getChannel(j, ch) && ch.name[0]) { setSosChannel(j); break; }
      }
      nav.invalidate();
    });
    v.info("repeats", []() -> String { return String("every 5 min until stopped"); });
    v.info("shortcut", []() -> String { return String("press the side button 5 times fast"); });
  };
  m->rebuild(*m);
  nav.push(m);
}

// ---- breadcrumb trail ------------------------------------------------------------------------
static bool s_trail = false;
static std::vector<TrailPt>* s_pts = nullptr;
static uint32_t s_trailCheck = 0;
static double s_walked = 0;
static char s_trailPath[64] = "";
static const size_t TRAIL_MAX = 5000;       // ~40 kB in PSRAM; about 75 km at 15 m spacing
static const double TRAIL_STEP_M = 15;

bool trailOn() { return s_trail; }
const TrailPt* trail(size_t& n) { n = s_pts ? s_pts->size() : 0; return n ? s_pts->data() : nullptr; }

static void trailStart() {
  if (!s_pts) s_pts = new std::vector<TrailPt>();
  s_pts->clear();
  s_pts->reserve(256);
  s_walked = 0;
  s_trail = true;
  s_trailCheck = 0;
  if (!openLog("trail", "trail", "time_utc,lat,lon,sats", s_trailPath, sizeof(s_trailPath))) s_trailPath[0] = 0;
  logs.add(LOG_INFO, "trail started%s%s", s_trailPath[0] ? ": " : " (no sd, map only)", s_trailPath);
}

static void trailStop() {
  if (!s_trail) return;
  s_trail = false;
  logs.add(LOG_INFO, "trail stopped: %u points, %s", (unsigned)(s_pts ? s_pts->size() : 0), distText(s_walked).c_str());
}

static void trailTick() {
  if (!s_trail || millis() - s_trailCheck < 5000) return;
  s_trailCheck = millis();
  double la, lo;
  if (!fix(la, lo)) return;
  if (!s_pts->empty()) {
    const TrailPt& p = s_pts->back();
    const double d = metres(p.lat, p.lon, la, lo);
    if (d < TRAIL_STEP_M) return;
    if (d > 2000) return;                    // a GPS jump, not a walk
    s_walked += d;
  }
  if (s_pts->size() >= TRAIL_MAX) s_pts->erase(s_pts->begin() + 1);   // keep the start, drop the oldest after it
  s_pts->push_back({(float)la, (float)lo});
  appendLine(s_trailPath, utcStamp() + "," + String(la, 6) + "," + String(lo, 6) + "," + String(gps.fix().satellites));
}

static void trailPage() {
  auto* v = new TextPageView("Breadcrumb trail", [](std::vector<String>& out) {
    out.push_back(s_trail ? "RECORDING - the screen can go off" : "stopped");
    const size_t n = s_pts ? s_pts->size() : 0;
    out.push_back(String("points ") + n + "   walked " + distText(s_walked));
    double la, lo;
    if (n && fix(la, lo)) {
      const TrailPt& s = s_pts->front();
      const double d = metres(la, lo, s.lat, s.lon), b = bearing(la, lo, s.lat, s.lon);
      out.push_back(String("back to start: ") + distText(d) + "  " + String((int)lround(b)) + " deg " + compass(b));
    } else if (!fix(la, lo)) {
      out.push_back("gps  searching for a fix...");
    }
    if (s_trailPath[0]) out.push_back(String("log  sd ") + s_trailPath);
    out.push_back("the map shows the trail and the start point");
    out.push_back("");
    out.push_back(s_trail ? "press: stop" : "press: start a new trail");
  }, 1000);
  v->onPress = [] { if (s_trail) trailStop(); else trailStart(); };
  nav.push(v);
}

// ---- menu / loop ------------------------------------------------------------------------------
bool wantsGps() { return s_range || s_sos || s_trail; }

void tick() {
  rangeTick();
  armTick();
  sosTick();
  trailTick();
}

void openMenu() {
  auto* m = new MenuView("Field");
  m->rebuild = [](MenuView& v) {
    v.action(s_range ? "range test  (running)" : "range test", [] { rangePage(); });
    v.adjust("range test: discover every", []() -> String { return String(s_rangeEvery) + " s"; }, [](int d) {
      static const uint16_t S[] = {15, 30, 60, 120};
      int i = 0;
      for (int k = 0; k < 4; k++) if (S[k] <= s_rangeEvery) i = k;
      s_rangeEvery = S[constrain(i + d, 0, 3)];
    });
    v.action(s_sos ? "SOS beacon  (ON)" : "SOS beacon", [] { sosMenu(); });
    v.action(s_trail ? "breadcrumb trail  (recording)" : "breadcrumb trail", [] { trailPage(); });
    v.info("gps", []() -> String { return wantsGps() ? String("kept on while these run") : String("as set in settings"); });
  };
  m->rebuild(*m);
  nav.push(m);
}

}  // namespace field
