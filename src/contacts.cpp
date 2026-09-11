#include "app.h"
#include "node.h"
#include "history.h"
#include <esp_heap_caps.h>
#include <algorithm>

struct CRow {
  uint8_t pub[32];
  char name[32];
  uint8_t type, flags, pathLen;
  uint32_t lastmod;
  int32_t lat, lon;
  float distKm;
};

static const char* kindName(uint8_t t) {
  switch (t) { case 1: return "chat"; case 2: return "repeater"; case 3: return "room"; case 4: return "sensor"; }
  return "?";
}

static String pathText(uint8_t pathLen) {
  if (pathLen == OUT_PATH_UNKNOWN) return "flood";
  const uint8_t n = pathLen & 63;
  return n == 0 ? String("direct") : String(n) + (n == 1 ? " hop" : " hops");
}

// ---- list ------------------------------------------------------------------------------------
class ContactListView : public View {
public:
  enum Filter : uint8_t { ALL, CHATS, REPEATERS, ROOMS, FAVS, NEAR, F_COUNT };
  enum Sort : uint8_t { RECENT, NAME, DISTANCE, S_COUNT };

  ContactListView() {
    _rows = (CRow*)heap_caps_malloc(sizeof(CRow) * MAX_CONTACTS, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    rebuild();
  }
  ~ContactListView() override { free(_rows); }
  bool wantsAllKeys() override { return true; }
  void resume() override { rebuild(); dirty = true; }

  void tick() override {
    if (g_node && g_node->contactsGen() != _gen && millis() - _built > 2000) { rebuild(); dirty = true; }
    else if (millis() - _built > 30000) { rebuild(); dirty = true; }     // "ago" columns age
  }
  void rotate(int d) override {
    const int n = _n + 2;                  // two control rows first
    _focus = ((_focus + d) % n + n) % n;
    dirty = true;
  }
  void key(char c) override {
    if (c == '\n') { press(); return; }
    if ((uint8_t)c < 0x20 || _filter.length() >= 16) return;
    _filter += c; _focus = 2; rebuild(); dirty = true;
  }
  bool backspace() override {
    if (!_filter.length()) return false;
    _filter.remove(_filter.length() - 1); rebuild(); dirty = true;
    return true;
  }
  void press() override {
    if (_focus == 0) { _type = (_type + 1) % F_COUNT; rebuild(); dirty = true; return; }
    if (_focus == 1) { _sort = (_sort + 1) % S_COUNT; rebuild(); dirty = true; return; }
    const int i = _focus - 2;
    if (i < _n) app::openContactDetail(_rows[i].pub);
  }

  void draw(Canvas& g) override {
    const Theme& t = nav.theme();
    char right[48];
    if (_filter.length()) snprintf(right, sizeof(right), "/%s  %d", _filter.c_str(), _n);
    else snprintf(right, sizeof(right), "%d of %d  type to search", _n, g_node ? g_node->getNumContacts() : 0);
    drawHeader(g, "Contacts", right);
    static const char* FN[] = {"All", "Chats", "Repeaters", "Rooms", "Favourites", "Nearby"};
    static const char* SN[] = {"Recent", "Name", "Distance"};
    const int cy = L::BODY_Y + 3;
    char a[24], b[24];
    snprintf(a, sizeof(a), "show: %s", FN[_type]);
    snprintf(b, sizeof(b), "sort: %s", SN[_sort]);
    const int aw = g.textWidth(a) + 20, bw = g.textWidth(b) + 20;
    drawPill(g, 8, cy, aw, 20, _focus == 0 ? t.green : t.line, _focus == 0 ? t.bg : t.txt, a);
    drawPill(g, 16 + aw, cy, bw, 20, _focus == 1 ? t.green : t.line, _focus == 1 ? t.bg : t.txt, b);

    const int top = L::BODY_Y + 28, rowH = 30;
    const int visible = (L::H - top) / rowH;
    const int sel = _focus - 2;
    if (sel >= 0) {
      if (sel < _scroll) _scroll = sel;
      if (sel >= _scroll + visible) _scroll = sel - visible + 1;
    } else _scroll = 0;
    if (!_n) {
      g.setTextColor(t.dim, t.bg);
      g.drawString(g_node && g_node->getNumContacts() ? "no matches" : "no contacts yet - they appear as adverts arrive", 14, top + 8);
    }
    for (int i = _scroll; i < _n && i < _scroll + visible; i++) {
      const CRow& r = _rows[i];
      const int y = top + (i - _scroll) * rowH;
      const bool on = i == sel;
      const uint16_t bg = on ? t.focus : t.bg;
      if (on) { g.fillRect(0, y, L::W, rowH, bg); g.fillRect(0, y, 3, rowH, t.green); }
      drawAvatar(g, 22, y + rowH / 2, 12, r.name, r.type);
      char nm[40];
      sanitize(r.name, nm, sizeof(nm) - 4);
      richFit(g, nm, 190);
      g.setTextColor(on ? t.green : t.white, bg);
      drawRich(g, nm, 42, y + 7);
      int rx = L::W - 12;
      if (r.flags & 1) { g.setTextColor(t.amber, bg); rx -= 10; g.drawString("*", rx, y + 7); rx -= 6; }
      const char* ago = timeAgo(r.lastmod);
      g.setTextColor(t.dim, bg);
      rx -= 44;
      g.drawString(ago, rx + 44 - g.textWidth(ago), y + 7);
      String p = pathText(r.pathLen);
      rx -= 60;
      g.drawString(p, rx + 54 - g.textWidth(p), y + 7);
      if (r.distKm >= 0) {
        const char* dt = app::fmtDistance(r.distKm);
        rx -= 70;
        g.drawString(dt, rx + 64 - g.textWidth(dt), y + 7);
      }
    }
    drawScrollbar(g, _n, _scroll, visible, top, L::H - top);
  }

private:
  bool match(const ContactInfo& c) {
    switch (_type) {
      case CHATS: if (c.type != ADV_TYPE_CHAT) return false; break;
      case REPEATERS: if (c.type != ADV_TYPE_REPEATER) return false; break;
      case ROOMS: if (c.type != ADV_TYPE_ROOM) return false; break;
      case FAVS: if (!(c.flags & 1)) return false; break;
      default: break;
    }
    if (_filter.length() && !strcasestr(c.name, _filter.c_str())) return false;
    return true;
  }

  void rebuild() {
    _n = 0;
    _built = millis();
    if (!g_node || !_rows) return;
    _gen = g_node->contactsGen();
    double mlat = 0, mlon = 0;
    const bool haveMe = app::myPosition(mlat, mlon);
    ContactsIterator it = g_node->startContactsIterator();
    ContactInfo c;
    while (it.hasNext(g_node, c) && _n < MAX_CONTACTS) {
      if (!c.type || !match(c)) continue;
      CRow& r = _rows[_n];
      memcpy(r.pub, c.id.pub_key, 32);
      strlcpy(r.name, c.name, sizeof(r.name));
      r.type = c.type; r.flags = c.flags; r.pathLen = c.out_path_len;
      r.lastmod = c.lastmod; r.lat = c.gps_lat; r.lon = c.gps_lon;
      r.distKm = (haveMe && (c.gps_lat || c.gps_lon))
                   ? (float)app::distanceKm(mlat, mlon, c.gps_lat / 1e6, c.gps_lon / 1e6) : -1.0f;
      if (_type == NEAR && (r.distKm < 0 || r.distKm > 50)) continue;
      _n++;
    }
    const uint8_t s = _type == NEAR ? (uint8_t)DISTANCE : _sort;
    std::sort(_rows, _rows + _n, [s](const CRow& a, const CRow& b) {
      if ((a.flags & 1) != (b.flags & 1)) return (a.flags & 1) > (b.flags & 1);   // favourites first
      if (s == NAME) return strcasecmp(a.name, b.name) < 0;
      if (s == DISTANCE) {
        if ((a.distKm < 0) != (b.distKm < 0)) return b.distKm < 0;
        return a.distKm < b.distKm;
      }
      return a.lastmod > b.lastmod;
    });
    if (_focus > _n + 1) _focus = _n + 1;
  }

  CRow* _rows = nullptr;
  int _n = 0, _focus = 2, _scroll = 0;
  uint8_t _type = ALL, _sort = RECENT;
  uint32_t _gen = 0, _built = 0;
  String _filter;
};

// ---- admin / terminal ---------------------------------------------------------------------------
class TerminalView : public View {
public:
  explicit TerminalView(const uint8_t* pub) { memcpy(_pub, pub, 32); }
  bool wantsAllKeys() override { return true; }
  void tick() override {
    if (g_node && g_node->cliGen != _gen) { _gen = g_node->cliGen; _scroll = 0; dirty = true; }
    if (millis() - _blink > 530) { _blink = millis(); _caret = !_caret; dirty = true; }
  }
  void key(char c) override {
    if (c == '\n') { run(); return; }
    if ((uint8_t)c >= 0x20 && _cmd.length() < 60) { _cmd += c; dirty = true; }
  }
  bool backspace() override {
    if (!_cmd.length()) return false;
    _cmd.remove(_cmd.length() - 1); dirty = true; return true;
  }
  void rotate(int d) override {
    if (!g_node) return;
    _scroll = constrain(_scroll - d, 0, max(0, (int)g_node->cliCount - 7));
    dirty = true;
  }
  void press() override {
    if (_cmd.length()) { run(); return; }
    auto* m = new MenuView("Commands");
    static const char* CMDS[] = { "ver", "clock", "clock sync", "neighbors", "get radio", "get tx",
                                  "get repeat", "get af", "get name", "get lat", "get lon",
                                  "get advert.interval", "get flood.max", "stats-core", "stats-radio",
                                  "stats-packets", "advert" };
    uint8_t pub[32];
    memcpy(pub, _pub, 32);
    for (const char* c : CMDS) {
      String s = c;
      m->action(s, [pub, s] { nav.pop(); if (g_node) g_node->sendCli(pub, s.c_str()); });
    }
    m->action("reboot the node", [pub] {
      confirm("Reboot repeater?", "it will be off the air for a few seconds",
              [pub] { nav.pop(); if (g_node) g_node->sendCli(pub, "reboot"); });
    });
    m->action("clear this log", [] { nav.pop(); if (g_node) g_node->cliClear(); });
    nav.push(m);
  }
  void draw(Canvas& g) override {
    const Theme& t = nav.theme();
    ContactInfo* c = g_node ? g_node->contact(_pub) : nullptr;
    char title[48];
    snprintf(title, sizeof(title), "Console  %s", c ? c->name : "?");
    const uint8_t ls = g_node ? g_node->loginState(_pub) : 0;
    drawHeader(g, title, ls == 2 ? (g_node->loginIsAdmin() ? "admin" : "guest") : ls == 1 ? "logging in" : "not logged in");
    const int lines = 8, top = L::BODY_Y + 2;
    const int n = g_node ? g_node->cliCount : 0;
    const int first = max(0, n - lines - _scroll);
    for (int i = 0; i < lines && first + i < n; i++) {
      const char* l = g_node->cliLog[first + i];
      char safe[80];
      sanitize(l, safe, sizeof(safe));
      g.setTextColor(l[0] == '>' ? t.green : t.txt, t.bg);
      g.drawString(safe, 10, top + i * 17);
    }
    if (!n) { g.setTextColor(t.dim, t.bg); g.drawString("type a command, or press for a list", 10, top + 4); }
    const int y = L::H - 26;
    g.fillRoundRect(6, y, L::W - 12, 24, 6, t.panel);
    g.setTextColor(t.green, t.panel);
    g.drawString(">", 14, y + 4);
    g.setTextColor(t.white, t.panel);
    g.drawString(_cmd, 28, y + 4);
    if (_caret) g.fillRect(29 + g.textWidth(_cmd), y + 5, 2, 14, t.green);
  }
private:
  void run() {
    if (!g_node || !_cmd.length()) return;
    if (!g_node->sendCli(_pub, _cmd.c_str())) nav.toast("send failed");
    _cmd = "";
    dirty = true;
  }
  uint8_t _pub[32];
  String _cmd;
  uint32_t _gen = 0, _blink = 0;
  int _scroll = 0;
  bool _caret = true;
};

static void loginFlow(const uint8_t* pubIn, bool thenConsole) {
  uint8_t pub[32];
  memcpy(pub, pubIn, 32);
  prompt("Log in", "password (blank for guest)", "", 15, [pub, thenConsole](const String& pw) {
    if (!g_node || !g_node->login(pub, pw.c_str())) { nav.toast("could not send login"); return; }
    nav.toast("logging in...");
    if (thenConsole) nav.push(new TerminalView(pub));
  }, true);
}

static void statusPage(const uint8_t* pubIn) {
  uint8_t pub[32];
  memcpy(pub, pubIn, 32);
  if (!g_node || !g_node->requestStatus(pub)) { nav.toast("request failed"); return; }
  const uint32_t started = millis();
  nav.push(new TextPageView("Status", [pub, started](std::vector<String>& out) {
    const RemoteStatus& s = g_node->lastStatus();
    if (!s.valid || memcmp(s.pub, pub, 6)) {
      out.push_back(millis() - started > 20000 ? "no answer (out of range, or needs a login)" : "asking...");
      return;
    }
    char b[64];
    snprintf(b, sizeof(b), "battery      %.2f V", s.battMv / 1000.0); out.push_back(b);
    snprintf(b, sizeof(b), "uptime       %lud %luh %lum", (unsigned long)(s.upSecs / 86400),
             (unsigned long)(s.upSecs / 3600 % 24), (unsigned long)(s.upSecs / 60 % 60)); out.push_back(b);
    snprintf(b, sizeof(b), "noise floor  %d dBm", s.noiseFloor); out.push_back(b);
    snprintf(b, sizeof(b), "last rssi    %d dBm   snr %.1f dB", s.lastRssi, s.lastSnr4 / 4.0); out.push_back(b);
    snprintf(b, sizeof(b), "packets      rx %lu  tx %lu", (unsigned long)s.recv, (unsigned long)s.sent); out.push_back(b);
    snprintf(b, sizeof(b), "flood        rx %lu  tx %lu", (unsigned long)s.recvFlood, (unsigned long)s.sentFlood); out.push_back(b);
    snprintf(b, sizeof(b), "direct       rx %lu  tx %lu", (unsigned long)s.recvDirect, (unsigned long)s.sentDirect); out.push_back(b);
    snprintf(b, sizeof(b), "airtime      %lu min   queue %u", (unsigned long)(s.airSecs / 60), s.txQueue); out.push_back(b);
    snprintf(b, sizeof(b), "round trip   %lu ms", (unsigned long)s.rtt); out.push_back(b);
  }, 500));
}

static void telemetryPage(const uint8_t* pubIn) {
  uint8_t pub[32];
  memcpy(pub, pubIn, 32);
  if (!g_node || !g_node->requestTelemetry(pub)) { nav.toast("request failed"); return; }
  const uint32_t started = millis();
  nav.push(new TextPageView("Telemetry", [started](std::vector<String>& out) {
    if (!g_node->telemetryText[0]) { out.push_back(millis() - started > 20000 ? "no answer" : "asking..."); return; }
    String s = g_node->telemetryText;
    int a = 0;
    while (a < (int)s.length()) {
      int e = s.indexOf('\n', a);
      if (e < 0) e = s.length();
      out.push_back(s.substring(a, e));
      a = e + 1;
    }
  }, 500));
}

static void tracePage(const uint8_t* pubIn) {
  uint8_t pub[32];
  memcpy(pub, pubIn, 32);
  if (!g_node || !g_node->trace(pub)) { nav.toast("no route to trace (needs a known path or a repeater)"); return; }
  const uint32_t started = millis();
  nav.push(new TextPageView("Trace", [started](std::vector<String>& out) {
    const TraceResult& r = g_node->lastTrace();
    if (!r.valid) { out.push_back(millis() - started > 25000 ? "no answer - a hop is down or out of range" : "tracing..."); return; }
    char b[64];
    snprintf(b, sizeof(b), "%u hops, %lu ms round trip", r.hops, (unsigned long)r.rtt); out.push_back(b);
    for (uint8_t i = 0; i < r.hops; i++) {
      ContactInfo* c = g_node->contactByPrefix(&r.hashes[i], 1);
      snprintf(b, sizeof(b), "%2u  %02x %-18.18s  snr %5.1f dB", i + 1, r.hashes[i], c ? c->name : "", r.snr4[i] / 4.0);
      out.push_back(b);
    }
    snprintf(b, sizeof(b), "back to us  snr %.1f dB", r.finalSnr4 / 4.0); out.push_back(b);
  }, 500));
}

// ---- one contact -----------------------------------------------------------------------------------
static void buildContact(MenuView& m, const uint8_t* pubIn) {
  uint8_t pub[32];
  memcpy(pub, pubIn, 32);
  ContactInfo* c = g_node ? g_node->contact(pub) : nullptr;
  if (!c) { m.info("contact", []() -> String { return String("no longer in the list"); }); return; }
  m.setTitle(c->name);
  const uint8_t type = c->type;
  const bool infra = type == ADV_TYPE_REPEATER || type == ADV_TYPE_ROOM;
  if (type != ADV_TYPE_REPEATER) m.action(type == ADV_TYPE_ROOM ? "open room" : "send message", [pub] { app::openThreadForContact(pub); });
  if (infra) {
    m.action("log in + console", [pub] {
      if (g_node && g_node->loginState(pub) == 2) nav.push(new TerminalView(pub));
      else loginFlow(pub, true);
    });
    m.action("status", [pub] { statusPage(pub); });
  }
  m.action("telemetry", [pub] { telemetryPage(pub); });
  m.action("trace route", [pub] { tracePage(pub); });
  m.toggle("favourite", [pub] { ContactInfo* x = g_node->contact(pub); return x && (x->flags & 1); },
           [pub] { g_node->toggleFavourite(pub); });
  m.header("details");
  m.info("type", [pub]() -> String { ContactInfo* x = g_node->contact(pub); return String(x ? kindName(x->type) : "?"); });
  m.info("route", [pub]() -> String { ContactInfo* x = g_node->contact(pub); return x ? pathText(x->out_path_len) : String("?"); });
  m.info("last heard", [pub]() -> String { ContactInfo* x = g_node->contact(pub);
    return x && x->lastmod ? String(timeAgo(x->lastmod)) + "  " + clockText(x->lastmod, true) : String("never"); });
  m.info("position", [pub]() -> String { ContactInfo* x = g_node->contact(pub);
    if (!x || (!x->gps_lat && !x->gps_lon)) return String("not shared");
    return String(x->gps_lat / 1e6, 4) + ", " + String(x->gps_lon / 1e6, 4); });
  m.info("distance", [pub]() -> String { ContactInfo* x = g_node->contact(pub); double la, lo;
    if (!x || (!x->gps_lat && !x->gps_lon) || !app::myPosition(la, lo)) return String("-");
    return String(app::fmtDistance(app::distanceKm(la, lo, x->gps_lat / 1e6, x->gps_lon / 1e6))); });
  m.info("key", [pub]() -> String { char h[20]; mesh::Utils::toHex(h, pub, 8); return String(h); });
  m.header("manage");
  if (c->gps_lat || c->gps_lon) {
    const double la = c->gps_lat / 1e6, lo = c->gps_lon / 1e6;
    const String nm = c->name;
    m.action("show on map", [la, lo, nm] { app::openMap(la, lo, nm.c_str()); });
  }
  m.action("reset route (flood next)", [pub] { g_node->resetPath(pub); nav.toast("route reset"); });
  m.action("share contact nearby", [pub] { nav.toast(g_node->shareContact(pub) ? "shared zero-hop" : "share failed"); });
  m.action("clear chat history", [pub] {
    confirm("Clear chat?", "messages with this contact are removed from this device",
            [pub] { history.clearConv(ConvKey::contact(pub)); nav.toast("cleared"); });
  });
  m.action("forget contact", [pub] {
    confirm("Forget contact?", "removes it until it adverts again",
            [pub] { if (g_node->forgetContact(pub)) { nav.pop(); nav.toast("forgotten"); } });
  });
}

void app::openContactDetail(const uint8_t* pubIn) {
  uint8_t pub[32];
  memcpy(pub, pubIn, 32);
  auto* m = new MenuView("Contact");
  m->rebuild = [pub](MenuView& v) { buildContact(v, pub); };
  buildContact(*m, pub);
  nav.push(m);
}

void app::openContacts() { nav.push(new ContactListView()); }

// Used by the tools (discover list) to open a node that may not be a contact yet.
void openTerminalFor(const uint8_t* pub) { nav.push(new TerminalView(pub)); }
