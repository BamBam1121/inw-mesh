// The T-Deck's home screen: made for a finger, not a wheel. The time and date on
// top, the newest conversations in a card (tap one to answer it), and a dock of
// big buttons for everything else. The trackball and the keyboard still work:
// rolling moves a highlight through the rows and buttons, a click opens, and the
// pager's letter shortcuts (m c p t s, l to lock) are the same.
//
// The pager keeps its wheel carousel (home.cpp); this replaces it on boards that
// set BOARD_HOME_DASHBOARD.
#include "app.h"
#include "node.h"
#include "history.h"
#include "chats.h"
#include "gps.h"
#include "netwifi.h"
#include "regional.h"

namespace {

constexpr int CLOCK_Y = 22;
constexpr int CARD_X = 8, CARD_Y = 62, CARD_W = L::W - 16, ROW_H = 34, ROWS = 3;
constexpr int CARD_H = ROWS * ROW_H + 6;
constexpr int DOCK_Y = CARD_Y + CARD_H + 8, DOCK_N = 5, DOCK_GAP = 6;
constexpr int DOCK_W = (L::W - 16 - (DOCK_N - 1) * DOCK_GAP) / DOCK_N;
constexpr int DOCK_H = L::H - DOCK_Y - 6;

struct DockItem { const char* label; char key; void (*open)(); };
const DockItem DOCK[DOCK_N] = {
  { "Chats",    'm', [] { app::openChats(); } },
  { "People",   'c', [] { app::openContacts(); } },
  { "Map",      'p', [] { app::openMap(); } },
  { "Tools",    't', [] { app::openTools(); } },
  { "Settings", 's', [] { app::openSettings(); } },
};

// The frame of a card or button, in the theme's own style (as the carousel's cards).
void frame(lgfx::LovyanGFX& d, const Theme& t, int x, int y, int w, int h, bool focus, bool filled) {
  const uint16_t fill = focus ? t.focus : (filled ? t.panel : t.bg);
  switch (t.style) {
    case STYLE_BLOCKS:
      d.fillRect(x, y, w, h, fill);
      d.fillRect(x, y, w, 2, focus ? t.green : t.line);
      d.fillRect(x, y, 2, h, focus ? t.green : t.line);
      d.fillRect(x, y + h - 2, w, 2, t.greenDim);
      d.fillRect(x + w - 2, y, 2, h, t.greenDim);
      break;
    case STYLE_HERO:
      d.fillRect(x, y, w, h, fill);
      d.drawRect(x, y, w, h, focus ? t.green : t.line);
      d.drawRect(x + 2, y + 2, w - 4, h - 4, focus ? t.greenDim : t.line);
      break;
    case STYLE_AURORA:
      d.fillRoundRect(x, y, w, h, 10, fill);
      d.drawRoundRect(x, y, w, h, 10, focus ? t.green : t.line);
      if (focus) d.drawRoundRect(x - 2, y - 2, w + 4, h + 4, 12, t.greenDim);
      break;
    default:
      d.fillRoundRect(x, y, w, h, 4, fill);
      d.drawRoundRect(x, y, w, h, 4, focus ? t.green : t.line);
  }
}

// "Sat Sep 27": the big clock beside it already has the time.
const char* dateText() {
  static const char* const WD[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char* const MO[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  static char b[16];
  const uint32_t e = app::now();
  const int64_t local = (int64_t)e + regional::offsetMin(e) * 60;
  const int32_t days = (int32_t)(local / 86400);
  // civil date from days since 1970 (Howard Hinnant's algorithm)
  const int32_t z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
  const uint32_t doe = (uint32_t)(z - era * 146097);
  const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const uint32_t mp = (5 * doy + 2) / 153, d = doy - (153 * mp + 2) / 5 + 1;
  const uint32_t m = mp < 10 ? mp + 3 : mp - 9;
  snprintf(b, sizeof(b), "%s %s %u", WD[(days % 7 + 11) % 7], MO[m - 1], (unsigned)d);   // 1970-01-01 was a Thursday
  return b;
}

// Small line icons for the dock, drawn centred on (cx, cy).
void icon(lgfx::LovyanGFX& d, int which, int cx, int cy, uint16_t c) {
  switch (which) {
    case 0:   // chats: a speech bubble
      d.drawRoundRect(cx - 11, cy - 8, 22, 15, 5, c);
      d.fillTriangle(cx - 5, cy + 6, cx - 1, cy + 6, cx - 7, cy + 11, c);
      d.drawFastHLine(cx - 6, cy - 3, 12, c);
      d.drawFastHLine(cx - 6, cy + 1, 8, c);
      break;
    case 1:   // people: a head and shoulders
      d.drawCircle(cx, cy - 5, 5, c);
      d.drawArc(cx, cy + 12, 11, 10, 200, 340, c);
      break;
    case 2:   // map: a pin
      d.drawCircle(cx, cy - 4, 7, c);
      d.fillCircle(cx, cy - 4, 2, c);
      d.drawLine(cx - 6, cy, cx, cy + 10, c);
      d.drawLine(cx + 6, cy, cx, cy + 10, c);
      break;
    case 3:   // tools: an antenna and its waves
      d.fillCircle(cx, cy - 1, 2, c);
      d.drawLine(cx, cy + 1, cx, cy + 10, c);
      d.drawArc(cx, cy - 1, 7, 6, 210, 330, c);
      d.drawArc(cx, cy - 1, 12, 11, 210, 330, c);
      break;
    default:  // settings: a gear
      d.drawCircle(cx, cy, 6, c);
      d.drawCircle(cx, cy, 2, c);
      for (int k = 0; k < 8; k++) {
        const float a = k * 0.7854f;
        d.fillCircle(cx + (int)lroundf(cosf(a) * 9), cy + (int)lroundf(sinf(a) * 9), 2, c);
      }
  }
}

#if INW_DEV
}  // namespace
uint32_t g_dashUs[4];   // home: microseconds in refresh, clock, card, dock (the USB "prof" prints them)
namespace {
#endif
class DashboardView : public View {
public:
  bool isHome() override { return true; }
  bool hasClock() override { return true; }     // the big clock below: the bar above shows the name
  bool headerBack() override { return false; }

  void draw(Canvas& g) override {
    const Theme& t = nav.theme();
#if INW_DEV
    const uint32_t t0 = micros();
    refresh();
    const uint32_t t1 = micros();
    drawClock(g, t);
    const uint32_t t2 = micros();
    drawCard(g, t);
    const uint32_t t3 = micros();
    drawDock(g, t);
    g_dashUs[0] = t1 - t0; g_dashUs[1] = t2 - t1; g_dashUs[2] = t3 - t2; g_dashUs[3] = micros() - t3;
#else
    refresh();
    drawClock(g, t);
    drawCard(g, t);
    drawDock(g, t);
#endif
  }

  void tick() override {
    if (history.gen != _gen || millis() - _drawnAt > 5000) dirty = true;
    // The trackball's highlight goes away when it's left alone, as a phone's would.
    if (_showFocus && millis() - _focusAt > 15000) { _showFocus = false; dirty = true; }
  }

  // Trackball: through the conversation rows, then the dock.
  void rotate(int d) override {
    const int n = _n + DOCK_N;
    if (_showFocus) _focus = constrain(_focus + d, 0, n - 1);
    _showFocus = true;                 // the first roll just shows where it is
    _focusAt = millis();
    dirty = true;
  }
  // The ball: up and down through the messages, down from the last one onto the dock,
  // sideways along the dock, up from it back to the messages.
  bool roll(int dx, int dy) override {
    if (!_showFocus) { rotate(0); return true; }
    const int n = _n + DOCK_N;
    if (_focus >= _n) {
      if (dx) _focus = constrain(_focus + dx, _n, n - 1);
      if (dy < 0 && _n) _focus = _n - 1;
    } else {
      _focus = constrain(_focus + (dy ? dy : dx), 0, _n);
    }
    _focusAt = millis();
    dirty = true;
    return true;
  }
  void press() override { if (_showFocus) activate(_focus); else { _showFocus = true; dirty = true; } }
  void key(char c) override {
    if (c == '\n') { press(); return; }
    if (c == 'l') { app::lock(); return; }
    for (const DockItem& it : DOCK) if (c == it.key) { it.open(); return; }
  }
  bool backspace() override { app::lock(); return true; }

  bool touch(const TouchEvent& e) override {
    if (e.type != TouchEvent::Tap) return false;
    _showFocus = false;                  // a finger needs no highlight
    if (e.y >= CARD_Y && e.y < CARD_Y + CARD_H && e.x >= CARD_X && e.x < CARD_X + CARD_W) {
      const int r = (e.y - CARD_Y - 3) / ROW_H;
      if (r >= 0 && r < _n) { _focus = r; activate(r); return true; }
      if (!_n) { app::openChats(); return true; }
      return false;
    }
    if (e.y >= DOCK_Y) {
      const int b = (e.x - 8) / (DOCK_W + DOCK_GAP);
      if (b >= 0 && b < DOCK_N) { _focus = _n + b; activate(_n + b); return true; }
    }
    return false;
  }

  void resume() override { _listAt = 0; dirty = true; }

private:
  // The list of recent conversations is worked out again only when a message has come
  // or gone, or every 5 s (a channel renamed, a mute changed): it is 20 ms of looking
  // through the history, and done for every frame it was half of what drawing this
  // screen cost.
  void refresh() {
    _drawnAt = millis();
    if (_listAt && history.gen == _gen && millis() - _listAt < 5000) return;
    _listAt = millis() | 1;
    _n = recentChats(_chats, ROWS);
    _gen = history.gen;
    if (_focus >= _n + DOCK_N) _focus = _n + DOCK_N - 1;
  }

  void activate(int i) {
    if (i < _n) { openThread(_chats[i].key); return; }
    const int b = i - _n;
    if (b >= 0 && b < DOCK_N) DOCK[b].open();
  }

  void drawClock(Canvas& g, const Theme& t) {
    g.setFont(&fonts::Font4);
    g.setTextColor(t.green, t.bg);
    g.drawString(app::timeValid() ? clockText(app::now()) : "--:--", 12, CLOCK_Y + 4);
    g.setFont(&fonts::Font2);
    g.setTextColor(t.txt, t.bg);
    if (app::timeValid()) {
      const char* date = dateText();
      g.drawString(date, L::W - 12 - g.textWidth(date), CLOCK_Y + 2);
    }
    char line[48];
    snprintf(line, sizeof(line), "%d contacts  //  %s", g_node ? g_node->getNumContacts() : 0,
             bleConnected() ? "phone linked" : (bleEnabled() ? "ble on" : "ble off"));
    g.setTextColor(t.dim, t.bg);
    g.drawString(line, L::W - 12 - g.textWidth(line), CLOCK_Y + 20);
  }

  void drawCard(Canvas& g, const Theme& t) {
    frame(g, t, CARD_X, CARD_Y, CARD_W, CARD_H, false, true);
    g.setFont(&fonts::Font2);
    if (!_n) {
      g.setTextColor(t.dim, t.panel);
      const char* a = "no messages yet";
      const char* b = "tap here to start one";
      g.drawString(a, (L::W - g.textWidth(a)) / 2, CARD_Y + CARD_H / 2 - 18);
      g.drawString(b, (L::W - g.textWidth(b)) / 2, CARD_Y + CARD_H / 2 + 2);
      return;
    }
    for (int i = 0; i < _n; i++) {
      const ChatEntry& e = _chats[i];
      const int y = CARD_Y + 3 + i * ROW_H;
      const bool on = _showFocus && _focus == i;
      const uint16_t bg = on ? t.focus : t.panel;
      if (on) { g.fillRect(CARD_X + 3, y, CARD_W - 6, ROW_H, bg); g.fillRect(CARD_X + 3, y, 3, ROW_H, t.green); }
      else if (i) g.drawFastHLine(CARD_X + 36, y, CARD_W - 44, t.line);
      drawAvatar(g, CARD_X + 20, y + ROW_H / 2, 12, e.name, e.kind);
      const char* ago = e.lastTs ? timeAgo(e.lastTs) : "";
      const int agoW = g.textWidth(ago);
      g.setTextColor(e.unread ? t.green : t.txt, bg);
      drawUtf8(g, e.name, CARD_X + 40, y + 1, CARD_W - 52 - agoW - 28);
      g.setTextColor(t.dim, bg);
      g.drawString(ago, CARD_X + CARD_W - 10 - agoW, y + 1);
      char pv[80];
      strlcpy(pv, e.preview, sizeof(pv));
      g.setTextColor(e.unread ? t.txt : t.dim, bg);
      drawUtf8(g, pv, CARD_X + 40, y + 17, CARD_W - 52 - (e.unread ? 22 : 0));
      if (e.unread) {
        char n[6];
        snprintf(n, sizeof(n), "%u", e.unread);
        const int w = max(16, (int)g.textWidth(n) + 8);
        g.fillRoundRect(CARD_X + CARD_W - 10 - w, y + 17, w, 15, 7, e.mention ? t.amber : t.green);
        g.setTextColor(t.bg, e.mention ? t.amber : t.green);
        g.drawString(n, CARD_X + CARD_W - 10 - w + (w - g.textWidth(n)) / 2, y + 17);
      }
    }
  }

  void drawDock(Canvas& g, const Theme& t) {
    g.setFont(&fonts::Font2);
    const uint16_t unread = app::unread();
    for (int b = 0; b < DOCK_N; b++) {
      const int x = 8 + b * (DOCK_W + DOCK_GAP);
      const bool on = _showFocus && _focus == _n + b;
      frame(g, t, x, DOCK_Y, DOCK_W, DOCK_H, on, true);
      const uint16_t bg = on ? t.focus : t.panel;
      icon(g, b, x + DOCK_W / 2, DOCK_Y + DOCK_H / 2 - 8, on ? t.green : t.greenDim);
      g.setTextColor(on ? t.green : t.txt, bg);
      const char* label = DOCK[b].label;
      g.drawString(label, x + (DOCK_W - g.textWidth(label)) / 2, DOCK_Y + DOCK_H - 19);
      if (b == 0 && unread) {
        char n[6];
        snprintf(n, sizeof(n), "%u", unread);
        const int w = max(16, (int)g.textWidth(n) + 8);
        g.fillRoundRect(x + DOCK_W - w - 3, DOCK_Y + 3, w, 15, 7, t.amber);
        g.setTextColor(t.bg, t.amber);
        g.drawString(n, x + DOCK_W - w - 3 + (w - g.textWidth(n)) / 2, DOCK_Y + 3);
      }
    }
  }

  ChatEntry _chats[ROWS];
  int _n = 0, _focus = 0;
  bool _showFocus = false;              // the trackball's highlight: hidden for touch
  uint32_t _focusAt = 0;
  uint32_t _gen = 0, _drawnAt = 0;
  uint32_t _listAt = 0;                  // when the list was last worked out
};

}  // namespace

View* makeHomeView() { return new DashboardView(); }
