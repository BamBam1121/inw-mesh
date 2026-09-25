// Chat list and conversation thread.

#include "app.h"
#include "node.h"
#include "history.h"
#include "notify.h"
#include "fx.h"

ConvKey g_openConv;                 // the thread on screen, for notification muting

// ---- helpers ------------------------------------------------------------------------
static bool channelName(const ConvKey& k, char* out, size_t cap, int* idxOut = nullptr) {
  if (!g_node) return false;
  const int idx = g_node->findChannelBySecret(k.id);
  if (idx < 0) return false;
  ChannelDetails ch;
  g_node->getChannel(idx, ch);
  strlcpy(out, ch.name, cap);
  if (idxOut) *idxOut = idx;
  return true;
}

static void convName(const ConvKey& k, char* out, size_t cap, uint8_t* kind = nullptr) {
  if (k.type == CONV_CHANNEL) {
    if (kind) *kind = 10;
    if (!channelName(k, out, cap)) strlcpy(out, "(removed channel)", cap);
    return;
  }
  ContactInfo* c = g_node ? g_node->contactByPrefix(k.id, 6) : nullptr;
  if (kind) *kind = c ? c->type : 1;
  if (c) { strlcpy(out, c->name, cap); return; }
  HistMsg* m = history.last(k);
  if (m && !(m->flags & HF_OUT) && m->sender[0]) strlcpy(out, m->sender, cap);
  else snprintf(out, cap, "%02x%02x%02x%02x", k.id[0], k.id[1], k.id[2], k.id[3]);
}

// ---- quick replies / message actions -----------------------------------------------------
class ThreadView;
static void openQuickReplies(ThreadView* t);
static void openEmojiPicker(ThreadView* t);
static void openMessageActions(ThreadView* t, uint32_t id);

// ---- thread ---------------------------------------------------------------------------------
class ThreadView : public View {
public:
  explicit ThreadView(const ConvKey& k) : _key(k) {
    g_openConv = k;
    // Where "new" starts. Captured before marking read, and kept while the
    // thread stays open, so the line doesn't vanish the moment you look.
    if (history.unread(k)) _newAfter = history.readMark(k);
    history.markRead(k);
    refresh();
    // A room server only talks to logged-in clients; try the guest login once.
    ContactInfo* c = contact();
    if (c && c->type == ADV_TYPE_ROOM && g_node->loginState(c->id.pub_key) == 0)
      g_node->login(c->id.pub_key, "");
  }
  ~ThreadView() override { if (g_openConv == _key) g_openConv = ConvKey(); }
  bool wantsAllKeys() override { return true; }

  ContactInfo* contact() {
    return (_key.type == CONV_CONTACT && g_node) ? g_node->contactByPrefix(_key.id, 6) : nullptr;
  }

  void resume() override { g_openConv = _key; refresh(); dirty = true; }

  void refresh() {
    _n = history.collect(_key, _ids, MAX_IDS);
    _gen = history.gen;
    if (_sel >= _n) _sel = _n - 1;
    history.markRead(_key);
    _gen = history.gen;
    noticeChanges();
  }

  void tick() override {
    if (history.gen != _gen) { refresh(); dirty = true; }
    if (millis() - _blinkAt > 530) { _blinkAt = millis(); _caret = !_caret; if (_sel < 0) dirty = true; }
    if (_revealId && millis() - _revealAt < REVEAL_MS + 60) dirty = true;   // the decrypt effect
  }

  void setCompose(const String& s) { _compose = s; _sel = -1; dirty = true; }
  const String& compose() const { return _compose; }
  const ConvKey& key() const { return _key; }

  void key(char c) override {
    if (c == '\n') { send(); return; }
    if ((uint8_t)c < 0x20 || (uint8_t)c > 0x7E) return;
    if ((int)_compose.length() >= maxLen()) return;
    _compose += c;
    _sel = -1;
    dirty = true;
  }

  bool backspace() override {
    if (_sel >= 0) { _sel = -1; dirty = true; return true; }
    if (!_compose.length()) return false;
    // Drop a whole UTF-8 character (an emoji is 4 bytes), not one byte of it.
    int n = _compose.length() - 1;
    while (n > 0 && ((uint8_t)_compose[n] & 0xC0) == 0x80) n--;
    _compose.remove(n);
    dirty = true;
    return true;
  }

  void rotate(int d) override {
    if (!_n) return;
    // Up (negative) walks back through history, down returns to composing.
    int s = _sel < 0 ? _n : _sel;
    s += d;
    if (s >= _n) _sel = -1;
    else _sel = max(0, s);
    dirty = true;
  }

  void press() override {
    if (_sel >= 0 && _sel < _n) { openMessageActions(this, _ids[_sel]); return; }
    openEmojiPicker(this);
  }

  int maxLen() const {
    // MeshCore text payload is 160 bytes; channel posts also carry "<name>: ".
    if (_key.type == CONV_CHANNEL && g_node) return max(40, 150 - (int)strlen(g_node->name()));
    return 150;
  }

  void send() {
    if (!_compose.length() || !g_node) return;
    char text[168];
    strlcpy(text, _compose.c_str(), sizeof(text));
    if (_key.type == CONV_CHANNEL) {
      const int idx = g_node->findChannelBySecret(_key.id);
      if (idx < 0) { nav.toast("channel no longer exists"); return; }
      const uint32_t id = history.add(_key, HF_OUT, ST_SENDING, g_node->name(), text, app::now());
      g_node->sendChannel(idx, text, id);
    } else {
      ContactInfo* c = contact();
      if (!c) { nav.toast("contact no longer exists"); return; }
      uint8_t pub[32];
      memcpy(pub, c->id.pub_key, 32);
      const uint32_t id = history.add(_key, HF_OUT | (c->type == ADV_TYPE_ROOM ? HF_ROOM : 0), ST_SENDING,
                                      g_node->name(), text, app::now());
      g_node->sendDM(pub, text, id);
    }
    _compose = "";
    _sel = -1;
    fx::burst(L::W - 40, L::H - 14);                  // off it goes, in the theme's style
    refresh();
    dirty = true;
  }

  void resend(uint32_t id) {
    HistMsg* m = history.find(id);
    if (!m || !g_node) return;
    char text[168];
    strlcpy(text, m->text, sizeof(text));
    const uint32_t nid = history.add(_key, m->flags, ST_SENDING, g_node->name(), text, app::now());
    if (_key.type == CONV_CHANNEL) {
      const int idx = g_node->findChannelBySecret(_key.id);
      if (idx >= 0) g_node->sendChannel(idx, text, nid);
    } else if (ContactInfo* c = contact()) {
      uint8_t pub[32];
      memcpy(pub, c->id.pub_key, 32);
      g_node->sendDM(pub, text, nid);
    }
    _sel = -1;
    refresh();
  }

  void draw(Canvas& g) override {
    const Theme& t = nav.theme();
    char title[40];
    uint8_t kind = 1;
    convName(_key, title, sizeof(title), &kind);
    char sub[40] = "";
    if (_key.type == CONV_CHANNEL) {
      snprintf(sub, sizeof(sub), "%u messages", _n);
    } else if (ContactInfo* c = contact()) {
      if (c->type == ADV_TYPE_ROOM) {
        const uint8_t ls = g_node->loginState(c->id.pub_key);
        snprintf(sub, sizeof(sub), "room  %s", ls == 2 ? "logged in" : ls == 1 ? "logging in" : ls == 3 ? "login failed" : "");
      } else if (c->out_path_len == OUT_PATH_UNKNOWN) snprintf(sub, sizeof(sub), "flood  %s", timeAgo(c->lastmod));
      else if ((c->out_path_len & 63) == 0) snprintf(sub, sizeof(sub), "direct  %s", timeAgo(c->lastmod));
      else snprintf(sub, sizeof(sub), "%u hops  %s", c->out_path_len & 63, timeAgo(c->lastmod));
    }
    drawHeader(g, title, sub);

    const int top = L::BODY_Y + 2, bottom = L::H - 28;
    g.setClipRect(0, top, L::W, bottom - top);
    if (!_n) {
      g.setTextColor(t.dim, t.bg);
      g.drawString(_key.type == CONV_CHANNEL ? "no messages yet - say something" : "no messages yet", 14, top + 10);
    }
    // Bottom-up: the anchor is the selected message, or the newest.
    const int anchor = _sel >= 0 ? _sel : _n - 1;
    int y = bottom - 2;
    _posN = 0;
    // If a selection is scrolled up, keep a little of the next message visible.
    for (int i = anchor; i >= 0 && y > top; i--) {
      HistMsg* m = history.find(_ids[i]);
      if (!m) continue;
      y -= ui_settings.compactChat ? drawCompact(g, *m, y, i == _sel) : drawBubble(g, *m, y, i == _sel);
      y -= 4;
      // Red line above the first message that was unread when the thread opened.
      if (_newAfter != NO_DIVIDER && _ids[i] > _newAfter && (i == 0 || _ids[i - 1] <= _newAfter)) {
        y -= drawNewDivider(g, y);
      }
    }
    g.clearClipRect();
    if (_sel >= 0 && _sel < _n - 1) {
      char more[24];
      snprintf(more, sizeof(more), "%d newer", _n - 1 - _sel);
      drawPill(g, L::W - 90, bottom - 22, 80, 18, t.greenDim, t.white, more);
    }
    drawCompose(g, bottom);
  }

private:
  static constexpr uint32_t NO_DIVIDER = 0xFFFFFFFF;
  uint32_t _newAfter = NO_DIVIDER;

  int drawNewDivider(Canvas& g, int bottomY) {
    const Theme& t = nav.theme();
    const int h = 16, y = bottomY - h / 2 - 2;
    g.setFont(&fonts::Font0);
    const char* label = "NEW";
    const int lw = g.textWidth(label) + 10, cx = L::W / 2;
    g.drawFastHLine(8, y, cx - lw / 2 - 12, t.red);
    g.drawFastHLine(8, y + 1, cx - lw / 2 - 12, t.red);
    g.drawFastHLine(cx + lw / 2 + 4, y, L::W - 8 - (cx + lw / 2 + 4), t.red);
    g.drawFastHLine(cx + lw / 2 + 4, y + 1, L::W - 8 - (cx + lw / 2 + 4), t.red);
    g.fillRoundRect(cx - lw / 2 - 4, y - 5, lw + 4, 12, 5, t.red);
    g.setTextColor(t.white, t.red);
    g.drawString(label, cx - lw / 2 + 3, y - 3);
    g.setFont(&fonts::Font2);
    return h;
  }

  // Returns the height used; draws with its bottom edge at `bottomY`.
  int drawBubble(Canvas& g, const HistMsg& m, int bottomY, bool selected) {
    const Theme& t = nav.theme();
    const bool out = m.flags & HF_OUT;
    const bool showName = !out && (_key.type == CONV_CHANNEL || (m.flags & HF_ROOM));
    char text[200];
    sanitize(m.text, text, sizeof(text));
    uint16_t st[12]; uint8_t ln[12];
    const int maxW = 320;
    g.setFont(&fonts::Font2);
    const int nl = wrapText(g, text, maxW - 16, st, ln, 12);
    int textW = 0;
    for (int i = 0; i < nl; i++) textW = max(textW, richWidth(g, text + st[i], ln[i]));
    char meta[48];
    metaText(m, meta, sizeof(meta));
    char name[28] = "";
    if (showName) sanitize(m.sender, name, sizeof(name));
    const int metaW = g.textWidth(meta);
    const int nameW = showName ? richWidth(g, name) : 0;
    const int w = min(maxW, max(max(textW, metaW), nameW) + 16);
    const int h = 8 + (showName ? 17 : 0) + nl * 17 + 16;
    const int x = out ? L::W - 8 - w : 8;
    const int y = bottomY - h;
    const bool mention = m.flags & HF_MENTION;
    const uint16_t bg = out ? t.bubbleOut : (mention ? t.mentionBg : t.bubbleIn);
    g.fillRoundRect(x, y, w, h, 8, bg);
    if (selected) g.drawRoundRect(x - 1, y - 1, w + 2, h + 2, 9, t.green);
    else if (mention) g.drawRoundRect(x, y, w, h, 8, t.amber);
    int ty = y + 4;
    if (showName) {
      g.setTextColor(nameColor(m.sender) | 0x8410, bg);    // lifted for contrast
      drawRich(g, name, x + 8, ty);
      ty += 17;
    }
    g.setTextColor(t.white, bg);
    scrambleIfNew(m.id, text);                        // a new arrival decrypts in front of you
    for (int i = 0; i < nl; i++) {
      drawRich(g, text + st[i], x + 8, ty, ln[i]);
      ty += 17;
    }
    g.setTextColor(m.status == ST_FAILED ? t.red : t.dim, bg);
    g.drawString(meta, x + w - 8 - metaW, ty);
    notePos(m.id, x + w - 12, ty + 7);                // where its effects go: the status line
    return h;
  }

  int drawCompact(Canvas& g, const HistMsg& m, int bottomY, bool selected) {
    const Theme& t = nav.theme();
    const bool out = m.flags & HF_OUT;
    char text[220];
    char body[200];
    sanitize(m.text, body, sizeof(body));
    char who[28];
    sanitize(out ? "me" : (m.sender[0] ? m.sender : "?"), who, sizeof(who));
    snprintf(text, sizeof(text), "%s> %s", who, body);
    uint16_t st[12]; uint8_t ln[12];
    const int nl = wrapText(g, text, L::W - 90, st, ln, 12);
    const int h = nl * 17 + 2;
    const int y = bottomY - h;
    if (selected) g.fillRect(0, y, L::W, h, t.focus);
    for (int i = 0; i < nl; i++) {
      g.setTextColor(i == 0 ? (out ? t.green : nameColor(m.sender) | 0x8410) : t.txt, selected ? t.focus : t.bg);
      drawRich(g, text + st[i], 8, y + 1 + i * 17, ln[i]);
    }
    char meta[48];
    metaText(m, meta, sizeof(meta));
    g.setTextColor(m.status == ST_FAILED ? t.red : t.dim, selected ? t.focus : t.bg);
    g.drawString(meta, L::W - 8 - g.textWidth(meta), y + 1);
    notePos(m.id, L::W - 16, y + 8);
    return h;
  }

  // ---- animation: what changed since the last look, and where it's on screen ----
  static constexpr uint32_t REVEAL_MS = 650;
  struct Seen { uint32_t id; uint8_t status, repeats; };
  struct Pos  { uint32_t id; int16_t x, y; };
  Seen _seen[16] = {};
  uint8_t _seenN = 0;
  Pos _pos[24] = {};
  uint8_t _posN = 0;
  uint32_t _maxId = 0, _revealId = 0, _revealAt = 0;
  bool _seeded = false;

  void notePos(uint32_t id, int x, int y) {
    if (_posN < 24) _pos[_posN++] = {id, (int16_t)x, (int16_t)y};
  }
  const Pos* posOf(uint32_t id) const {
    for (uint8_t i = 0; i < _posN; i++) if (_pos[i].id == id) return &_pos[i];
    return nullptr;
  }

  // A repeat heard: a ping off the bubble. Delivered: a tick. Failed: a shake.
  // A message that just arrived: it decrypts on screen. The first look at a
  // thread only records what's there, so opening it doesn't set everything off.
  void noticeChanges() {
    const uint32_t now = millis();
    uint32_t newest = _maxId;
    for (int i = max(0, _n - 16); i < _n; i++) {
      HistMsg* m = history.find(_ids[i]);
      if (!m) continue;
      newest = max(newest, _ids[i]);
      if (!(m->flags & HF_OUT)) {
        if (_seeded && _ids[i] > _maxId) { _revealId = _ids[i]; _revealAt = now; }
        continue;
      }
      Seen* s = nullptr;
      for (uint8_t k = 0; k < _seenN; k++) if (_seen[k].id == _ids[i]) s = &_seen[k];
      if (!s) {
        if (_seenN < 16) s = &_seen[_seenN++];
        else { memmove(_seen, _seen + 1, sizeof(Seen) * 15); s = &_seen[15]; }
        *s = {_ids[i], m->status, m->repeats};
        continue;
      }
      if (_seeded) {
        const Pos* p = posOf(_ids[i]);
        if (p && m->repeats > s->repeats)
          for (int r = 0; r < min(3, m->repeats - s->repeats); r++) fx::ping(p->x - r * 14, p->y);
        if (m->status != s->status) {
          if (m->status == ST_DELIVERED && p) fx::check(p->x - 4, p->y - 2);
          if (m->status == ST_FAILED) fx::fail();
        }
      }
      s->status = m->status;
      s->repeats = m->repeats;
    }
    _maxId = newest;
    _seeded = true;
  }

  // While a new message is decrypting, each character not yet revealed shows as
  // a random glyph from the theme's set; the reveal sweeps left to right.
  void scrambleIfNew(uint32_t id, char* text) {
    if (id != _revealId) return;
    const uint32_t age = millis() - _revealAt;
    if (age >= REVEAL_MS) { _revealId = 0; return; }
    static const char* SETS[] = {"0123456789abcdef", "#%&@$=", "*+x~^o", ".:*+~'"};
    const char* set = SETS[nav.theme().style & 3];
    const int setN = strlen(set);
    int len = 0;
    for (const char* p = text; *p; p++) if ((uint8_t)*p >= 0x20) len++;
    const int shown = (int)(len * (age / (float)REVEAL_MS));
    uint32_t seed = (age / 50) * 2654435761u + id;
    int k = 0;
    for (char* p = text; *p; p++) {
      if (*p == 0x01 || *p == 0x02) { if (p[1]) p++; continue; }   // emoji markers stay whole
      if ((uint8_t)*p < 0x21) { if ((uint8_t)*p >= 0x20) k++; continue; }
      if (k++ >= shown) { seed = seed * 1103515245u + 12345u; *p = set[(seed >> 16) % setN]; }
    }
  }

  void metaText(const HistMsg& m, char* out, size_t cap) {
    size_t w = snprintf(out, cap, "%s", m.ts && app::timeValid() ? clockText(m.ts) : "");
    if (m.flags & HF_OUT) {
      const char* st = m.status == ST_SENDING ? "  sending" : m.status == ST_SENT ? "  sent"
                     : m.status == ST_DELIVERED ? "  delivered" : m.status == ST_FAILED ? "  FAILED" : "";
      if (_key.type == CONV_CHANNEL && m.status == ST_SENT) st = m.repeats ? "  heard" : "  sent";
      w += snprintf(out + w, cap - w, "%s", st);
      if (m.repeats) w += snprintf(out + w, cap - w, " x%u", m.repeats);
      if (m.attempts > 1 && w < cap) w += snprintf(out + w, cap - w, " try%u", m.attempts);
    } else {
      if (ui_settings.showHops && w < cap) {
        if (m.hops == 0xFF || m.hops == 0) w += snprintf(out + w, cap - w, "  direct");
        else w += snprintf(out + w, cap - w, "  %uh", m.hops);
      }
      if (ui_settings.showSnr && w < cap) snprintf(out + w, cap - w, "  %.1fdB", m.snr4 / 4.0);
    }
  }

  void drawCompose(Canvas& g, int y) {
    const Theme& t = nav.theme();
    const int x = 6, w = L::W - 12, h = 24;
    y += 2;
    g.fillRoundRect(x, y, w, h, 12, t.panel);
    g.drawRoundRect(x, y, w, h, 12, _sel < 0 ? t.greenDim : t.line);
    if (!_compose.length()) {
      g.setTextColor(t.dim, t.panel);
      char ph[48];
      char nm[28];
      convName(_key, nm, sizeof(nm));
      snprintf(ph, sizeof(ph), "message %s", nm);
      drawUtf8(g, ph, x + 12, y + 4, w - 180);
      const char* hint = _sel >= 0 ? "press: actions" : "press: emoji + replies";
      g.drawString(hint, x + w - 12 - g.textWidth(hint), y + 4);
      if (_sel < 0 && _caret) g.fillRect(x + 10, y + 5, 2, 14, t.green);
      return;
    }
    char safe[200];
    sanitize(_compose.c_str(), safe, sizeof(safe));
    // Show the end of a long draft; never start the view inside an emoji marker.
    const char* p = safe;
    while (*p && richWidth(g, p) > w - 70) p += (*p == 0x01 || *p == 0x02) && p[1] ? 2 : 1;
    g.setTextColor(t.white, t.panel);
    const int cx = drawRich(g, p, x + 12, y + 4);
    if (_caret) g.fillRect(cx + 1, y + 5, 2, 14, t.green);
    char cnt[12];
    snprintf(cnt, sizeof(cnt), "%d", maxLen() - (int)_compose.length());
    g.setTextColor((int)_compose.length() > maxLen() - 15 ? t.amber : t.dim, t.panel);
    g.drawString(cnt, x + w - 12 - g.textWidth(cnt), y + 4);
  }

  static constexpr uint16_t MAX_IDS = 200;
  ConvKey _key;
  uint32_t _ids[MAX_IDS];
  int _n = 0, _sel = -1;
  uint32_t _gen = 0, _blinkAt = 0;
  bool _caret = true;
  String _compose;
};


// ---- emoji picker ----------------------------------------------------------------------------
// The keyboard has no emoji, so the wheel press in a thread opens this grid.
// Most-used first, then every other glyph we can draw; recent picks float up.
static const uint32_t POPULAR[] = {
  0x1F44D, 0x2764, 0x1F602, 0x1F60A, 0x1F64F, 0x1F525, 0x1F440, 0x2705, 0x1F62D, 0x1F605,
  0x1F389, 0x1F44B, 0x1F4AF, 0x1F923, 0x1F601, 0x1F642, 0x1F60E, 0x1F914, 0x1F622, 0x1F621,
  0x1F44C, 0x1F64C, 0x1F4AA, 0x270C, 0x1F4CD, 0x1F697, 0x26A1, 0x1F4E1, 0x1F60D, 0x1F618,
  0x1F643, 0x1F644, 0x1F631, 0x1F634, 0x1F92F, 0x1F44E, 0x274C, 0x2753, 0x26A0, 0x1F680,
};
static uint32_t s_recent[11] = {0};

static void utf8Append(String& s, uint32_t cp) {
  char b[5] = {0};
  if (cp < 0x80) b[0] = cp;
  else if (cp < 0x800) { b[0] = 0xC0 | (cp >> 6); b[1] = 0x80 | (cp & 0x3F); }
  else if (cp < 0x10000) { b[0] = 0xE0 | (cp >> 12); b[1] = 0x80 | ((cp >> 6) & 0x3F); b[2] = 0x80 | (cp & 0x3F); }
  else { b[0] = 0xF0 | (cp >> 18); b[1] = 0x80 | ((cp >> 12) & 0x3F); b[2] = 0x80 | ((cp >> 6) & 0x3F); b[3] = 0x80 | (cp & 0x3F); }
  s += b;
}

class EmojiPickerView : public View {
public:
  EmojiPickerView(ThreadView* tv) : _tv(tv) {
    auto add = [&](uint32_t cp) {
      const int i = emojiFind(cp);
      if (i < 0) return;
      for (int k : _order) if (k == i) return;
      _order.push_back(i);
    };
    for (uint32_t cp : s_recent) if (cp) add(cp);
    for (uint32_t cp : POPULAR) add(cp);
    for (uint16_t i = 0; i < emojiCount(); i++) add(emojiCodepoint(i));
  }
  bool wantsAllKeys() override { return true; }
  void rotate(int d) override { const int n = _order.size(); _f = ((_f + d) % n + n) % n; dirty = true; }
  void key(char c) override {
    if (c == '\n') { press(); return; }
    if (c == 'q') { nav.pop(); openQuickReplies(_tv); return; }
    // w/s jump a row, a/d a column: faster than spinning through 272
    const int n = _order.size();
    if (c == 'w') _f = max(0, _f - COLS);
    else if (c == 's') _f = min(n - 1, _f + COLS);
    else if (c == 'a') _f = max(0, _f - 1);
    else if (c == 'd') _f = min(n - 1, _f + 1);
    dirty = true;
  }
  void press() override {
    const uint32_t cp = emojiCodepoint(_order[_f]);
    // Remember it: recent picks lead the grid next time.
    int at = 10;
    for (int i = 0; i < 11; i++) if (s_recent[i] == cp) { at = i; break; }
    for (int i = at; i > 0; i--) s_recent[i] = s_recent[i - 1];
    s_recent[0] = cp;
    String c = _tv->compose();
    utf8Append(c, cp);
    ThreadView* tv = _tv;
    nav.pop();
    tv->setCompose(c);
  }
  void draw(Canvas& g) override;
private:
  static constexpr int COLS = 11, CELL = 42, ROWS = 4;
  ThreadView* _tv;
  std::vector<int> _order;
  int _f = 0, _top = 0;
};

static void openQuickReplies(ThreadView* tv) {
  auto* m = new MenuView("Quick reply");
  for (uint8_t i = 0; i < UiSettings::QUICK_MAX; i++) {
    if (!ui_settings.quick[i][0]) continue;
    String q = ui_settings.quick[i];
    m->action(q, [tv, q] { nav.pop(); tv->setCompose(q); tv->send(); });
  }
  m->action("share my position", [tv] {
    double la, lo;
    if (!app::myPosition(la, lo)) { nav.toast("no position yet"); return; }
    char b[48];
    snprintf(b, sizeof(b), "I'm at %.5f,%.5f", la, lo);
    nav.pop(); tv->setCompose(b); tv->send();
  });
  nav.push(m);
}

static void openMessageActions(ThreadView* tv, uint32_t id) {
  HistMsg* msg = history.find(id);
  if (!msg) return;
  const bool out = msg->flags & HF_OUT;
  const String sender = msg->sender;
  auto* m = new MenuView("Message");
  if (!out && sender.length()) {
    m->action("reply with @mention", [tv, sender] {
      nav.pop();
      tv->setCompose("@[" + sender + "] " + tv->compose());
    });
    if (tv->key().type == CONV_CHANNEL && g_node) {
      m->action("message " + sender + " privately", [sender] {
        // Find the sender among contacts by name.
        ContactsIterator it = g_node->startContactsIterator();
        ContactInfo c;
        while (it.hasNext(g_node, c)) {
          if (c.type && !strcmp(c.name, sender.c_str())) { nav.pop(); app::openThreadForContact(c.id.pub_key); return; }
        }
        nav.toast("not in your contacts yet");
      });
    }
  }
  const String text = msg->text;
  m->action("quote into reply", [tv, text] { nav.pop(); tv->setCompose("\"" + text.substring(0, 60) + "\" "); });
  if (out && (msg->status == ST_FAILED || tv->key().type == CONV_CHANNEL))
    m->action("send again", [tv, id] { nav.pop(); tv->resend(id); });
  const ConvKey ck = tv->key();
  m->value("notifications for this chat", [ck]() -> String { return notifyModeName(notifyMode(ck)); },
           [ck] { setNotifyMode(ck, (notifyMode(ck) + 1) % NM_COUNT); });
  m->header("details");
  m->info("time", [id]() -> String { HistMsg* x = history.find(id); return String(x && x->ts ? clockText(x->ts, true) : "unknown"); });
  if (!out) {
    m->info("route", [id]() -> String { HistMsg* x = history.find(id); if (!x) return String("");
      return x->hops == 0xFF || !x->hops ? String("direct") : String(x->hops) + " hops"; });
    // One row per repeater that carried it, named from contacts where we know them.
    HistMsg* msg2 = history.find(id);
    for (uint8_t h = 0; msg2 && h < msg2->path_len; h++) {
      const uint8_t hash = msg2->path[h];
      char label[12];
      snprintf(label, sizeof(label), "  hop %u", h + 1);
      m->info(label, [hash]() -> String {
        ContactInfo* c = g_node ? g_node->contactByPrefix(&hash, 1) : nullptr;
        char b[40];
        snprintf(b, sizeof(b), "%02x  %s", hash, c && c->name[0] ? c->name : "(unknown)");
        return String(b); });
    }
    m->info("signal", [id]() -> String { HistMsg* x = history.find(id); return String(x ? x->snr4 / 4.0 : 0, 1) + " dB SNR"; });
  } else {
    m->info("status", [id]() -> String { HistMsg* x = history.find(id); if (!x) return String("");
      static const char* S[] = {"received", "sending", "sent", "delivered", "failed"};
      return String(S[x->status % 5]); });
    m->info("repeats heard", [id]() -> String { HistMsg* x = history.find(id); return String(x ? x->repeats : 0); });
    m->info("attempts", [id]() -> String { HistMsg* x = history.find(id); return String(x ? max<int>(1, x->attempts) : 0); });
    m->info("round trip", [id]() -> String { HistMsg* x = history.find(id);
      return x && x->rtt10 ? String(x->rtt10 * 10) + " ms" : String("-"); });
  }
  nav.push(m);
}

static void openEmojiPicker(ThreadView* t) { nav.push(new EmojiPickerView(t)); }

void EmojiPickerView::draw(Canvas& g) {
  const Theme& t = nav.theme();
  drawHeader(g, "Emoji", "press insert  q quick replies  wasd move");
  const int row = _f / COLS;
  if (row < _top) _top = row;
  if (row >= _top + ROWS) _top = row - ROWS + 1;
  const int x0 = (L::W - COLS * CELL) / 2;
  for (int r = _top; r < _top + ROWS; r++) {
    for (int c = 0; c < COLS; c++) {
      const int i = r * COLS + c;
      if (i >= (int)_order.size()) break;
      const int x = x0 + c * CELL, y = L::BODY_Y + 4 + (r - _top) * CELL;
      if (i == _f) { g.fillRoundRect(x + 1, y + 1, CELL - 2, CELL - 2, 6, t.focus); g.drawRoundRect(x + 1, y + 1, CELL - 2, CELL - 2, 6, t.green); }
      drawEmojiGlyph(g, _order[i], x + 5, y + 5, 2);
    }
  }
  drawScrollbar(g, ((int)_order.size() + COLS - 1) / COLS, _top, ROWS, L::BODY_Y, L::H - L::BODY_Y);
}

// ---- chat list ----------------------------------------------------------------------------------
struct ChatEntry {
  ConvKey key;
  char name[32];
  uint8_t kind;
  uint32_t lastTs, lastId;
  uint16_t unread;
  bool mention;
  char preview[72];
};

class ChatListView : public View {
public:
  ChatListView() { rebuild(); }
  bool wantsAllKeys() override { return true; }
  void resume() override { rebuild(); dirty = true; }
  void tick() override {
    if (history.gen != _hgen || (g_node && g_node->contactsGen() != _cgen && millis() - _built > 3000)) {
      rebuild(); dirty = true;
    }
  }
  void rotate(int d) override {
    const int n = _count + 1;
    _focus = ((_focus + d) % n + n) % n;
    dirty = true;
  }
  void key(char c) override {
    if (c == '\n') { press(); return; }
    if ((uint8_t)c < 0x20 || _filter.length() >= 16) return;
    _filter += c; _focus = 0; rebuild(); dirty = true;
  }
  bool backspace() override {
    if (!_filter.length()) return false;
    _filter.remove(_filter.length() - 1); _focus = 0; rebuild(); dirty = true;
    return true;
  }
  void press() override {
    if (_focus == _count) { newChatMenu(); return; }
    if (_focus < _count) {
      const ChatEntry& e = _e[_focus];
      nav.push(new ThreadView(e.key));
    }
  }

  void draw(Canvas& g) override {
    const Theme& t = nav.theme();
    char right[32];
    if (_filter.length()) snprintf(right, sizeof(right), "/%s", _filter.c_str());
    else snprintf(right, sizeof(right), "%d chats", _count);
    drawHeader(g, "Messages", right);
    const int rowH = 44, visible = 4;
    const int n = _count + 1;
    if (_focus < _scroll) _scroll = _focus;
    if (_focus >= _scroll + visible) _scroll = _focus - visible + 1;
    for (int i = _scroll; i < n && i < _scroll + visible; i++) {
      const int y = L::BODY_Y + (i - _scroll) * rowH;
      const bool on = i == _focus;
      const uint16_t bg = on ? t.focus : t.bg;
      if (on) { g.fillRect(0, y, L::W, rowH, bg); g.fillRect(0, y, 3, rowH, t.green); }
      if (i == _count) {
        g.setTextColor(on ? t.green : t.dim, bg);
        g.drawString("+  new message / join channel", 16, y + 13);
        continue;
      }
      const ChatEntry& e = _e[i];
      drawAvatar(g, 26, y + rowH / 2, 16, e.name, e.kind);
      char nm[44];
      sanitize(e.name, nm, sizeof(nm) - 4);
      richFit(g, nm, L::W - 130);
      g.setTextColor(e.unread ? t.green : t.white, bg);
      drawRich(g, nm, 52, y + 4);
      if (e.lastTs && app::timeValid()) {
        const char* tt = timeAgo(e.lastTs);
        g.setTextColor(e.unread ? t.green : t.dim, bg);
        g.drawString(tt, L::W - 12 - g.textWidth(tt), y + 4);
      }
      char pv[100];
      sanitize(e.preview, pv, sizeof(pv) - 4);
      richFit(g, pv, L::W - 110);
      g.setTextColor(t.dim, bg);
      drawRich(g, pv, 52, y + 23);
      const uint8_t nm_ = notifyMode(e.key);
      if (e.unread) {
        char b[8];
        snprintf(b, sizeof(b), "%u", min<uint16_t>(e.unread, 99));
        const int bw = max(20, (int)g.textWidth(b) + 10);
        // Muted chats still count unread, but quietly.
        drawPill(g, L::W - 12 - bw, y + 23, bw, 17, nm_ == NM_MUTED ? t.line : e.mention ? t.amber : t.green,
                 nm_ == NM_MUTED ? t.dim : t.bg, b);
      } else if (nm_ == NM_MUTED || nm_ == NM_SILENT) {
        g.setTextColor(t.dim, bg);
        g.drawString(nm_ == NM_MUTED ? "muted" : "silent", L::W - 12 - g.textWidth(nm_ == NM_MUTED ? "muted" : "silent"), y + 23);
      } else if (e.mention) {
        g.setTextColor(t.amber, bg);
        g.drawString("@", L::W - 24, y + 23);
      }
      g.drawFastHLine(52, y + rowH - 1, L::W - 60, t.line);
    }
    drawScrollbar(g, n, _scroll, visible, L::BODY_Y, L::H - L::BODY_Y);
  }

private:
  void add(const ConvKey& k, int chanIdx, const char* name, uint8_t kind) {
    if (_count >= MAX) return;
    if (_filter.length() && !strcasestr(name, _filter.c_str())) return;
    ChatEntry& e = _e[_count++];
    e.key = k;
    strlcpy(e.name, name, sizeof(e.name));
    e.kind = kind;
    HistMsg* m = history.last(k);
    e.lastTs = m ? m->ts : 0;
    e.lastId = m ? m->id : 0;
    e.unread = history.unread(k);
    e.mention = history.hasMention(k);
    if (m) {
      const bool out = m->flags & HF_OUT;
      if (out) snprintf(e.preview, sizeof(e.preview), "you: %s", m->text);
      else if (k.type == CONV_CHANNEL || (m->flags & HF_ROOM)) snprintf(e.preview, sizeof(e.preview), "%s: %s", m->sender, m->text);
      else strlcpy(e.preview, m->text, sizeof(e.preview));
    } else {
      strlcpy(e.preview, chanIdx >= 0 ? "no messages yet" : "", sizeof(e.preview));
    }
  }

  void rebuild() {
    _count = 0;
    _hgen = history.gen;
    _cgen = g_node ? g_node->contactsGen() : 0;
    _built = millis();
    if (!g_node) return;
    // Every channel is a chat, whether or not it has traffic yet.
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
      ChannelDetails ch;
      if (!g_node->getChannel(i, ch) || !ch.name[0]) continue;
      add(ConvKey::channel(ch.channel.secret), i, ch.name, 10);
    }
    ConvKey keys[64];
    const uint16_t n = history.conversations(keys, 64);
    for (uint16_t i = 0; i < n; i++) {
      if (keys[i].type != CONV_CONTACT) continue;
      char nm[32]; uint8_t kind;
      convName(keys[i], nm, sizeof(nm), &kind);
      add(keys[i], -1, nm, kind);
    }
    // Most recent activity first; channels with no traffic keep their slot order.
    for (int i = 1; i < _count; i++) {
      ChatEntry tmp = _e[i];
      int j = i - 1;
      while (j >= 0 && _e[j].lastId < tmp.lastId) { _e[j + 1] = _e[j]; j--; }
      _e[j + 1] = tmp;
    }
    if (_focus > _count) _focus = _count;
  }

  void newChatMenu();

  static constexpr int MAX = 104;
  ChatEntry _e[MAX];
  int _count = 0, _focus = 0, _scroll = 0;
  uint32_t _hgen = 0, _cgen = 0, _built = 0;
  String _filter;
};

// Hashtag channels derive their key from the name, exactly as the MeshCore app
// does: the first 16 bytes of SHA-256("#name").
static void hashtagSecret(const char* name, uint8_t* out16) {
  uint8_t h[32];
  mesh::Utils::sha256(h, 32, (const uint8_t*)name, strlen(name));
  memcpy(out16, h, 16);
}

// Standard base64 (the MeshCore app shows channel keys this way).
static int b64decode(const char* in, uint8_t* out, int cap) {
  auto v = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
  };
  uint32_t acc = 0; int bits = 0, n = 0;
  for (; *in && *in != '='; in++) {
    const int x = v(*in);
    if (x < 0) return 0;
    acc = (acc << 6) | x; bits += 6;
    if (bits >= 8) { bits -= 8; if (n < cap) out[n++] = (acc >> bits) & 0xFF; }
  }
  return n;
}

void joinChannelFlow();
void ChatListView::newChatMenu() {
  auto* m = new MenuView("New");
  m->action("message a contact", [] { nav.pop(); app::openContacts(); });
  m->action("join a #hashtag channel", [] { nav.pop(); joinChannelFlow(); });
  m->action("add private channel (key)", [] {
    nav.pop();
    prompt("Private channel", "channel name", "", 30, [](const String& name) {
      if (!name.length()) return;
      prompt("Channel key", "32 hex chars, or base64 from the app", "", 44, [name](const String& key) {
        uint8_t s[32] = {0};
        bool ok = false;
        if (key.length() == 32) ok = mesh::Utils::fromHex(s, 16, key.c_str());
        else if (key.length() == 24 || key.length() == 44) {
          ok = b64decode(key.c_str(), s, sizeof(s)) >= 16;
        }
        if (!ok) { nav.toast("that key is not valid"); return; }
        nav.toast(g_node && g_node->addChannelNamed(name.c_str(), s) ? "channel added" : "no free channel slot");
      });
    });
  });
  nav.push(m);
}

void joinChannelFlow() {
  prompt("Join channel", "hashtag channel name, e.g. inw or #geg-bot", "#", 30, [](const String& in) {
    String name = in;
    name.trim();
    if (name.length() < 2) return;
    if (name[0] != '#') name = "#" + name;
    name.toLowerCase();
    uint8_t s[16];
    hashtagSecret(name.c_str(), s);
    if (!g_node) return;
    if (g_node->addChannelNamed(name.c_str(), s)) {
      nav.toast("joined");
      app::openThreadForChannel(g_node->findChannelBySecret(s));
    } else if (g_node->findChannelBySecret(s) >= 0) {
      app::openThreadForChannel(g_node->findChannelBySecret(s));
    } else nav.toast("no free channel slot (40)");
  });
}

void app::openChats() { nav.push(new ChatListView()); }

void app::openThreadForContact(const uint8_t* pub) {
  nav.push(new ThreadView(ConvKey::contact(pub)));
}

void app::openThreadForChannel(int idx) {
  if (!g_node || idx < 0) return;
  ChannelDetails ch;
  if (!g_node->getChannel(idx, ch) || !ch.name[0]) return;
  nav.push(new ThreadView(ConvKey::channel(ch.channel.secret)));
}
