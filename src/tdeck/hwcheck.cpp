#include "hwcheck.h"
#include "app.h"
#include "node.h"
#include "gps.h"
#include "dataio.h"
#include "keyboard.h"
#include "rotary.h"
#include "touch_gt911.h"

extern Keyboard keyboard;          // main.cpp
extern const char* radio_chip;     // variants/inw_tdeck/target.cpp: "SX1262", or "none"
void markUiDirty();                // main.cpp

namespace {

// ---- hardware check ------------------------------------------------------------

enum Dot : uint8_t { OK, WAIT, BAD, INFO };

class HardwareCheckView : public View {
public:
  void draw(Canvas& g) override {
    const Theme& t = nav.theme();
    drawHeader(g, "Hardware check", "live");
    g.setFont(&fonts::Font2);
    int y = L::BODY_Y + 3;
    char b[64];
    auto row = [&](Dot d, const char* label, const char* value) {
      g.fillCircle(9, y + 8, 4, d == OK ? t.green : d == WAIT ? t.amber : d == BAD ? t.red : t.line);
      g.setTextColor(t.dim, t.bg);
      g.drawString(label, 20, y);
      g.setTextColor(d == BAD ? t.red : d == WAIT ? t.amber : t.txt, t.bg);
      g.drawString(value, 90, y);
      y += ROW;
    };

    if (!touchPanel.ok()) row(BAD, "touch", "not found");
    else if (!touchPanel.touches()) {
      snprintf(b, sizeof(b), "GT911 0x%02X - touch the screen", touchPanel.address()); row(WAIT, "touch", b);
    } else {
      snprintf(b, sizeof(b), "GT911 0x%02X, %lu touches", touchPanel.address(), (unsigned long)touchPanel.touches());
      row(OK, "touch", b);
    }
    if (_touched) snprintf(b, sizeof(b), "%s %d,%d  raw %u,%u", _fDown ? "at" : "was", _fx, _fy, touchPanel.rawX(), touchPanel.rawY());
    else snprintf(b, sizeof(b), "reports %u x %u", touchPanel.resX(), touchPanel.resY());
    row(INFO, "  finger", b);

    uint32_t n[4];
    tdeck_tb::counts(n);
    const int dirs = (n[0] > 0) + (n[1] > 0) + (n[2] > 0) + (n[3] > 0);
    snprintf(b, sizeof(b), "U%lu D%lu L%lu R%lu  click %u%s", (unsigned long)n[0], (unsigned long)n[1],
             (unsigned long)n[2], (unsigned long)n[3], _clicks, digitalRead(PIN_TB_CLICK) == LOW ? " *" : "");
    row(dirs == 4 && _clicks ? OK : WAIT, "trackball", dirs || _clicks ? b : "roll it each way, click");

    const uint8_t k = keyboard.lastKey();
    if (!keyboard.ok()) row(BAD, "keyboard", "not found");
    else if (!k) row(WAIT, "keyboard", "type any key");
    else {
      if (k >= 0x21 && k < 0x7F) snprintf(b, sizeof(b), "last key  %c  (0x%02X)", k, k);
      else snprintf(b, sizeof(b), "last key  %s  (0x%02X)", k == ' ' ? "space" : k == 0x08 ? "del" : k == 0x0D ? "enter" : "?", k);
      row(OK, "keyboard", b);
    }

    if (radio_chip[0] == 'n') row(BAD, "radio", "not found");
    else {
      if (g_node) snprintf(b, sizeof(b), "%s  %.3f MHz  sf%u", radio_chip, g_node->prefs().freq, g_node->prefs().sf);
      else snprintf(b, sizeof(b), "%s", radio_chip);
      row(OK, "radio", b);
    }

    const GpsFix& f = gps.fix();
    if (!ui_settings.gpsOn) row(INFO, "gps", "off (Settings > GPS)");
    else if (!gps.bytesRead) row(WAIT, "gps", "not heard (the Plus has one)");
    else if (!gps.goodSentences) { snprintf(b, sizeof(b), "data but garbled, trying %lu baud", (unsigned long)gps.baud()); row(WAIT, "gps", b); }
    else if (gps.hasFix()) { snprintf(b, sizeof(b), "fix, %u satellites", f.satellites); row(OK, "gps", b); }
    else if (gps.heard()) { snprintf(b, sizeof(b), "talking, hears %u sats, no fix yet", gps.heard()); row(WAIT, "gps", b); }
    else row(WAIT, "gps", "talking, hears 0 sats - needs sky");

    const uint16_t mv = app::batteryMv();
    if (mv < 2800) { snprintf(b, sizeof(b), "none? (%u mV)", mv); row(BAD, "battery", b); }
    else { snprintf(b, sizeof(b), "%u mV  %u%%%s", mv, app::batteryPct(), app::charging() ? "  charging" : ""); row(OK, "battery", b); }

    if (sdMounted()) { snprintf(b, sizeof(b), "%llu MB free", (unsigned long long)(sdFreeBytes() / 1048576ULL)); row(OK, "sd card", b); }
    else row(INFO, "sd card", "none in");

    if (ESP.getPsramSize()) {
      snprintf(b, sizeof(b), "psram %u MB  heap %u kB free", (unsigned)(ESP.getPsramSize() >> 20), (unsigned)(ESP.getFreeHeap() / 1024));
      row(OK, "memory", b);
    } else row(BAD, "memory", "no psram");

    const uint8_t o = ui_settings.orient;
    if (!o) row(INFO, "screen", "as built");
    else {
      snprintf(b, sizeof(b), "%s%s%s%s%s", o & 1 ? "flipped " : "", o & 2 ? "touch-LR " : "", o & 4 ? "touch-UD " : "",
               o & 8 ? "ball-rev " : "", o & 16 ? "inverted" : "");
      row(INFO, "screen", b);
    }
  }

  void tick() override {
    if (millis() - _at >= 150) { _at = millis(); dirty = true; }
  }
  void press() override { _clicks++; dirty = true; }
  // Letters are for testing the keyboard here, not shortcuts. Del still goes back.
  bool wantsAllKeys() override { return true; }
  void key(char) override { dirty = true; }
  bool coasts() override { return false; }
  bool touch(const TouchEvent& e) override {                // shown, not acted on: nothing to open here
    _touched = true;
    _fx = e.x; _fy = e.y;
    _fDown = e.type == TouchEvent::Down || e.type == TouchEvent::Drag || e.type == TouchEvent::LongPress;
    return true;
  }

private:
  static constexpr int ROW = 17;
  uint32_t _at = 0;
  uint16_t _clicks = 0;
  bool _touched = false, _fDown = false;
  int16_t _fx = 0, _fy = 0;
};

// ---- touch test ------------------------------------------------------------------

class TouchTestView : public View {
public:
  void draw(Canvas& g) override {
    const Theme& t = nav.theme();
    drawHeader(g, "Touch test", _next < 4 ? "tap each ring" : nullptr);
    for (int gx = 40; gx < L::W; gx += 40) g.drawFastVLine(gx, L::BODY_Y, L::H - L::BODY_Y, t.panel);
    for (int gy = L::BODY_Y + 40; gy < L::H; gy += 40) g.drawFastHLine(0, gy, L::W, t.panel);

    for (int i = 0; i < 4; i++) {
      const P r = ring(i);
      const bool done = i < _next, now = i == _next;
      const uint16_t c = done ? t.greenDim : now ? t.green : t.line;
      g.drawCircle(r.x, r.y, 14, c);
      g.drawCircle(r.x, r.y, 13, c);
      if (now) g.fillCircle(r.x, r.y, 4, t.green);
      if (done) g.fillCircle(_hit[i].x, _hit[i].y, 3, t.amber);   // where that tap landed
    }

    for (int i = 0; i < _n; i++) {
      const P& p = _trail[(_head - _n + i + TRAIL) % TRAIL];
      g.fillCircle(p.x, p.y, 2, i == _n - 1 ? t.txt : t.greenDim);
    }
    if (_down) {
      g.drawFastHLine(0, _y, L::W, t.greenDim);
      g.drawFastVLine(_x, L::BODY_Y, L::H - L::BODY_Y, t.greenDim);
      g.drawCircle(_x, _y, 10, t.green);
    }

    g.setFont(&fonts::Font2);
    const int cy = L::BODY_Y + (L::H - L::BODY_Y) / 2;
    // The words sit on a card of their own, so the trail can't run through them.
    g.fillRoundRect(8, cy - 28, L::W - 16, 66, 8, t.bg);   // the rings are above and below it
    g.drawRoundRect(8, cy - 28, L::W - 16, 66, 8, t.line);
    char b[64];
    const char* line1;
    uint16_t c1 = t.txt;
    if (_next < 4) { snprintf(b, sizeof(b), "tap the bright ring  (%d of 4)", _next + 1); line1 = b; }
    else { line1 = _verdict; c1 = _verdictColour; }
    g.setTextColor(c1, t.bg);
    g.drawString(line1, (L::W - g.textWidth(line1)) / 2, cy - 22);
    g.setTextColor(t.dim, t.bg);
    if (_n) snprintf(b, sizeof(b), "x %d  y %d    raw %u, %u", _x, _y, touchPanel.rawX(), touchPanel.rawY());
    else snprintf(b, sizeof(b), "draw anywhere: the dots follow your finger");
    g.drawString(b, (L::W - g.textWidth(b)) / 2, cy - 2);
    const char* hint = _next < 4 ? "click the ball to start over" : "tap anywhere or click the ball to go again";
    g.drawString(hint, (L::W - g.textWidth(hint)) / 2, cy + 18);
  }

  bool coasts() override { return false; }
  bool touch(const TouchEvent& e) override {
    switch (e.type) {
      case TouchEvent::Down:
      case TouchEvent::Drag:
        _down = true; _x = e.x; _y = e.y;
        _trail[_head] = {e.x, e.y}; _head = (_head + 1) % TRAIL; if (_n < TRAIL) _n++;
        break;
      case TouchEvent::Up:
        _down = false;
        break;
      case TouchEvent::Tap:
        _down = false; _x = e.x; _y = e.y;
        if (_next < 4) { _hit[_next++] = {e.x, e.y}; if (_next == 4) judge(); }
        else restart();
        break;
      default: break;
    }
    return true;
  }
  void press() override { restart(); dirty = true; }

private:
  struct P { int16_t x, y; };
  static constexpr int INSET = 24, TRAIL = 60;

  // Clockwise from the top left, clear of the header.
  P ring(int i) const {
    const int16_t l = INSET, r = L::W - INSET, top = L::BODY_Y + INSET, bot = L::H - INSET;
    return i == 0 ? P{l, top} : i == 1 ? P{r, top} : i == 2 ? P{r, bot} : P{l, bot};
  }

  void restart() { _next = 0; _n = 0; _head = 0; _down = false; }

  // Only which way the taps went counts, not how close they were - sloppy taps
  // still say which side is which.
  void judge() {
    const int across = (_hit[1].x - _hit[0].x) + (_hit[2].x - _hit[3].x);   // left to right: should grow
    const int downY  = (_hit[3].y - _hit[0].y) + (_hit[2].y - _hit[1].y);   // top to bottom: should grow
    const int xWithDown = abs((_hit[3].x - _hit[0].x) + (_hit[2].x - _hit[1].x));
    const int yWithAcross = abs((_hit[1].y - _hit[0].y) + (_hit[2].y - _hit[3].y));
    const Theme& t = nav.theme();
    if (xWithDown > abs(across) && yWithAcross > abs(downY)) {
      _verdict = "touch axes look swapped: please report it";
      _verdictColour = t.red;
      return;
    }
    uint8_t fix = 0;
    if (across < 0) fix |= 2;
    if (downY < 0) fix |= 4;
    if (!fix) {
      int err = 0;
      for (int i = 0; i < 4; i++) { const P r = ring(i); err += abs(_hit[i].x - r.x) + abs(_hit[i].y - r.y); }
      _verdict = err / 4 > 40 ? "right way round, but off: please report it" : "touch lines up with the screen";
      _verdictColour = err / 4 > 40 ? t.amber : t.green;
      return;
    }
    ui_settings.orient ^= fix;
    app::applyDisplay();
    markUiDirty();
    _verdict = fix == 6 ? "touch was upside down: fixed, test again" :
               fix == 2 ? "touch was mirrored left-right: fixed, test again" :
                          "touch was mirrored up-down: fixed, test again";
    _verdictColour = t.amber;
  }

  P _hit[4] = {};
  P _trail[TRAIL] = {};
  int _next = 0, _n = 0, _head = 0;
  bool _down = false;
  int16_t _x = 0, _y = 0;
  const char* _verdict = "";
  uint16_t _verdictColour = 0;
};

}  // namespace

void openHardwareCheck() { nav.push(new HardwareCheckView()); }
void openTouchTest() { nav.push(new TouchTestView()); }
