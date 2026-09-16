#include "app.h"
#include "node.h"
#include "history.h"
#include "dataio.h"
#include "gps.h"
#include "logstore.h"
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
    dirty = true;
  }
  void tick() override {
    if (!g_node) return;
    if (g_node->discoveredCount != _seen) { _seen = g_node->discoveredCount; dirty = true; }
    if (millis() - _started < 12000 && millis() - _last > 1000) { _last = millis(); dirty = true; }
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
    const bool live = millis() - _started < 12000;
    char right[32];
    snprintf(right, sizeof(right), live ? "listening %lus" : "done  r = rescan",
             (unsigned long)(12 - (millis() - _started) / 1000));
    drawHeader(g, "Discover nearby", right);
    const int n = g_node ? g_node->discoveredCount : 0;
    if (!n) {
      g.setTextColor(t.dim, t.bg);
      g.drawString(live ? "asking repeaters in direct range to answer..." : "no answers. press to try again", 14, L::BODY_Y + 10);
      return;
    }
    g.setTextColor(t.greenDim, t.bg);
    g.drawString("node", 14, L::BODY_Y + 2);
    g.drawString("they hear us", 250, L::BODY_Y + 2);
    g.drawString("we hear them", 365, L::BODY_Y + 2);
    for (int i = 0; i < n && i < 7; i++) {
      const DiscoverHit& h = g_node->discovered[i];
      const int y = L::BODY_Y + 20 + i * 22;
      const bool on = i == _focus;
      if (on) g.fillRect(0, y - 2, L::W, 22, t.focus);
      ContactInfo* c = g_node->contact(h.pub);
      char nm[36];
      if (c) sanitize(c->name, nm, sizeof(nm));
      else snprintf(nm, sizeof(nm), "%02x%02x%02x%02x (new)", h.pub[0], h.pub[1], h.pub[2], h.pub[3]);
      const uint16_t bg = on ? t.focus : t.bg;
      g.setTextColor(on ? t.green : t.white, bg);
      drawRich(g, nm, 14, y);
      char a[20], b[24];
      snprintf(a, sizeof(a), "%.1f dB", h.theirSnr4 / 4.0);
      snprintf(b, sizeof(b), "%.1f dB %d", h.ourSnr4 / 4.0, h.rssi);
      g.setTextColor(h.theirSnr4 > 0 ? t.green : h.theirSnr4 > -28 ? t.amber : t.red, bg);
      g.drawString(a, 250, y);
      g.setTextColor(h.ourSnr4 > 0 ? t.green : h.ourSnr4 > -28 ? t.amber : t.red, bg);
      g.drawString(b, 365, y);
    }
  }
private:
  uint32_t _started = 0, _last = 0;
  uint8_t _seen = 0;
  int _focus = 0;
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
