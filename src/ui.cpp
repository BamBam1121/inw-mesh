#include "ui.h"
#include "app.h"
#include "statusbar.h"
#include "node.h"
#include "gps.h"
#include "logstore.h"
#include "netwifi.h"
#include "power.h"
#include <time.h>
#include "emoji_data.h"

Nav nav;
static std::vector<View*> s_graveyard;   // popped views die on the next tick, never mid-call

// ---- status bar ----------------------------------------------------------------
static uint16_t mix565(uint16_t a, uint16_t b, float f) {
  const int r = ((a >> 11) & 31) + (int)((((b >> 11) & 31) - ((a >> 11) & 31)) * f);
  const int g = ((a >> 5) & 63) + (int)((((b >> 5) & 63) - ((a >> 5) & 63)) * f);
  const int bl = (a & 31) + (int)(((b & 31) - (a & 31)) * f);
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

static constexpr int BATT_W = 20, BATT_H = 10, BATT_Y = 4;

// The battery glyph at (x, y), 22 x 10. While plugged in it shows a charge mark
// in the theme's own shape; while actually charging the fill breathes.
static void drawBatteryIcon(lgfx::LovyanGFX& d, const Theme& t, int x, int y) {
  const uint8_t pct = app::batteryPct();
  const bool plugged = app::pluggedIn(), charging = app::charging();
  d.fillRect(x - 1, y - 2, BATT_W + 5, BATT_H + 4, t.panel);
  d.drawRect(x, y, BATT_W, BATT_H, t.dim);
  d.fillRect(x + BATT_W, y + 3, 2, 4, t.dim);
  const float breath = charging ? 0.5f + 0.5f * sinf(millis() * (2.0f * PI / 1800.0f)) : 1.0f;
  uint16_t bc = power::saver() ? t.amber : pct <= 15 ? t.red : pct <= 30 ? t.amber : t.txt;
  if (plugged) {
    switch (t.style) {
      case STYLE_AURORA: {             // drift through the sky's colours
        const float h = fmodf(millis() / 4000.0f, 1.0f);
        const uint16_t a = t.green, b = t.greenDim, c = t.blue;
        bc = h < 0.33f ? mix565(a, c, h * 3) : h < 0.66f ? mix565(c, b, (h - 0.33f) * 3) : mix565(b, a, (h - 0.66f) * 3);
        break;
      }
      case STYLE_HERO:   bc = t.red; break;    // hearts refill red
      default:           bc = t.green; break;
    }
    bc = mix565(t.panel, bc, 0.35f + 0.65f * breath);
  }
  const int fw = (BATT_W - 4) * min<uint8_t>(pct, 100) / 100;
  d.fillRect(x + 2, y + 2, max(fw, plugged ? 2 : 0), BATT_H - 4, bc);
  if (!plugged) return;
  const int cx = x + BATT_W / 2, cy = y + BATT_H / 2;
  const uint16_t mark = t.white, edge = t.panel;
  switch (t.style) {
    case STYLE_BLOCKS:                  // a stepped pixel bolt
      d.fillRect(cx, cy - 4, 3, 3, edge); d.fillRect(cx - 2, cy - 1, 5, 2, edge); d.fillRect(cx - 3, cy + 1, 3, 3, edge);
      d.fillRect(cx + 1, cy - 3, 1, 2, mark); d.fillRect(cx - 1, cy - 1, 3, 1, mark); d.fillRect(cx - 2, cy + 1, 1, 2, mark);
      break;
    case STYLE_HERO:                    // a little heart
      d.fillCircle(cx - 2, cy - 1, 2, edge); d.fillCircle(cx + 2, cy - 1, 2, edge); d.fillTriangle(cx - 4, cy, cx + 4, cy, cx, cy + 4, edge);
      d.fillCircle(cx - 2, cy - 1, 1, mark); d.fillCircle(cx + 2, cy - 1, 1, mark); d.fillTriangle(cx - 3, cy, cx + 3, cy, cx, cy + 3, mark);
      break;
    case STYLE_AURORA:                  // a four-point star
      d.fillTriangle(cx, cy - 5, cx - 2, cy, cx + 2, cy, edge); d.fillTriangle(cx, cy + 5, cx - 2, cy, cx + 2, cy, edge);
      d.fillTriangle(cx - 5, cy, cx, cy - 2, cx, cy + 2, edge); d.fillTriangle(cx + 5, cy, cx, cy - 2, cx, cy + 2, edge);
      d.fillTriangle(cx, cy - 4, cx - 1, cy, cx + 1, cy, mark); d.fillTriangle(cx, cy + 4, cx - 1, cy, cx + 1, cy, mark);
      d.fillTriangle(cx - 4, cy, cx, cy - 1, cx, cy + 1, mark); d.fillTriangle(cx + 4, cy, cx, cy - 1, cx, cy + 1, mark);
      break;
    default:                            // a lightning bolt
      d.fillTriangle(cx + 2, cy - 5, cx - 3, cy + 1, cx + 1, cy + 1, edge);
      d.fillTriangle(cx - 2, cy + 5, cx + 3, cy - 1, cx - 1, cy - 1, edge);
      d.fillTriangle(cx + 1, cy - 4, cx - 2, cy + 1, cx + 1, cy + 1, mark);
      d.fillTriangle(cx - 1, cy + 4, cx + 2, cy - 1, cx - 1, cy - 1, mark);
      break;
  }
}

static int battX() { return L::W - 4 - BATT_W - 2; }

// Animates just the battery corner, straight to the panel, while plugged in.
// A full status-bar repaint every frame would cost a whole-screen push.
void animateBatteryIcon(lgfx::LovyanGFX* panel, const Theme& t) {
  static uint32_t last = 0;
  if (!panel || !app::pluggedIn() || millis() - last < 60) return;
  last = millis();
  static LGFX_Sprite s;
  if (!s.getBuffer()) { s.setColorDepth(16); if (!s.createSprite(BATT_W + 5, BATT_H + 4)) return; }
  // The sprite's (0, 0) sits at the panel's (battX() - 1, BATT_Y - 2).
  drawBatteryIcon(s, t, 1, 2);
  s.pushSprite(panel, battX() - 1, BATT_Y - 2);
}

void drawStatusBar(lgfx::LovyanGFX& d, const Theme& t) {
  d.setFont(&fonts::Font2);
  d.fillRect(0, 0, L::W, 17, t.panel);
  d.drawFastHLine(0, 17, L::W, t.line);
  d.setTextColor(t.green, t.panel);
  d.drawString("SQUATCH", 6, 1);
  int x = 6 + d.textWidth("SQUATCH") + 8;
  const uint16_t un = app::unread();
  if (un) {
    char b[12];
    snprintf(b, sizeof(b), "%u new", un);
    d.fillRoundRect(x, 2, d.textWidth(b) + 8, 13, 6, t.amber);
    d.setTextColor(t.bg, t.amber);
    d.drawString(b, x + 4, 1);
    x += d.textWidth(b) + 14;
  }
  if (app::timeValid()) {
    const char* c = clockText(app::now());
    d.setTextColor(t.txt, t.panel);
    d.drawString(c, (L::W - d.textWidth(c)) / 2, 1);
  }
  // right side, laid out right to left
  int rx = battX();
  drawBatteryIcon(d, t, rx, BATT_Y);
  const char* bt = app::batteryText();
  rx -= d.textWidth(bt) + 4;
  d.setTextColor(t.dim, t.panel);
  d.drawString(bt, rx, 1);
  if (power::saver()) {
    rx -= 44;
    d.setTextColor(t.amber, t.panel);
    d.drawString("SAVER", rx, 1);
  } else if (power::holding()) {
    rx -= 30;
    d.setTextColor(t.green, t.panel);
    d.drawString("80%", rx, 1);
  }
  if (bleEnabled()) {
    rx -= 22;
    d.setTextColor(bleConnected() ? t.blue : t.dim, t.panel);
    d.drawString("BT", rx, 1);
  }
  if (wifi::enabled()) {
    rx -= 34;
    d.setTextColor(wifi::connected() ? t.green : t.dim, t.panel);
    d.drawString("WiFi", rx, 1);
  }
  if (ui_settings.gpsOn && !power::saver()) {
    rx -= 30;
    d.setTextColor(gps.hasFix() ? t.green : t.dim, t.panel);
    d.drawString("GPS", rx, 1);
  }
  if (!app::radioOk()) {
    rx -= 50;
    d.setTextColor(t.red, t.panel);
    d.drawString("NO RF", rx, 1);
  }
}

// ---- helpers ----------------------------------------------------------------------
void drawHeader(Canvas& g, const char* title, const char* right) {
  const Theme& t = nav.theme();
  g.setFont(&fonts::Font2);
  g.setTextColor(t.green, t.bg);
  g.drawString("<", 6, L::HEAD_Y + 4);
  int rw = 0;
  char r[96] = "";
  if (right && *right) { sanitize(right, r, sizeof(r)); rw = richWidth(g, r, -1); }
  drawUtf8(g, title, 18, L::HEAD_Y + 4, L::W - 40 - rw);
  if (rw) {
    g.setTextColor(t.dim, t.bg);
    drawRich(g, r, L::W - 8 - rw, L::HEAD_Y + 4);
  }
  g.drawFastHLine(0, L::BODY_Y - 1, L::W, t.line);
}

void drawScrollbar(Canvas& g, int total, int first, int visible, int y0, int h) {
  if (total <= visible || total <= 0) return;
  const Theme& t = nav.theme();
  const int th = max(10, h * visible / total);
  const int ty = y0 + (h - th) * first / max(1, total - visible);
  g.fillRect(L::W - 3, y0, 2, h, t.line);
  g.fillRect(L::W - 3, ty, 2, th, t.greenDim);
}

void drawPill(Canvas& g, int x, int y, int w, int h, uint16_t bg, uint16_t fg, const char* text) {
  g.fillRoundRect(x, y, w, h, h / 2, bg);
  g.setTextColor(fg, bg);
  const int tw = min(w - 8, widthUtf8(g, text));
  drawUtf8(g, text, x + (w - tw) / 2, y + (h - 16) / 2 + 1, w - 8);
}

void drawToggle(Canvas& g, int x, int y, bool on) {
  const Theme& t = nav.theme();
  g.fillRoundRect(x, y, 30, 14, 7, on ? t.greenDim : t.line);
  g.fillCircle(on ? x + 23 : x + 7, y + 7, 5, on ? t.green : t.dim);
}

// ---- emoji -------------------------------------------------------------------------
// In sanitized text an emoji we have a glyph for becomes a two-byte marker:
// 0x01 n  -> glyph n-1   (glyphs 0..253)
// 0x02 n  -> glyph n+253 (glyphs 254..)
// Everything that draws or measures user text goes through the rich* helpers
// below, which treat that pair as one 16 px image.
static int emojiIndex(uint32_t cp) {
  int lo = 0, hi = EMOJI_COUNT - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) >> 1;
    if (EMOJI_CP[mid] == cp) return mid;
    if (EMOJI_CP[mid] < cp) lo = mid + 1; else hi = mid - 1;
  }
  return -1;
}

static inline bool isMarker(char c) { return c == 0x01 || c == 0x02; }
static inline int markerGlyph(const char* p) { return (uint8_t)p[0] == 0x01 ? (uint8_t)p[1] - 1 : (uint8_t)p[1] + 253; }
constexpr int EMOJI_W = 17;   // 16 px glyph + 1 px gap

void sanitize(const char* in, char* out, size_t cap) {
  size_t w = 0;
  const uint8_t* s = (const uint8_t*)in;
  auto put = [&](const char* r) { while (*r && w + 1 < cap) out[w++] = *r++; };
  auto putEmoji = [&](int idx) {
    if (w + 2 >= cap) return;
    if (idx < 254) { out[w++] = 0x01; out[w++] = (char)(idx + 1); }
    else { out[w++] = 0x02; out[w++] = (char)(idx - 253); }
  };
  while (*s && w + 1 < cap) {
    const uint8_t c = *s;
    uint32_t cp;
    int len;
    if (c < 0x80) { cp = c; len = 1; }
    else if ((c & 0xE0) == 0xC0 && s[1]) { cp = ((c & 0x1F) << 6) | (s[1] & 0x3F); len = 2; }
    else if ((c & 0xF0) == 0xE0 && s[1] && s[2]) { cp = ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); len = 3; }
    else if ((c & 0xF8) == 0xF0 && s[1] && s[2] && s[3]) {
      cp = ((uint32_t)(c & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F); len = 4;
    } else { s++; continue; }
    s += len;
    if (cp < 0x80) {
      if (cp == '\n' || (cp >= 0x20 && cp < 0x7F)) out[w++] = (char)cp;
      else if (cp == '\t') out[w++] = ' ';
      continue;
    }
    // Invisible joiners and modifiers: a ZWJ sequence draws as its parts.
    if (cp == 0x200D || (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0x1F3FB && cp <= 0x1F3FF) || cp == 0x20E3) continue;
    const int ei = emojiIndex(cp);
    if (ei >= 0) { putEmoji(ei); continue; }
    if (cp >= 0xC0 && cp <= 0xFF) {
      static const char* LAT = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiionooooo/ouuuuypy";
      char r[2] = { LAT[cp - 0xC0], 0 }; put(r);
    }
    else if (cp == 0xB0) put("o");
    else if (cp == 0xA0) put(" ");
    else if (cp == 0x2018 || cp == 0x2019) put("'");
    else if (cp == 0x201C || cp == 0x201D) put("\"");
    else if (cp == 0x2013 || cp == 0x2014) put("-");
    else if (cp == 0x2026) put("...");
    else if (cp == 0x2022) put("*");
    else if (cp >= 0x1F1E6 && cp <= 0x1F1FF) { char r[2] = { (char)('A' + (cp - 0x1F1E6)), 0 }; put(r); }
    else put(cp >= 0x2000 ? "*" : "?");
  }
  out[w] = 0;
}

static void blitEmoji(lgfx::LovyanGFX& g, int idx, int x, int y) {
  if (idx < 0 || idx >= EMOJI_COUNT) return;
  const uint8_t* px = EMOJI_PIX[idx];
  for (int j = 0; j < 16; j++) {
    for (int i = 0; i < 16; i++, px += 3) {
      const uint8_t a = px[2];
      if (a < 8) continue;
      const uint16_t fg = px[0] | (px[1] << 8);
      if (a > 247) { g.drawPixel(x + i, y + j, fg); continue; }
      const uint16_t bg = g.readPixel(x + i, y + j);
      const uint32_t r = (((fg >> 11) & 31) * a + ((bg >> 11) & 31) * (255 - a)) / 255;
      const uint32_t gg = (((fg >> 5) & 63) * a + ((bg >> 5) & 63) * (255 - a)) / 255;
      const uint32_t b = ((fg & 31) * a + (bg & 31) * (255 - a)) / 255;
      g.drawPixel(x + i, y + j, (uint16_t)((r << 11) | (gg << 5) | b));
    }
  }
}

void drawEmojiGlyph(lgfx::LovyanGFX& g, int idx, int x, int y, int scale) {
  if (scale <= 1) { blitEmoji(g, idx, x, y); return; }
  if (idx < 0 || idx >= EMOJI_COUNT) return;
  const uint8_t* px = EMOJI_PIX[idx];
  for (int j = 0; j < 16; j++)
    for (int i = 0; i < 16; i++, px += 3)
      if (px[2] >= 96) g.fillRect(x + i * scale, y + j * scale, scale, scale, (uint16_t)(px[0] | (px[1] << 8)));
}

uint16_t emojiCount() { return EMOJI_COUNT; }
uint32_t emojiCodepoint(uint16_t i) { return i < EMOJI_COUNT ? EMOJI_CP[i] : 0; }
int emojiFind(uint32_t cp) { return emojiIndex(cp); }

// Width of the first n bytes (n < 0: the whole string) of sanitized text.
int richWidth(lgfx::LovyanGFX& g, const char* s, int n) {
  if (n < 0) n = strlen(s);
  int w = 0, i = 0;
  char run[200];
  int rl = 0;
  auto flush = [&]() { if (rl) { run[rl] = 0; w += g.textWidth(run); rl = 0; } };
  while (i < n) {
    if (isMarker(s[i]) && i + 1 < n) { flush(); w += EMOJI_W; i += 2; continue; }
    if (rl < (int)sizeof(run) - 1) run[rl++] = s[i];
    i++;
  }
  flush();
  return w;
}

// Draw sanitized text; returns the x after the last glyph.
int drawRich(lgfx::LovyanGFX& g, const char* s, int x, int y, int n) {
  if (n < 0) n = strlen(s);
  int i = 0;
  char run[200];
  int rl = 0;
  auto flush = [&]() { if (rl) { run[rl] = 0; x += g.drawString(run, x, y); rl = 0; } };
  while (i < n) {
    if (isMarker(s[i]) && i + 1 < n) { flush(); blitEmoji(g, markerGlyph(s + i), x, y); x += EMOJI_W; i += 2; continue; }
    if (rl < (int)sizeof(run) - 1) run[rl++] = s[i];
    i++;
  }
  flush();
  return x;
}

// Truncate sanitized text in place to fit maxW, ending in "..." when cut.
void richFit(lgfx::LovyanGFX& g, char* s, int maxW) {
  if (richWidth(g, s, -1) <= maxW) return;
  const int dots = g.textWidth("...");
  int n = strlen(s);
  while (n > 0 && richWidth(g, s, n) + dots > maxW) {
    n--;
    if (n > 0 && isMarker(s[n - 1])) n--;          // never split a marker pair
  }
  s[n] = 0;
  strcat(s, "...");
}

int drawUtf8(lgfx::LovyanGFX& g, const char* utf8, int x, int y, int maxW) {
  char buf[260];
  sanitize(utf8, buf, sizeof(buf) - 4);
  if (maxW > 0) richFit(g, buf, maxW);
  return drawRich(g, buf, x, y);
}

int widthUtf8(lgfx::LovyanGFX& g, const char* utf8) {
  char buf[256];
  sanitize(utf8, buf, sizeof(buf));
  return richWidth(g, buf, -1);
}

int wrapText(Canvas& g, const char* text, int w, uint16_t* starts, uint8_t* lens, int maxLines) {
  int n = 0;
  const int len = strlen(text);
  int i = 0;
  char one[2] = {0, 0};
  while (i < len && n < maxLines) {
    int lineW = 0, lastSpace = -1, j = i;
    while (j < len && text[j] != '\n') {
      const bool emo = isMarker(text[j]) && j + 1 < len;
      int cw;
      if (emo) cw = EMOJI_W;
      else { one[0] = text[j]; cw = g.textWidth(one); }
      if (lineW + cw > w) break;
      lineW += cw;
      if (text[j] == ' ') lastSpace = j;
      j += emo ? 2 : 1;
    }
    int end = j;
    if (j < len && text[j] != '\n' && lastSpace > i) end = lastSpace;
    if (end == i) end = i + (isMarker(text[i]) && i + 1 < len ? 2 : 1);
    starts[n] = i;
    lens[n] = (uint8_t)min(end - i, 255);
    n++;
    i = end;
    if (i < len && (text[i] == ' ' || text[i] == '\n')) i++;
  }
  return n;
}

const char* timeAgo(uint32_t epoch) {
  static char b[12];
  if (!epoch || !app::timeValid()) return "";
  const int32_t d = (int32_t)(app::now() - epoch);
  if (d < 60) return "now";
  if (d < 3600) snprintf(b, sizeof(b), "%ldm", (long)(d / 60));
  else if (d < 86400) snprintf(b, sizeof(b), "%ldh", (long)(d / 3600));
  else if (d < 86400L * 365) snprintf(b, sizeof(b), "%ldd", (long)(d / 86400));
  else return "long ago";
  return b;
}

const char* clockText(uint32_t epoch, bool withDate) {
  static char b[24];
  const time_t t = (time_t)epoch + (time_t)ui_settings.tzMinutes * 60;
  struct tm tm;
  gmtime_r(&t, &tm);
  char hm[10];
  if (ui_settings.clock24) snprintf(hm, sizeof(hm), "%02d:%02d", tm.tm_hour, tm.tm_min);
  else {
    int h = tm.tm_hour % 12; if (!h) h = 12;
    snprintf(hm, sizeof(hm), "%d:%02d%s", h, tm.tm_min, tm.tm_hour < 12 ? "am" : "pm");
  }
  if (withDate) {
    static const char* MON[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    snprintf(b, sizeof(b), "%s %d %s", MON[tm.tm_mon % 12], tm.tm_mday, hm);
  } else {
    strlcpy(b, hm, sizeof(b));
  }
  return b;
}

uint16_t nameColor(const char* name) {
  static const uint32_t PAL[] = { 0x6a4c93, 0x3a6b35, 0x8a5a2b, 0x2b5f8a, 0x8a2b4f, 0x2b8a7a, 0x6b6b2b, 0x4f4f8a };
  uint32_t h = 5381;
  for (const char* p = name; *p; p++) h = h * 33 + (uint8_t)*p;
  const uint32_t c = PAL[h % 8];
  return lgfx::color565((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

void drawAvatar(Canvas& g, int cx, int cy, int r, const char* name, uint8_t kind) {
  const Theme& t = nav.theme();
  char ini[3] = {0, 0, 0};
  const char* p = name;
  while (*p && !isalnum((unsigned char)*p)) p++;
  if (*p) {
    ini[0] = toupper((unsigned char)*p);
    const char* sp = strchr(p, ' ');
    if (sp && isalnum((unsigned char)sp[1])) ini[1] = toupper((unsigned char)sp[1]);
    else if (isalnum((unsigned char)p[1])) ini[1] = toupper((unsigned char)p[1]);
  } else ini[0] = '?';
  if (kind == 10) {                                  // channel
    g.fillCircle(cx, cy, r, t.greenDim);
    g.setTextColor(t.bg, t.greenDim);
    g.drawString("#", cx - g.textWidth("#") / 2, cy - 8);
    return;
  }
  const uint16_t c = nameColor(name);
  if (kind == 2) {                                   // repeater: ring
    g.fillCircle(cx, cy, r, t.line);
    g.drawCircle(cx, cy, r, c);
    g.drawCircle(cx, cy, r - 1, c);
    g.setTextColor(t.txt, t.line);
  } else if (kind == 3) {                            // room: square-ish
    g.fillRoundRect(cx - r, cy - r, r * 2, r * 2, 4, c);
    g.setTextColor(t.white, c);
  } else {
    g.fillCircle(cx, cy, r, c);
    g.setTextColor(t.white, c);
  }
  g.drawString(ini, cx - g.textWidth(ini) / 2, cy - 8);
}

// ---- Nav ------------------------------------------------------------------------------
void Nav::begin(LGFX* d, Theme* t) {
  _d = d; _t = t;
  _canvas.setPsram(true);
  _canvas.setColorDepth(16);
  _canvas.createSprite(L::W, L::H);
}

void Nav::push(View* v) {
  if (_depth >= 12) { delete v; return; }
  _stack[_depth++] = v;
  v->dirty = true;
  _statusDirty = true;
}

void Nav::pop() {
  if (_depth <= 1) return;                 // the home view is never popped
  s_graveyard.push_back(_stack[--_depth]);
  _stack[_depth] = nullptr;
  if (top()) top()->resume();
  _statusDirty = true;
}

void Nav::popTo(View* v) { while (_depth > 1 && top() != v) pop(); }
void Nav::popToHome() { while (_depth > 1) pop(); }

void Nav::replaceTop(View* v) {
  if (_depth > 1) { s_graveyard.push_back(_stack[--_depth]); _stack[_depth] = nullptr; }
  push(v);
}

void Nav::backspace() {
  View* v = top();
  if (!v) return;
  if (!v->backspace()) pop();
}

void Nav::toast(const char* msg, uint16_t ms) {
  strlcpy(_toast, msg, sizeof(_toast));
  _toastUntil = millis() + ms;
  invalidate();
}

void Nav::banner(const char* title, const char* text, uint16_t ms) {
  strlcpy(_bannerTitle, title, sizeof(_bannerTitle));
  sanitize(text, _bannerText, sizeof(_bannerText) - 4);
  _bannerUntil = millis() + ms;
  invalidate();
}

void Nav::tick() {
  for (View* v : s_graveyard) delete v;
  s_graveyard.clear();
  const uint32_t now = millis();
  if (_toastUntil && (int32_t)(now - _toastUntil) >= 0) { _toastUntil = 0; invalidate(); }
  if (_bannerUntil && (int32_t)(now - _bannerUntil) >= 0) { _bannerUntil = 0; invalidate(); }
  if (now - _lastStatus > 15000) { _lastStatus = now; _statusDirty = true; }
  if (top()) top()->tick();
}

void Nav::drawOverlays(lgfx::LovyanGFX& g) {
  const Theme& t = *_t;
  g.setFont(&fonts::Font2);
  if (_bannerUntil) {
    const int y = 20, h = 42;
    g.fillRoundRect(6, y, L::W - 12, h, 8, t.panel);
    g.drawRoundRect(6, y, L::W - 12, h, 8, t.green);
    g.setTextColor(t.green, t.panel);
    drawUtf8(g, _bannerTitle, 16, y + 4, L::W - 36);
    g.setTextColor(t.txt, t.panel);
    char line[100];
    strlcpy(line, _bannerText, sizeof(line) - 4);
    richFit(g, line, L::W - 36);
    drawRich(g, line, 16, y + 22);
  }
  if (_toastUntil) {
    const int w = min(L::W - 20, (int)g.textWidth(_toast) + 24);
    const int x = (L::W - w) / 2, y = L::H - 28;
    g.fillRoundRect(x, y, w, 22, 11, t.greenDim);
    g.setTextColor(t.white, t.greenDim);
    g.drawString(_toast, x + (w - g.textWidth(_toast)) / 2, y + 3);
  }
}

void Nav::draw() {
  View* v = top();
  if (!v || !_d) return;
  if (!v->dirty && !_statusDirty) return;
  if (v->customRender()) {
    v->render(_statusDirty);
    v->dirty = false;
    _statusDirty = false;
    return;
  }
  _canvas.fillScreen(_t->bg);
  if (!v->isLock()) drawStatusBar(_canvas, *_t);
  v->draw(_canvas);
  drawOverlays(_canvas);
  _canvas.pushSprite(_d, 0, 0);
  v->dirty = false;
  _statusDirty = false;
}

// ---- MenuView ---------------------------------------------------------------------------
MenuView& MenuView::header(const String& label) {
  _rows.push_back({RowKind::Header, label, nullptr, nullptr, nullptr, nullptr});
  return *this;
}
MenuView& MenuView::action(const String& label, std::function<void()> fn) {
  _rows.push_back({RowKind::Action, label, nullptr, nullptr, fn, nullptr});
  return *this;
}
MenuView& MenuView::submenu(const String& label, std::function<void()> fn, std::function<String()> value) {
  _rows.push_back({RowKind::Submenu, label, value, nullptr, fn, nullptr});
  return *this;
}
MenuView& MenuView::value(const String& label, std::function<String()> v, std::function<void()> fn) {
  _rows.push_back({RowKind::Value, label, v, nullptr, fn, nullptr});
  return *this;
}
MenuView& MenuView::toggle(const String& label, std::function<bool()> get, std::function<void()> flip) {
  _rows.push_back({RowKind::Toggle, label, nullptr, get, flip, nullptr});
  return *this;
}
MenuView& MenuView::adjust(const String& label, std::function<String()> v, std::function<void(int)> step) {
  _rows.push_back({RowKind::Adjust, label, v, nullptr, nullptr, step});
  return *this;
}
MenuView& MenuView::info(const String& label, std::function<String()> v) {
  _rows.push_back({RowKind::Info, label, v, nullptr, nullptr, nullptr});
  return *this;
}

void MenuView::resume() {
  if (rebuild) {
    const int f = _focus, s = _scroll;
    _rows.clear();
    rebuild(*this);
    _focus = min(f, (int)_rows.size() - 1);
    _scroll = s;
  }
  if (_focus < 0) _focus = 0;
  if (!_rows.empty() && !focusable(_focus)) { moveFocus(1); if (!focusable(_focus)) moveFocus(-1); }
  dirty = true;
}

void MenuView::moveFocus(int d) {
  const int n = _rows.size();
  if (!n) return;
  // Stop at the ends instead of wrapping. A flick of the wheel arrives as several
  // detents at once, and wrapping meant an upward flick stepped past the first row
  // and reappeared at the bottom, so the top of a long menu was unreachable.
  const int dir = d >= 0 ? 1 : -1;
  for (int f = _focus + dir; f >= 0 && f < n; f += dir) {
    if (focusable(f)) { _focus = f; return; }
  }
}

void MenuView::rotate(int d) {
  if (_rows.empty()) return;
  if (_editing) {
    MenuRow& r = _rows[_focus];
    // Values go up when the wheel turns up, like a volume knob. (The wheel reports
    // "down" as positive because that is the natural direction for lists.)
    if (r.onAdjust) r.onAdjust(-d);
    dirty = true;
    return;
  }
  if (!focusable(_focus)) { moveFocus(1); if (!focusable(_focus)) moveFocus(-1); }
  const int steps = abs(d);
  for (int i = 0; i < steps; i++) moveFocus(d);
  dirty = true;
}

void MenuView::press() {
  if (_rows.empty()) return;
  MenuRow& r = _rows[_focus];
  if (r.kind == RowKind::Adjust) { _editing = !_editing; dirty = true; return; }
  dirty = true;
  if (r.onPress) { auto fn = r.onPress; fn(); }   // copy: fn may rebuild _rows
}

bool MenuView::backspace() {
  if (_editing) { _editing = false; dirty = true; return true; }
  return false;
}

void MenuView::key(char c) {
  if (c == '\n') { press(); return; }
  if (_editing || _rows.empty()) return;
  const char lc = tolower(c);
  const int n = _rows.size();
  for (int k = 1; k <= n; k++) {
    const int i = (_focus + k) % n;
    if (focusable(i) && _rows[i].label.length() && tolower(_rows[i].label[0]) == lc) {
      _focus = i; dirty = true; return;
    }
  }
}

void MenuView::tick() {
  if (millis() - _lastRefresh > refreshMs) { _lastRefresh = millis(); dirty = true; }
}

void MenuView::draw(Canvas& g) {
  const Theme& t = nav.theme();
  char pos[24] = "";
  const int visible = (L::H - L::BODY_Y) / L::ROW_H;
  if ((int)_rows.size() > visible) snprintf(pos, sizeof(pos), "%d/%d", _focus + 1, (int)_rows.size());
  drawHeader(g, _title.c_str(), pos);
  if (_rows.empty()) {
    g.setTextColor(t.dim, t.bg);
    g.drawString("nothing here", 18, L::BODY_Y + 10);
    return;
  }
  if (_focus < _scroll) _scroll = _focus;
  if (_focus >= _scroll + visible) _scroll = _focus - visible + 1;
  // Pull headers and info rows above the focus into view: focus never lands on them,
  // so without this the top of a list scrolls away and cannot be brought back.
  while (_scroll > 0 && !focusable(_scroll - 1) && _focus < _scroll + visible - 1) _scroll--;
  _scroll = constrain(_scroll, 0, max(0, (int)_rows.size() - visible));
  for (int i = _scroll; i < (int)_rows.size() && i < _scroll + visible; i++) {
    const MenuRow& r = _rows[i];
    const int y = L::BODY_Y + (i - _scroll) * L::ROW_H;
    const bool on = i == _focus;
    const uint16_t bg = on ? t.focus : t.bg;
    if (r.kind == RowKind::Header) {
      g.setTextColor(t.greenDim, t.bg);
      g.drawString(r.label, 10, y + 3);
      g.drawFastHLine(14 + g.textWidth(r.label), y + 11, L::W - 30 - g.textWidth(r.label), t.line);
      continue;
    }
    if (on) { g.fillRect(0, y, L::W, L::ROW_H, bg); g.fillRect(0, y, 3, L::ROW_H, t.green); }
    g.setTextColor(r.kind == RowKind::Info ? t.dim : (on ? t.green : t.txt), bg);
    drawUtf8(g, r.label.c_str(), 12, y + 2, L::W - 170);
    const int rx = L::W - 12;
    switch (r.kind) {
      case RowKind::Toggle:
        drawToggle(g, rx - 30, y + 3, r.toggled && r.toggled());
        break;
      case RowKind::Submenu: {
        String v = r.value ? r.value() : String();
        g.setTextColor(t.dim, bg);
        g.drawString(">", rx - 6, y + 2);
        if (v.length()) drawUtf8(g, v.c_str(), rx - 14 - widthUtf8(g, v.c_str()), y + 2);
        break;
      }
      case RowKind::Value: case RowKind::Adjust: case RowKind::Info: {
        String v = r.value ? r.value() : String();
        const int vw = g.textWidth(v);
        if (on && _editing) {
          g.fillRoundRect(rx - vw - 10, y + 1, vw + 14, L::ROW_H - 2, 4, t.green);
          g.setTextColor(t.bg, t.green);
          g.drawString(v, rx - vw - 3, y + 2);
          g.setTextColor(t.green, bg);
          g.drawString("<", rx - vw - 22, y + 2);
        } else {
          g.setTextColor(r.kind == RowKind::Info ? t.txt : t.dim, bg);
          g.drawString(v, rx - vw, y + 2);
        }
        break;
      }
      default: break;
    }
  }
  drawScrollbar(g, _rows.size(), _scroll, visible, L::BODY_Y, L::H - L::BODY_Y);
}

// ---- PromptView -------------------------------------------------------------------------
void PromptView::draw(Canvas& g) {
  const Theme& t = nav.theme();
  drawHeader(g, _title.c_str());
  g.setTextColor(t.dim, t.bg);
  g.drawString(_hint, 12, L::BODY_Y + 8);
  const int fx = 10, fy = L::BODY_Y + 32, fw = L::W - 20, fh = 36;
  g.fillRoundRect(fx, fy, fw, fh, 6, t.panel);
  g.drawRoundRect(fx, fy, fw, fh, 6, t.green);
  g.setFont(&fonts::Font4);
  String shown = _secret ? String() : _buf;
  if (_secret) for (size_t i = 0; i < _buf.length(); i++) shown += '*';
  char safe[200];
  sanitize(shown.c_str(), safe, sizeof(safe));
  // Keep the caret end in view.
  const char* p = safe;
  while (*p && g.textWidth(p) > fw - 26) p++;
  g.setTextColor(t.white, t.panel);
  g.drawString(p, fx + 10, fy + 6);
  if (_caret) g.fillRect(fx + 12 + g.textWidth(p), fy + 8, 3, 21, t.green);
  g.setFont(&fonts::Font2);
  char cnt[16];
  snprintf(cnt, sizeof(cnt), "%u/%u", (unsigned)_buf.length(), (unsigned)_maxLen);
  g.setTextColor(t.dim, t.bg);
  g.drawString(cnt, L::W - 12 - g.textWidth(cnt), fy + fh + 6);
  g.drawString("enter saves  -  backspace on empty cancels  -  hold orange for 123", 12, L::H - 20);
}

void PromptView::key(char c) {
  if (c == '\n') { commit(); return; }
  if ((uint8_t)c < 0x20 || (uint8_t)c > 0x7E || _buf.length() >= _maxLen) return;
  _buf += c;
  _caret = true;
  dirty = true;
}

bool PromptView::backspace() {
  if (!_buf.length()) return false;
  _buf.remove(_buf.length() - 1);
  dirty = true;
  return true;
}

void PromptView::tick() {
  if (millis() - _blink > 500) { _blink = millis(); _caret = !_caret; dirty = true; }
}

void PromptView::commit() {
  auto fn = _done;
  const String v = _buf;
  nav.pop();
  if (fn) fn(v);
}

// ---- ConfirmView ---------------------------------------------------------------------------
void ConfirmView::draw(Canvas& g) {
  const Theme& t = nav.theme();
  const int x = 30, y = 36, w = L::W - 60, h = 150;
  g.fillRoundRect(x, y, w, h, 10, t.panel);
  g.drawRoundRect(x, y, w, h, 10, t.amber);
  g.setTextColor(t.amber, t.panel);
  g.drawString(_q, x + (w - g.textWidth(_q)) / 2, y + 16);
  g.setTextColor(t.txt, t.panel);
  uint16_t st[4]; uint8_t ln[4];
  char buf[160];
  strlcpy(buf, _detail.c_str(), sizeof(buf));
  const int n = wrapText(g, buf, w - 30, st, ln, 3);
  for (int i = 0; i < n; i++) {
    char line[120];
    snprintf(line, sizeof(line), "%.*s", ln[i], buf + st[i]);
    g.drawString(line, x + (w - g.textWidth(line)) / 2, y + 44 + i * 18);
  }
  const int by = y + h - 40, bw = 110;
  // No is red, Yes is green; the focused one is filled, the other keeps its colour in the text.
  drawPill(g, x + w / 2 - bw - 10, by, bw, 26, !_sel ? t.red : t.line, !_sel ? t.white : t.red, "No");
  drawPill(g, x + w / 2 + 10, by, bw, 26, _sel ? t.green : t.line, _sel ? t.bg : t.green, "Yes");
}

void ConfirmView::press() {
  auto fn = _yes;
  const bool yes = _sel;
  nav.pop();
  if (yes && fn) fn();
}

// ---- TextPageView ----------------------------------------------------------------------------
void TextPageView::tick() {
  if (!_last || millis() - _last > _refresh) {
    _last = millis();
    const size_t before = _lines.size();
    _lines.clear();
    _fill(_lines);
    const int visible = (L::H - L::BODY_Y - 4) / 18;
    if (_stickBottom && _lines.size() != before) _scroll = max(0, (int)_lines.size() - visible);
    dirty = true;
  }
}

void TextPageView::rotate(int d) {
  const int visible = (L::H - L::BODY_Y - 4) / 18;
  _scroll = constrain(_scroll + d, 0, max(0, (int)_lines.size() - visible));
  dirty = true;
}

void TextPageView::draw(Canvas& g) {
  const Theme& t = nav.theme();
  drawHeader(g, _title.c_str());
  const int visible = (L::H - L::BODY_Y - 4) / 18;
  g.setTextColor(t.txt, t.bg);
  if (_lines.empty()) { g.setTextColor(t.dim, t.bg); g.drawString("nothing yet", 12, L::BODY_Y + 8); }
  for (int i = 0; i < visible && _scroll + i < (int)_lines.size(); i++) {
    char safe[160];
    sanitize(_lines[_scroll + i].c_str(), safe, sizeof(safe));
    const bool dimLine = safe[0] == '#';
    g.setTextColor(dimLine ? t.greenDim : t.txt, t.bg);
    drawRich(g, dimLine ? safe + 1 : safe, 12, L::BODY_Y + 4 + i * 18);
  }
  drawScrollbar(g, _lines.size(), _scroll, visible, L::BODY_Y, L::H - L::BODY_Y);
}
