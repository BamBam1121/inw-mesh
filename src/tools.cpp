#include "app.h"
#include "node.h"
#include "history.h"
#include "dataio.h"
#include "gps.h"
#include "logstore.h"
#include "fieldtools.h"
#include "fx.h"
#include <SPIFFS.h>
#include <SD.h>

extern uint32_t g_shotAt;          // main.cpp: take a screenshot at this millis()

static const char* payloadName(uint8_t t) {
  static const char* N[] = {"REQ", "RESP", "TXT", "ACK", "ADVERT", "GRP_TXT", "GRP_DATA", "ANON",
                            "PATH", "TRACE", "MULTI", "CTRL", "?12", "?13", "?14", "RAW"};
  return N[t & 15];
}

// ---- discover --------------------------------------------------------------------------------
class DiscoverView : public View {
public:
  DiscoverView() { scan(); }
  void scan() {
    if (!g_node || !g_node->discover()) { nav.toast("radio busy"); return; }
    _started = millis();
    _focus = 0;
    _seen = 0;
    _doneDrawn = false;
    dirty = true;
  }
  void tick() override {
    if (!g_node) return;
    if (g_node->discoveredCount != _seen) {
      // Each new answer lands on the scope with the theme's ping.
      for (uint8_t i = _seen; i < g_node->discoveredCount; i++) {
        int x, y;
        blipPos(g_node->discovered[i], x, y);
        fx::ping(x, y);
      }
      _seen = g_node->discoveredCount;
      dirty = true;
    }
    if (millis() - _started < LISTEN_MS) dirty = true;     // the sweep turns every frame
    else if (!_doneDrawn) { _doneDrawn = true; dirty = true; }
  }
  void rotate(int d) override {
    const int n = g_node ? g_node->discoveredCount : 0;
    if (n) { _focus = ((_focus + d) % n + n) % n; dirty = true; }
  }
  void key(char c) override { if (c == 'r') scan(); else if (c == '\n') press(); }
  void press() override {
    if (!g_node || _focus >= g_node->discoveredCount) { scan(); return; }
    const DiscoverHit& h = g_node->discovered[_focus];
    if (g_node->contact(h.pub)) app::openContactDetail(h.pub);
    else nav.toast("not a contact yet - it will be once it adverts");
  }
  void draw(Canvas& g) override {
    const Theme& t = nav.theme();
    const uint32_t age = millis() - _started;
    const bool live = age < LISTEN_MS;
    char right[32];
    snprintf(right, sizeof(right), live ? "listening %lus" : "done  r = rescan",
             (unsigned long)((LISTEN_MS - min(age, LISTEN_MS)) / 1000 + 1));
    drawHeader(g, "Discover nearby", right);
    const int n = g_node ? g_node->discoveredCount : 0;

    // The scope on the left: a sweep while listening, a blip for each answer.
    fx::radar(g, RX, RY, RR, live ? age * 6.2832f / 1700.0f - 1.5708f : -1);
    for (int i = 0; i < n; i++) {
      int x, y;
      blipPos(g_node->discovered[i], x, y);
      const uint32_t since = millis() - g_node->discovered[i].at;
      fx::blip(g, x, y, since < 900 ? 1.0f - since / 900.0f : 0, i == _focus);
    }

    // The list on the right.
    const int lx = RX + RR + 16;
    if (!n) {
      g.setTextColor(t.dim, t.bg);
      g.drawString(live ? "asking repeaters in" : "no answers.", lx, L::BODY_Y + 30);
      g.drawString(live ? "direct range to answer..." : "press to try again", lx, L::BODY_Y + 48);
      return;
    }
    g.setTextColor(t.greenDim, t.bg);
    g.drawString("node", lx, L::BODY_Y + 2);
    g.drawString("they", 348, L::BODY_Y + 2);
    g.drawString("we", 418, L::BODY_Y + 2);
    const int first = max(0, min(_focus - 3, n - 7));
    for (int i = first; i < n && i < first + 7; i++) {
      const DiscoverHit& h = g_node->discovered[i];
      const int y = L::BODY_Y + 20 + (i - first) * 22;
      const bool on = i == _focus;
      if (on) g.fillRect(lx - 4, y - 2, L::W - lx + 4, 22, t.focus);
      ContactInfo* c = g_node->contact(h.pub);
      char nm[36];
      if (c) sanitize(c->name, nm, sizeof(nm));
      else snprintf(nm, sizeof(nm), "%02x%02x%02x (new)", h.pub[0], h.pub[1], h.pub[2]);
      richFit(g, nm, 348 - lx - 6);
      const uint16_t bg = on ? t.focus : t.bg;
      g.setTextColor(on ? t.green : t.white, bg);
      drawRich(g, nm, lx, y);
      char a[12], b[12];
      snprintf(a, sizeof(a), "%.0f", h.theirSnr4 / 4.0);
      snprintf(b, sizeof(b), "%.0f", h.ourSnr4 / 4.0);
      g.setTextColor(h.theirSnr4 > 0 ? t.green : h.theirSnr4 > -28 ? t.amber : t.red, bg);
      g.drawString(a, 348, y);
      g.setTextColor(h.ourSnr4 > 0 ? t.green : h.ourSnr4 > -28 ? t.amber : t.red, bg);
      g.drawString(b, 418, y);
      g.setTextColor(t.dim, bg);
      g.drawString("dB", 348 + g.textWidth(a) + 3, y);
      g.drawString("dB", 418 + g.textWidth(b) + 3, y);
    }
  }
private:
  static constexpr uint32_t LISTEN_MS = 12000;
  static constexpr int RX = 92, RY = 132, RR = 82;     // the scope: centre and radius

  // Where an answer sits on the scope: its bearing from its key (stable, so a
  // node lands in the same place every scan), its distance from how well we
  // heard it - a strong signal sits near the middle.
  static void blipPos(const DiscoverHit& h, int& x, int& y) {
    const float a = ((h.pub[0] << 8) | h.pub[1]) / 65536.0f * 6.2832f;
    const float snr = h.ourSnr4 / 4.0f;
    const float d = RR * max(0.18f, min(0.92f, (12.0f - snr) / 32.0f));
    x = RX + (int)(cosf(a) * d);
    y = RY + (int)(sinf(a) * d);
  }

  uint32_t _started = 0;
  uint8_t _seen = 0;
  int _focus = 0;
  bool _doneDrawn = false;
};

// ---- text pages ------------------------------------------------------------------------------
static void signalPage() {
  nav.push(new TextPageView("Radio", [](std::vector<String>& out) {
    if (!g_node) { out.push_back("radio not running"); return; }
    NodePrefs& p = g_node->prefs();
    char b[72];
    snprintf(b, sizeof(b), "%.4f MHz  bw %.1f  sf%u  cr%u  %d dBm", p.freq, p.bw, p.sf, p.cr, p.tx_power_dbm); out.push_back(b);
    snprintf(b, sizeof(b), "last packet    rssi %.0f dBm   snr %.1f dB", g_node->lastRssi(), g_node->lastSnr()); out.push_back(b);
    snprintf(b, sizeof(b), "noise floor    %d dBm", g_node->noiseFloor()); out.push_back(b);
    snprintf(b, sizeof(b), "received       %lu  (%lu errors)", (unsigned long)g_node->rxCount(), (unsigned long)g_node->rxErrors()); out.push_back(b);
    snprintf(b, sizeof(b), "sent           %lu", (unsigned long)g_node->txCount()); out.push_back(b);
    snprintf(b, sizeof(b), "flood  rx %lu  tx %lu", (unsigned long)g_node->getNumRecvFlood(), (unsigned long)g_node->getNumSentFlood()); out.push_back(b);
    snprintf(b, sizeof(b), "direct rx %lu  tx %lu", (unsigned long)g_node->getNumRecvDirect(), (unsigned long)g_node->getNumSentDirect()); out.push_back(b);
    const uint32_t up = millis() / 1000;
    snprintf(b, sizeof(b), "airtime        %lu s  (%.2f%% of uptime)", (unsigned long)g_node->airtimeSecs(),
             up ? g_node->airtimeSecs() * 100.0 / up : 0.0); out.push_back(b);
    snprintf(b, sizeof(b), "client repeat  %s", p.isRepeatEn() ? "ON - forwarding" : "off"); out.push_back(b);
  }, 1000));
}

static void packetLogPage() {
  auto* v = new TextPageView("Packets (live)", [](std::vector<String>& out) {
    if (!g_node) return;
    const uint8_t n = g_node->pktCount;
    for (uint8_t i = 0; i < n; i++) {
      const PacketLogEntry& e = g_node->pktLog[(g_node->pktHead + InwNode::PKT_LOG_MAX - n + i) % InwNode::PKT_LOG_MAX];
      char b[80];
      const uint32_t ago = (millis() - e.at) / 1000;
      static const char* R[] = {"tflood", "flood", "direct", "tdirect"};
      if (e.tx) snprintf(b, sizeof(b), "%4lus  TX %-8s %-7s %uB", (unsigned long)ago, payloadName(e.payloadType), R[e.routeType & 3], e.len);
      else snprintf(b, sizeof(b), "%4lus  rx %-8s %-7s %uh %4ddBm %5.1fdB", (unsigned long)ago, payloadName(e.payloadType),
                    R[e.routeType & 3], e.hops, e.rssi, e.snr4 / 4.0);
      out.push_back(b);
    }
  }, 700);
  v->stickToBottom();
  nav.push(v);
}

static void recentPage() {
  nav.push(new TextPageView("Recently heard", [](std::vector<String>& out) {
    if (!g_node) return;
    AdvertPath ap[16];
    const int n = g_node->getRecentlyHeard(ap, 16);
    for (int i = 0; i < n; i++) {
      if (!ap[i].recv_timestamp) continue;
      char b[96];
      snprintf(b, sizeof(b), "%-22.22s %6s  %u hop%s", ap[i].name, timeAgo(ap[i].recv_timestamp),
               ap[i].path_len & 63, (ap[i].path_len & 63) == 1 ? "" : "s");
      out.push_back(b);
    }
    if (out.empty()) out.push_back("no adverts heard since boot");
  }, 2000));
}

static void gpsPage() {
  nav.push(new TextPageView("GPS", [](std::vector<String>& out) {
    char b[64];
    const GpsFix& f = gps.fix();
    snprintf(b, sizeof(b), "receiver   %s", !ui_settings.gpsOn ? "off" : gps.started() ? "on" : "not found"); out.push_back(b);
    snprintf(b, sizeof(b), "fix        %s", gps.hasFix() ? "yes" : "searching"); out.push_back(b);
    snprintf(b, sizeof(b), "satellites %u", f.satellites); out.push_back(b);
    if (gps.hasFix()) {
      snprintf(b, sizeof(b), "position   %.6f, %.6f", f.lat, f.lon); out.push_back(b);
    }
    if (f.year) { snprintf(b, sizeof(b), "utc        %04u-%02u-%02u %02u:%02u:%02u", f.year, f.month, f.day, f.hour, f.minute, f.second); out.push_back(b); }
    if (g_node) {
      snprintf(b, sizeof(b), "advert pos %.5f, %.5f", g_node->prefs().node_lat, g_node->prefs().node_lon); out.push_back(b);
      snprintf(b, sizeof(b), "sharing    %s", g_node->prefs().advert_loc_policy ? "in adverts" : "not shared"); out.push_back(b);
    }
  }, 1000));
}

void deviceInfoPage() {
  nav.push(new TextPageView("Device", [](std::vector<String>& out) {
    char b[72];
    snprintf(b, sizeof(b), "firmware     Squatch Mesh %s  (MeshCore %s)", FW_VERSION, FIRMWARE_VERSION); out.push_back(b);
    if (g_node) {
      char h[20];
      mesh::Utils::toHex(h, g_node->self_id.pub_key, 8);
      snprintf(b, sizeof(b), "node         %s", g_node->name()); out.push_back(b);
      snprintf(b, sizeof(b), "public key   %s...", h); out.push_back(b);
      int chans = 0;
      for (int i = 0; i < MAX_GROUP_CHANNELS; i++) { ChannelDetails c; if (g_node->getChannel(i, c) && c.name[0]) chans++; }
      snprintf(b, sizeof(b), "contacts     %d / %d    channels %d / %d", g_node->getNumContacts(), MAX_CONTACTS, chans, MAX_GROUP_CHANNELS); out.push_back(b);
      snprintf(b, sizeof(b), "ble pin      %06lu  %s", (unsigned long)blePin(), bleConnected() ? "(phone linked)" : ""); out.push_back(b);
    }
    snprintf(b, sizeof(b), "messages     %u stored", history.size()); out.push_back(b);
    snprintf(b, sizeof(b), "heap         %u kB free   psram %u kB free", (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getFreePsram() / 1024)); out.push_back(b);
    snprintf(b, sizeof(b), "flash store  %u / %u kB", (unsigned)(SPIFFS.usedBytes() / 1024), (unsigned)(SPIFFS.totalBytes() / 1024)); out.push_back(b);
    if (sdMounted()) { snprintf(b, sizeof(b), "sd card      %llu MB free", (unsigned long long)(sdFreeBytes() / 1048576ULL)); out.push_back(b); }
    else out.push_back("sd card      not mounted");
    snprintf(b, sizeof(b), "battery      %s  %u mV", app::batteryText(), app::batteryMv()); out.push_back(b);
    snprintf(b, sizeof(b), "uptime       %lu min", (unsigned long)(millis() / 60000)); out.push_back(b);
    if (ui_settings.lastSdBackup) { snprintf(b, sizeof(b), "sd backup    %s ago", timeAgo(ui_settings.lastSdBackup)); out.push_back(b); }
  }, 2000));
}

void logsPage() {
  nav.push(new TextPageView("Log", [](std::vector<String>& out) {
    for (uint8_t i = 0; i < logs.count(); i++) out.push_back(logs.line(i));
  }, 2000));
}

void app::openTools() {
  auto* m = new MenuView("Tools");
  m->header("mesh");
  m->action("advert: nearby (zero hop)", [] { nav.toast(g_node && g_node->advertZeroHop() ? "advert sent nearby" : "failed"); });
  m->action("advert: whole mesh (flood)", [] { nav.toast(g_node && g_node->advertFlood() ? "flood advert sent" : "failed"); });
  m->action("discover repeaters nearby", [] { nav.push(new DiscoverView()); });
  m->action("recently heard", [] { recentPage(); });
  m->action("trace / ping / console", [] { app::openContacts(); nav.toast("pick a contact"); });
  m->action("field: range test, sos, trail", [] { field::openMenu(); });
  m->header("radio");
  m->action("signal + radio stats", [] { signalPage(); });
  m->action("packet sniffer", [] { packetLogPage(); });
  m->header("device");
  m->action("gps", [] { gpsPage(); });
  m->action("device info", [] { deviceInfoPage(); });
  m->action("log", [] { logsPage(); });
  m->action("test notification", [] { app::testNotify(); });
  m->action("screenshot in 5 s (to sd)", [] { g_shotAt = millis() + 5000; nav.toast("go to the screen - capturing in 5 s"); });
  nav.push(m);
}
