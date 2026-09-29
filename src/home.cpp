#include "app.h"
#include "carousel.h"
#include "mascot.h"
#include "scenes.h"
#include "node.h"
#include "history.h"
#include "gps.h"
#include "backlight.h"
#include "quips.h"
#include "power.h"

static Carousel s_carousel;

static const char* subChats() {
  static char b[24];
  const uint16_t n = app::unread();
  if (n) snprintf(b, sizeof(b), "%u unread", n);
  else snprintf(b, sizeof(b), "up to date");
  return b;
}
static const char* subContacts() {
  static char b[24];
  snprintf(b, sizeof(b), "%d known", g_node ? g_node->getNumContacts() : 0);
  return b;
}
static const char* subMap() { return gps.hasFix() ? "gps fix" : (ui_settings.gpsOn && !power::saver() ? "searching" : "gps off"); }
static const char* subTools() { return "discover  trace  rf"; }
#if INW_NFC
static const char* subNfc() { return "read  write  share"; }
void openNfc();
#endif
static const char* subSettings() {
  static char b[32];
  if (!g_node) return "radio down";
  snprintf(b, sizeof(b), "%.3f  sf%u", g_node->prefs().freq, g_node->prefs().sf);
  return b;
}

static const CarouselItem ITEMS[] = {
  { "MESSAGES", subChats,    [] { app::openChats(); } },
  { "CONTACTS", subContacts, [] { app::openContacts(); } },
  { "MAP",      subMap,      [] { app::openMap(); } },
  { "TOOLS",    subTools,    [] { app::openTools(); } },
#if INW_NFC
  { "NFC",      subNfc,      [] { openNfc(); } },
#endif
  { "SETTINGS", subSettings, [] { app::openSettings(); } },
};

static const char* footerText() {
  static char b[64];
  if (!g_node) return app::radioFault();
  snprintf(b, sizeof(b), "%s  //  %s%s", g_node->name(),
           bleConnected() ? "phone linked" : (bleEnabled() ? "ble on" : "ble off"),
           g_node->prefs().isRepeatEn() ? "  //  repeating" : "");
  return b;
}

class HomeView : public View {
public:
  HomeView() {
    s_carousel.begin(nav.display(), &nav.theme(), ITEMS, sizeof(ITEMS) / sizeof(ITEMS[0]));
    s_carousel.footer = footerText;
  }
  bool isHome() override { return true; }
  // Straight to the panel for smooth sliding, unless an overlay needs compositing.
  bool customRender() override { return !nav.overlayActive(); }
  void render(bool full) override {
    if (_wasOverlay) s_carousel.invalidate();     // overlay left debris: full repaint once
    else if (full) s_carousel.refresh();
    _wasOverlay = false;
    s_carousel.draw();
  }
  void draw(Canvas& g) override {
    s_carousel.drawInto(g);
    _wasOverlay = true;
  }
  void rotate(int d) override { s_carousel.nudge(d); dirty = true; }
  void press() override { s_carousel.select(); }
  void key(char c) override {
    if (c == '\n') { s_carousel.select(); return; }
    // Letter shortcuts from home: m(essages) c(ontacts) p (map) t(ools) s(ettings)
    switch (c) {
      case 'm': app::openChats(); break;
      case 'c': app::openContacts(); break;
      case 'p': app::openMap(); break;
      case 't': app::openTools(); break;
#if INW_NFC
      case 'n': openNfc(); break;
#endif
      case 's': app::openSettings(); break;
      case 'l': app::lock(); break;
    }
  }
  bool backspace() override { app::lock(); return true; }
  void tick() override {
    s_carousel.tick();
    if (s_carousel.dirty()) dirty = true;
    // Subtitles and the footer are live; refresh the frame now and then.
    if (millis() - _last > 3000) { _last = millis(); s_carousel.refresh(); dirty = true; }
  }
  void resume() override { s_carousel.invalidate(); dirty = true; }
private:
  bool _wasOverlay = false;
  uint32_t _last = 0;
};

// ---------------------------------------------------------------------------------
class LockView : public View {
public:
  bool isLock() override { return true; }
  void draw(Canvas& d) override {
    const Theme& t = nav.theme();
    drawStatusBar(d, t, false);          // the big clock below is the time here
    const bool hasUnread = app::unread() > 0;
    switch (t.style) {
      case STYLE_BLOCKS: scenes::blocks(d, t, _phase, _scroll, hasUnread); break;
      case STYLE_HERO:   scenes::hero(d, t, _phase, _scroll, hasUnread, app::batteryPct(), app::unread()); break;
      case STYLE_AURORA: scenes::aurora(d, t, _phase, _scroll, hasUnread); break;
      default:           scenes::inw(d, t, _phase, _scroll, hasUnread); break;
    }
    d.fillRect(0, 172, L::W, L::H - 172, t.bg);

    d.setFont(&fonts::Font4);
    d.setTextColor(t.green, t.bg);
    d.drawString(app::timeValid() ? clockText(app::now()) : "--:--", 8, 182);
    d.setFont(&fonts::Font2);
    d.setTextColor(t.dim, t.bg);
    const uint16_t un = app::unread();
    if (un || !g_node) {
      char sub[40];
      if (un) snprintf(sub, sizeof(sub), "%u unread message%s", un, un == 1 ? "" : "s");
      else snprintf(sub, sizeof(sub), "radio down");
      d.setTextColor(un ? t.amber : t.red, t.bg);
      // A narrow screen has no room beside the clock, but a taller one has a row under it.
      if (L::W < 400) d.drawString(sub, 8, 208);
      else d.drawString(sub, 140, 192);
      d.setTextColor(t.dim, t.bg);
    }
    if (app::timeValid()) {                // the date, once: the time is the big clock's
      const char* date = dateText(app::now());
      d.drawString(date, L::W - 8 - d.textWidth(date), 192);
    }
    // A new line on every wake, and every half hour while it sits here.
    if (!_quip[0] || millis() - _quipAt > 30UL * 60UL * 1000UL) {
      for (int i = 0; i < 8; i++) {
        strlcpy(_quip, quipNext(), sizeof(_quip));
        if (d.textWidth(_quip) <= L::W - 16) break;
      }
      _quipAt = millis();
    }
    d.setTextColor(t.greenDim, t.bg);
    d.drawString(_quip, 8, L::W < 400 ? 224 : 206);

    // Touchscreen: the face rides up with the finger (the status bar stays), and says
    // so once letting go will unlock.
    if (_lift > 0) {
      d.scroll(0, -_lift);
      d.fillRect(0, L::H - _lift, L::W, _lift, t.bg);
      drawStatusBar(d, t);
      if (_lift >= 24) {
        d.setFont(&fonts::Font2);
        d.setTextColor(_lift >= UNLOCK_PX ? t.green : t.dim, t.bg);
        d.setTextDatum(textdatum_t::bottom_center);
        d.drawString(_lift >= UNLOCK_PX ? "release to unlock" : "keep going", L::W / 2, L::H - 6);
        d.setTextDatum(textdatum_t::top_left);
      }
    }
  }
  void tick() override {
    if (dimmer.asleep()) _lift = 0;
    // Animate only while someone is looking at it: not dimmed, not off.
    if (dimmer.asleep() || dimmer.dimmed()) return;
    if (millis() - _step < 33) return;
    _step = millis();
    _phase += 0.32f;
    _scroll += 2.0f;
    if (_scroll > 10000.0f) _scroll = 0;
    dirty = true;
  }
  // By default only a wheel press unlocks: keys and wheel turns happen in a pocket,
  // and any of them used to open the pager. Settings can allow any key again.
  void rotate(int) override { if (ui_settings.wheelUnlock) hint(); else nav.pop(); }
  void press() override { nav.pop(); }
  void key(char) override { if (ui_settings.wheelUnlock) hint(); else nav.pop(); }
  bool backspace() override { if (ui_settings.wheelUnlock) hint(); else nav.pop(); return true; }
  // Touchscreen: a swipe up unlocks, like a phone. Any drag that ends UNLOCK_PX above
  // where it began, mostly upward, however slowly: only flicks under 0.35 s used to
  // count, and an unhurried thumb missed about half the time on a T-Deck. A tap alone
  // doesn't unlock, so a touch in a pocket can't open it; it says how instead.
  static constexpr int UNLOCK_PX = 36;
  bool touch(const TouchEvent& e) override {
    switch (e.type) {
      case TouchEvent::Drag:
        _lift = constrain(e.y0 - e.y, 0, L::H / 2);
        return true;
      case TouchEvent::Up:
      case TouchEvent::Swipe: {
        const int up = e.y0 - e.y, side = abs(e.x - e.x0);
        _lift = 0;
        // Within ~50 degrees of straight up: a thumb arcs.
        if (up >= UNLOCK_PX && up * 5 >= side * 4) { nav.dismissToast(); nav.pop(); }
        return true;
      }
      case TouchEvent::Tap:
        hint();
        return true;
      default:
        return false;
    }
  }
private:
  void hint() {
    if (millis() - _hintAt < 3000) return;
    _hintAt = millis();
    nav.toast(BOARD_HAS_TOUCH ? "swipe up to unlock" : "press the wheel to unlock");
  }
  uint32_t _hintAt = 0;
  float _phase = 0, _scroll = 0;
  uint32_t _step = 0, _quipAt = 0;
  char _quip[96] = "";
  int _lift = 0;                     // px the finger has pulled the face up
};

#ifndef BOARD_HOME_DASHBOARD
#define BOARD_HOME_DASHBOARD 0
#endif
#if !BOARD_HOME_DASHBOARD   // a touchscreen board has its own home (e.g. src/tdeck/dashboard.cpp)
View* makeHomeView() { return new HomeView(); }
#endif
View* makeLockView() { return new LockView(); }
