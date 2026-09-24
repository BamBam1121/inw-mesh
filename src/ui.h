// View stack over a single PSRAM canvas. The home carousel is the root; every
// screen it opens is pushed on top and Backspace pops it. A view only redraws
// when marked dirty, since the panel shares its bus with the radio and SD.

#pragma once
#include <Arduino.h>
#include <functional>
#include <vector>
#include "display_config.h"
#include "theme.h"

using Canvas = lgfx::LGFX_Sprite;

namespace fx { bool active(); }       // fx.h: effects running keep the screen redrawing

// Layout constants shared by every screen.
namespace L {
  constexpr int W = 480, H = 222;
  constexpr int STATUS_H = 18;          // status bar
  constexpr int HEAD_Y = 18, HEAD_H = 24;
  constexpr int BODY_Y = 42;            // first content pixel under a header
  constexpr int ROW_H = 20;
}

class View {
public:
  virtual ~View() {}
  // Draw everything below the status bar. The canvas is cleared to bg first.
  virtual void draw(Canvas& g) = 0;
  virtual void rotate(int d) {}
  virtual void press() {}
  // Backspace. Return true if handled (e.g. deleted a character); false pops.
  virtual bool backspace() { return false; }
  virtual void key(char c) {}
  virtual void tick() {}                 // every loop, set dirty when needed
  virtual void resume() { dirty = true; } // revealed again after a child popped
  // Views that draw straight to the panel (the carousel) override this.
  virtual bool customRender() { return false; }
  virtual void render(bool full) {}
  virtual bool isHome() { return false; }
  virtual bool isLock() { return false; }
  virtual bool wantsAllKeys() { return false; }  // don't treat letters as shortcuts
  bool dirty = true;
};

class Nav {
public:
  void begin(LGFX* d, Theme* t);
  void push(View* v);
  void pop();                      // deletes the view
  void popTo(View* v);             // pop until v is on top
  void popToHome();
  View* top() { return _depth ? _stack[_depth - 1] : nullptr; }
  int depth() const { return _depth; }
  void replaceTop(View* v);

  void toast(const char* msg, uint16_t ms = 2500);
  // Puts a notice on screen right away, before a slow blocking job starts.
  void busy(const char* msg) { toast(msg, 60000); draw(); }
  void banner(const char* title, const char* text, uint16_t ms = 4500);
  void invalidate() { if (top()) top()->dirty = true; _statusDirty = true; }
  void statusChanged() { _statusDirty = true; }

  void rotate(int d)  { if (top()) top()->rotate(d); }
  void press()        { if (top()) top()->press(); }
  void key(char c)    { if (top()) top()->key(c); }
  void backspace();
  void tick();
  void draw();
  // Draw the top view into the canvas without sending it to the panel, for a
  // transition to reveal (or collapse) the real picture.
  void compose();

  Canvas& canvas() { return _canvas; }
  LGFX* display() { return _d; }
  Theme& theme() { return *_t; }
  // Overlays (toast, banner) painted on top of whatever the view drew.
  void drawOverlays(lgfx::LovyanGFX& g);
  bool overlayActive() const { return _toastUntil || _bannerUntil || fx::active(); }

private:
  LGFX* _d = nullptr;
  Theme* _t = nullptr;
  Canvas _canvas;
  View* _stack[12] = {nullptr};
  int _depth = 0;
  char _toast[64] = "";
  uint32_t _toastUntil = 0;
  char _bannerTitle[48] = "", _bannerText[128] = "";
  uint32_t _bannerUntil = 0;
  bool _statusDirty = true;
  uint32_t _lastStatus = 0;
};

extern Nav nav;

// ---- drawing helpers ----------------------------------------------------------
void drawHeader(Canvas& g, const char* title, const char* right = nullptr);
void drawScrollbar(Canvas& g, int total, int first, int visible, int y0, int h);
void drawPill(Canvas& g, int x, int y, int w, int h, uint16_t bg, uint16_t fg, const char* text);
void drawToggle(Canvas& g, int x, int y, bool on);
// Make message text safe for the bitmap font: UTF-8 punctuation to ASCII,
// emoji and anything unmappable to a single marker.
void sanitize(const char* in, char* out, size_t cap);
// Word-wrap `text` into lines no wider than `w` pixels in the current font.
// Returns the number of lines; each line is a (start, len) pair into `text`.
// Rich text: sanitized strings may hold 2-byte emoji markers; these draw and
// measure them as 16 px colour glyphs. drawUtf8/widthUtf8 sanitize first.
int  richWidth(lgfx::LovyanGFX& g, const char* s, int n = -1);
int  drawRich(lgfx::LovyanGFX& g, const char* s, int x, int y, int n = -1);
void richFit(lgfx::LovyanGFX& g, char* s, int maxW);
int  drawUtf8(lgfx::LovyanGFX& g, const char* utf8, int x, int y, int maxW = 0);
int  widthUtf8(lgfx::LovyanGFX& g, const char* utf8);
void drawEmojiGlyph(lgfx::LovyanGFX& g, int idx, int x, int y, int scale = 1);
uint16_t emojiCount();
uint32_t emojiCodepoint(uint16_t i);
int emojiFind(uint32_t cp);
int wrapText(Canvas& g, const char* text, int w, uint16_t* starts, uint8_t* lens, int maxLines);
const char* timeAgo(uint32_t epoch);
const char* clockText(uint32_t epoch, bool withDate = false);
uint16_t nameColor(const char* name);      // stable per-name avatar colour
void drawAvatar(Canvas& g, int cx, int cy, int r, const char* name, uint8_t kind);

// ---- generic menu --------------------------------------------------------------
enum class RowKind : uint8_t { Action, Value, Toggle, Submenu, Adjust, Info, Header };

struct MenuRow {
  RowKind kind;
  String label;
  std::function<String()> value;          // Value/Adjust/Info right column
  std::function<bool()> toggled;          // Toggle state
  std::function<void()> onPress;
  std::function<void(int)> onAdjust;      // Adjust: rotate while editing
};

class MenuView : public View {
public:
  explicit MenuView(const String& title) : _title(title) {}
  MenuView& header(const String& label);
  MenuView& action(const String& label, std::function<void()> fn);
  MenuView& submenu(const String& label, std::function<void()> fn, std::function<String()> value = nullptr);
  MenuView& value(const String& label, std::function<String()> v, std::function<void()> fn = nullptr);
  MenuView& toggle(const String& label, std::function<bool()> get, std::function<void()> flip);
  MenuView& adjust(const String& label, std::function<String()> v, std::function<void(int)> step);
  MenuView& info(const String& label, std::function<String()> v);
  void clear() { _rows.clear(); _focus = 0; _scroll = 0; }

  void draw(Canvas& g) override;
  void rotate(int d) override;
  void press() override;
  bool backspace() override;
  void key(char c) override;
  void tick() override;
  String title() const { return _title; }
  void setTitle(const String& t) { _title = t; dirty = true; }
  // Rebuild hook: called on resume so rows reflect state changed by children.
  std::function<void(MenuView&)> rebuild;
  void resume() override;
  uint32_t refreshMs = 1000;              // live values refresh this often

protected:
  bool focusable(int i) const { return _rows[i].kind != RowKind::Header && _rows[i].kind != RowKind::Info; }
  void moveFocus(int d);
  String _title;
  std::vector<MenuRow> _rows;
  int _focus = 0, _scroll = 0;
  int _drawnFocus = -1;                   // the view follows the focus only when it moves
  bool _editing = false;
  uint32_t _lastRefresh = 0;
};

// ---- text prompt ------------------------------------------------------------------
class PromptView : public View {
public:
  PromptView(const String& title, const String& hint, const String& initial, size_t maxLen,
             std::function<void(const String&)> done, bool secret = false)
    : _title(title), _hint(hint), _buf(initial), _maxLen(maxLen), _done(done), _secret(secret) {}
  void draw(Canvas& g) override;
  void key(char c) override;
  bool backspace() override;
  void press() override { commit(); }
  void tick() override;
  bool wantsAllKeys() override { return true; }
private:
  void commit();
  String _title, _hint, _buf;
  size_t _maxLen;
  std::function<void(const String&)> _done;
  bool _secret, _caret = true;
  uint32_t _blink = 0;
};

// ---- confirm ------------------------------------------------------------------------
class ConfirmView : public View {
public:
  ConfirmView(const String& q, const String& detail, std::function<void()> yes)
    : _q(q), _detail(detail), _yes(yes) {}
  void draw(Canvas& g) override;
  void rotate(int d) override { _sel = !_sel; dirty = true; }
  void press() override;
  void key(char c) override { if (c == 'y') { _sel = true; press(); } else if (c == 'n') { _sel = false; press(); } }
private:
  String _q, _detail;
  std::function<void()> _yes;
  bool _sel = false;               // cursor starts on "no"
};

// ---- a scrollable page of text lines (logs, info, telemetry) -----------------------
class TextPageView : public View {
public:
  TextPageView(const String& title, std::function<void(std::vector<String>&)> fill, uint32_t refreshMs = 1000)
    : _title(title), _fill(fill), _refresh(refreshMs) {}
  void draw(Canvas& g) override;
  void rotate(int d) override;
  void tick() override;
  std::function<void()> onPress;
  void press() override { if (onPress) onPress(); }
private:
  String _title;
  std::function<void(std::vector<String>&)> _fill;
  std::vector<String> _lines;
  uint32_t _refresh, _last = 0;
  int _scroll = 0;
  bool _stickBottom = false;
public:
  void stickToBottom() { _stickBottom = true; }
};

// Convenience
inline void confirm(const String& q, const String& detail, std::function<void()> yes) {
  nav.push(new ConfirmView(q, detail, yes));
}
inline void prompt(const String& title, const String& hint, const String& initial, size_t maxLen,
                   std::function<void(const String&)> done, bool secret = false) {
  nav.push(new PromptView(title, hint, initial, maxLen, done, secret));
}
