#include "app.h"
#include "carousel.h"
#include "mascot.h"
#include "node.h"
#include "history.h"
#include "gps.h"
#include "backlight.h"

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
static const char* subMap() { return gps.hasFix() ? "gps fix" : (ui_settings.gpsOn ? "searching" : "gps off"); }
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
  if (!g_node) return "radio not responding";
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
  static constexpr int GROUND_Y = 170;
  bool isLock() override { return true; }
  void draw(Canvas& d) override {
    const Theme& t = nav.theme();
    drawStatusBar(d, t);
    d.fillTriangle(0, 120, 90, 66, 160, 120, t.line);
    d.fillTriangle(120, 124, 230, 58, 340, 124, t.line);
    d.fillTriangle(300, 120, 400, 72, 480, 120, t.line);
    const int span = L::W + 60;
    for (int i = 0; i < 14; i++) {
      int px = (int)(i * 46 - _scroll);
      px = ((px % span) + span) % span - 30;
      if (px > 200 && px < 300) continue;
      const int ph = 20 + (i % 3) * 7;
      d.fillTriangle(px, GROUND_Y - 4, px + 9, GROUND_Y - 4 - ph, px + 18, GROUND_Y - 4, t.greenDim);
    }
    d.drawFastHLine(0, GROUND_Y, L::W, t.greenDim);
    drawSasquatch(d, t, 250, GROUND_Y, 88, _phase, app::unread() ? t.amber : t.green);

    d.setFont(&fonts::Font4);
    d.setTextColor(t.green, t.bg);
    d.drawString(app::timeValid() ? clockText(app::now()) : "--:--", 8, 182);
    d.setFont(&fonts::Font2);
    d.setTextColor(t.dim, t.bg);
    char sub[64];
    const uint16_t un = app::unread();
    if (un) snprintf(sub, sizeof(sub), "%u unread message%s", un, un == 1 ? "" : "s");
    else if (!g_node) snprintf(sub, sizeof(sub), "radio down");
    else snprintf(sub, sizeof(sub), "%d nodes  //  mesh listening", g_node->getNumContacts());
    d.drawString(sub, 140, 192);
    if (app::timeValid()) {
      const char* date = clockText(app::now(), true);
      d.drawString(date, L::W - 8 - d.textWidth(date), 192);
    }
    d.setTextColor(t.greenDim, t.bg);
    d.drawString("any key", L::W - 60, 206);
  }
  void tick() override {
    // Animate only while someone can see it.
    if (dimmer.asleep()) return;
    if (millis() - _step < 33) return;
    _step = millis();
    _phase += 0.32f;
    _scroll += 2.0f;
    if (_scroll > 10000.0f) _scroll = 0;
    dirty = true;
  }
  void rotate(int) override { nav.pop(); }
  void press() override { nav.pop(); }
  void key(char) override { nav.pop(); }
  bool backspace() override { nav.pop(); return true; }
private:
  float _phase = 0, _scroll = 0;
  uint32_t _step = 0;
};

View* makeHomeView() { return new HomeView(); }
View* makeLockView() { return new LockView(); }
