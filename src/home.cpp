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
#include "motion.h"
#include "squatch_talk.h"
#include "regional.h"

#ifndef BOARD_SCENE_STEP_MS
#define BOARD_SCENE_STEP_MS 0   // board_pins.h: move the lock scene by time at the pager's pace (ms a step)
#endif
#if BOARD_SCENE_STEP_MS
// The pager's real ms per lock step; "scenestep N" over USB (dev builds) changes it live.
uint32_t g_sceneStepMs = BOARD_SCENE_STEP_MS;
#endif

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
  // What's already waiting when it locks isn't news to the sasquatch.
  LockView() : _wasPlugged(app::pluggedIn()), _seenUnread(app::unread()) {}
  bool isLock() override { return true; }
  void draw(Canvas& d) override {
    const Theme& t = nav.theme();
    drawStatusBar(d, t, false);          // the big clock below is the time here
    const bool hasUnread = app::unread() > 0;
    scenes::mascotLift() = _lift;         // mid-hop after a shake
    scenes::mascotPose() = pose();        // blinking, talking, waving...
    scenes::lockScene(d, t, t.style, _phase, _scroll, hasUnread, app::batteryPct(), app::unread(), _lx, _ly);
    scenes::mascotLift() = 0;
    drawLockSignal(d, t);
    scenes::mascotPose() = SquatchPose();
    drawTalk(d, t);
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
      d.drawString(sub, 140, 192);
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
    d.drawString(_quip, 8, 206);
  }
  void tick() override {
    // Animate only while someone is looking at it: not dimmed, not off.
    if (dimmer.asleep() || dimmer.dimmed()) return;
#if BOARD_SCENE_STEP_MS
    if (millis() - _step < 42) return;
#else
    if (millis() - _step < 33) return;
#endif
    const uint32_t was = _step;
    _step = millis();
    // How long the lock face had been dark (or dim): across lock screens, since a new
    // one is made each time it locks again.
    const uint32_t gap = litAt() ? _step - litAt() : 0xFFFFFFFFUL;
    litAt() = _step;
    if (!ui_settings.squatchQuiet) chatter(gap);
    // He blinks every few seconds, now and then twice.
    if ((int32_t)(_step - _blinkAt) >= (int32_t)BLINK_MS)
      _blinkAt = _step + (random(5) == 0 ? 250 : 2000 + random(4000));
    // A low battery shows: he trudges along at a tired pace.
    const float pace = tired() ? 0.6f : 1.0f;
#if BOARD_SCENE_STEP_MS
    // The pager moves the scene one step per lock frame, about every 42 ms, and that is
    // the pace it should have everywhere. A board that draws faster would walk faster
    // (the T-Deck at 33 ms: 27% quicker) and unevenly whenever a frame runs long, so here
    // it moves on by the time that passed, at the pager's rate. Capped, so coming back
    // from a pause doesn't jump.
    const float steps = was ? fminf(2.0f, (float)(_step - was) / g_sceneStepMs) : 1.0f;
#else
    const float steps = 1.0f;                // the pager: one step a frame (the reference)
#endif
    _phase += 0.32f * pace * steps;
    _scroll += 2.0f * pace * steps;
    if (_scroll > 10000.0f) _scroll = 0;
    // The scene leans with the pager (motion.h), eased so 25 samples a second draw
    // smoothly at 30 frames.
    float lx, ly;
    motion::lean(lx, ly);
    _lx += (lx - _lx) * 0.3f;
    _ly += (ly - _ly) * 0.3f;
    dirty = true;
  }
  // By default only a wheel press unlocks: keys and wheel turns happen in a pocket,
  // and any of them used to open the pager. Settings can allow any key again.
  void rotate(int) override { if (ui_settings.wheelUnlock) hint(); else nav.pop(); }
  void press() override { nav.pop(); }
  void key(char) override { if (ui_settings.wheelUnlock) hint(); else nav.pop(); }
  bool backspace() override { if (ui_settings.wheelUnlock) hint(); else nav.pop(); return true; }
private:
  void hint() {
    if (millis() - _hintAt < 3000) return;
    _hintAt = millis();
    nav.toast("press the wheel to unlock");
  }

  // ---- the sasquatch talks (squatch_talk.h) ------------------------------------------
  static constexpr uint32_t QUIET_MS = 6000, HOP_MS = 380, BLINK_MS = 120;

  // Start a line of kind k `delayMs` from now (n: how many messages, for MESSAGE). A
  // shake always gets its say; anything else waits for the bubble on screen and a
  // few quiet seconds after it.
  void say(talk::Kind k, uint32_t delayMs = 0, bool force = false, unsigned n = 0) {
    const uint32_t now = millis();
    if (!force && (_saying || (int32_t)(now - _quietUntil) < 0)) return;
    strlcpy(_say, talk::line(k, n), sizeof(_say));
    _sayKind = k;
    _sayMs = talk::sayMs(_say);
    _sayAt = now + delayMs;
    _saying = true;
    dirty = true;
  }

  static bool tired() { return app::batteryPct() < 15 && !app::pluggedIn(); }

  // How he looks this frame (mascot.h): blinking, tired, flailing mid-hop, and
  // whatever goes with the line he's saying.
  SquatchPose pose() const {
    SquatchPose p;
    const uint32_t now = millis();
    p.blink = (int32_t)(now - _blinkAt) >= 0 && now - _blinkAt < BLINK_MS;
    if (tired()) p.slump = 1;
    if (_saying && !ui_settings.squatchQuiet && (int32_t)(now - _sayAt) >= 0 && now - _sayAt < _sayMs)
      talk::pose(_sayKind, _say, now - _sayAt, _sayMs, p);
    if (_hops && _flail) p.armsUp = 1;
    return p;
  }

  static uint32_t& litAt() { static uint32_t v = 0; return v; }   // the lock face's last lit frame

  static int localHour() {
    if (!app::timeValid()) return 12;
    const time_t t = (time_t)app::now() + (time_t)regional::offsetMin(app::now()) * 60;
    struct tm tm;
    gmtime_r(&t, &tm);
    return tm.tm_hour;
  }

  // Once a frame while the lock face is lit. gap: how long it had been dark (or dim).
  void chatter(uint32_t gap) {
    const uint32_t now = millis();
    const uint16_t un = app::unread();
    const bool plugged = app::pluggedIn();
    if (gap > 3000) {
      // Just lit up. A message that woke it, a low battery, or - after a good while
      // dark (or now and then) - hello for the time of day. After the wake animation.
      if (un > _seenUnread) say(talk::MESSAGE, 400, false, un - _seenUnread);
      else if (app::batteryPct() < 15 && !plugged) say(talk::LOW_BATT, 400);
      else if (gap > 15UL * 60UL * 1000UL || random(4) == 0) {
        const int h = localHour();
        // Late runs 10 pm to 5 am: the small hours are late, not the middle of the day.
        say(h >= 5 && h < 11 ? talk::MORNING : h >= 11 && h < 17 ? talk::DAY : h >= 17 && h < 22 ? talk::EVENING : talk::LATE, 400);
      }
      _seenUnread = un;
      _wasPlugged = plugged;
      _leaning = false;
    }
    // Shaken: he says so, and hops - twice for a hard one. A third shake in a few
    // seconds gets the "okay, okay".
    float peak;
    if (motion::takeShake(peak)) {
      if (now - _shakeWinAt > 8000) { _shakes = 0; _shakeWinAt = now; }
      const bool again = ++_shakes >= 3;
      if (again) _shakes = 0;
      say(again ? talk::SHAKE_AGAIN : peak > 2.2f ? talk::SHAKE_HARD : talk::SHAKE, 0, true);
      _hopAt = now;
      _hops = peak > 2.2f ? 2 : 1;
      _flail = peak > 2.2f;               // a hard one throws his arms up
    }
    if (un > _seenUnread) say(talk::MESSAGE, 0, false, un - _seenUnread);
    _seenUnread = un;
    if (plugged && !_wasPlugged) say(talk::PLUG, 900);   // after the charging splash
    _wasPlugged = plugged;
    // Held tipped right over for 2 s (the scene sliding downhill): not just picked up.
    if (fabsf(_lx) > 0.95f || fabsf(_ly) > 0.95f) {
      if (!_leaning) { _leaning = true; _leanAt = now; }
      else if (now - _leanAt > 2000 && (int32_t)(now - _leanOkAt) >= 0) {
        say(talk::LEAN);
        _leanOkAt = now + 60000;
      }
    } else _leaning = false;
    // The hop: one (or two) quick arcs off the ground.
    if (_hops) {
      const uint32_t e = now - _hopAt;
      if (e >= HOP_MS * _hops) { _hops = 0; _lift = 0; }
      else _lift = (int)(14.0f * sinf(3.14159f * (float)(e % HOP_MS) / HOP_MS));
    }
    // The bubble ends; a few quiet seconds before the next unforced line.
    if (_saying && (int32_t)(now - (_sayAt + _sayMs)) >= 0) { _saying = false; _quietUntil = now + QUIET_MS; }
  }

  void drawTalk(Canvas& d, const Theme& t) {
    if (!_saying || ui_settings.squatchQuiet) return;
    const uint32_t now = millis();
    if ((int32_t)(now - _sayAt) < 0) return;                 // not started yet
    const uint32_t e = now - _sayAt;
    if (e >= _sayMs) return;
    int ax, ay;                                              // just above his head
    talk::anchor(t.style, _lift, ax, ay);
    const uint32_t pop = talk::POP_MS;
    const float grow = e < pop ? (float)e / pop : e > _sayMs - pop ? (float)(_sayMs - e) / pop : 1.0f;
    talk::bubble(d, t, ax, ay, _say, grow, talk::typed(_say, e));
  }

  uint32_t _hintAt = 0;
  float _phase = 0, _scroll = 0;
  float _lx = 0, _ly = 0;            // the lean drawn, eased toward motion::lean()
  uint32_t _step = 0, _quipAt = 0;
  char _quip[96] = "";
  // the talking sasquatch
  char _say[40] = "";
  talk::Kind _sayKind = talk::DAY;
  bool _saying = false, _leaning = false, _wasPlugged = false, _flail = false;
  uint32_t _sayAt = 0, _sayMs = 0, _quietUntil = 0, _shakeWinAt = 0, _hopAt = 0, _leanAt = 0, _leanOkAt = 0;
  uint32_t _blinkAt = millis() + 1500;
  uint16_t _seenUnread = 0;
  uint8_t _shakes = 0, _hops = 0;
  int _lift = 0;
};

View* makeHomeView() { return new HomeView(); }
View* makeLockView() { return new LockView(); }
