#include "fx.h"
#include <math.h>
#include "app.h"
#include "themes.h"
#include "haptic.h"

namespace fx {
namespace {

constexpr int W = L::W, H = L::H, CX = W / 2, CY = H / 2;

uint8_t style() { return app::themeSpec().style; }
const Theme& T() { return nav.theme(); }

float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
float easeIn(float t) { return t * t; }
float easeOut(float t) { return 1 - (1 - t) * (1 - t); }
float easeInOut(float t) { return t < 0.5f ? 2 * t * t : 1 - 2 * (1 - t) * (1 - t); }
uint32_t hash32(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16;
  return x;
}

// ---- little shapes ------------------------------------------------------------------
void heart(lgfx::LovyanGFX& g, int x, int y, int s, uint16_t c) {
  if (s < 1) return;
  const int r = s / 2 + 1;
  g.fillCircle(x - r + 1, y, r, c);
  g.fillCircle(x + r - 1, y, r, c);
  g.fillTriangle(x - 2 * r + 1, y + 1, x + 2 * r - 1, y + 1, x, y + 2 * r + 1, c);
}
void star(lgfx::LovyanGFX& g, int x, int y, int s, uint16_t c) {
  g.drawFastHLine(x - s, y, 2 * s + 1, c);
  g.drawFastVLine(x, y - s, 2 * s + 1, c);
  if (s > 1) { g.drawPixel(x - 1, y - 1, c); g.drawPixel(x + 1, y + 1, c);
               g.drawPixel(x + 1, y - 1, c); g.drawPixel(x - 1, y + 1, c); }
}
void bolt(lgfx::LovyanGFX& g, int x, int y, int s, uint16_t c) {   // s ~ half height
  g.fillTriangle(x + s / 3, y - s, x - s / 2, y + s / 6, x + s / 8, y + s / 6, c);
  g.fillTriangle(x - s / 3, y + s, x + s / 2, y - s / 6, x - s / 8, y - s / 6, c);
}
void thickLine(lgfx::LovyanGFX& g, int x0, int y0, int x1, int y1, int w, uint16_t c) {
  for (int k = -(w / 2); k <= w / 2; k++) { g.drawLine(x0, y0 + k, x1, y1 + k, c); g.drawLine(x0 + k, y0, x1 + k, y1, c); }
}

// ---- overlay state ------------------------------------------------------------------
struct Ring  { int16_t x, y; uint32_t at; uint16_t life; bool used; };
struct Spark { float x0, y0, vx, vy; uint32_t at; uint16_t life, color; float grav; uint8_t kind; bool used; };
struct Tick  { int16_t x, y; uint32_t at; bool used; };
Ring  rings[10];
Spark sparks[48];
Tick  ticks[4];
uint32_t shakeAt = 0, chargeAt = 0;
uint8_t  chargePct = 0;
constexpr uint16_t SHAKE_MS = 340, CHARGE_MS = 1500, TICK_MS = 900;

template <typename P, size_t N> P* freeSlot(P (&pool)[N]) {
  for (auto& p : pool) if (!p.used) return &p;
  P* oldest = &pool[0];                          // full: reuse the oldest
  for (auto& p : pool) if ((int32_t)(p.at - oldest->at) < 0) oldest = &p;
  return oldest;
}

void drawRing(lgfx::LovyanGFX& g, const Ring& r, uint32_t now) {
  const Theme& c = T();
  const float t = clamp01((now - r.at) / (float)r.life);
  switch (style()) {
  case STYLE_BLOCKS: {                              // a square ring placed in whole blocks
    const int s = 5 + (int)(t * 7) * 4;
    const uint16_t col = t < 0.5f ? c.green : c.greenDim;
    for (int k = -s; k < s; k += 4) {
      g.fillRect(r.x + k, r.y - s, 3, 3, col);
      g.fillRect(r.x + k, r.y + s - 3, 3, 3, col);
      g.fillRect(r.x - s, r.y + k, 3, 3, col);
      g.fillRect(r.x + s - 3, r.y + k, 3, 3, col);
    }
    if (t < 0.25f) g.fillRect(r.x - 2, r.y - 2, 5, 5, c.txt);
    break;
  }
  case STYLE_HERO: {                                // a heart pops up and floats off
    const float s = t < 0.2f ? t / 0.2f * 1.35f : 1.35f - (t - 0.2f) * 0.5f;
    const int y = r.y - (int)(easeOut(t) * 20);
    heart(g, r.x, y, (int)(6 * s) + 1, t < 0.75f ? c.red : c.amber);
    if (t > 0.12f && t < 0.85f) {
      const int d = 9 + (int)(t * 14);
      star(g, r.x - d, y - 3, 2, c.green);
      star(g, r.x + d, y + 1, 2, c.green);
    }
    break;
  }
  case STYLE_AURORA: {                              // soft rings, one colour after another
    const uint16_t cols[3] = {c.green, c.blue, c.greenDim};
    for (int k = 0; k < 3; k++) {
      const float tk = t - k * 0.16f;
      if (tk <= 0 || tk >= 1) continue;
      const int rad = 3 + (int)(easeOut(tk) * 30);
      g.drawCircle(r.x, r.y, rad, cols[k]);
      if (tk < 0.45f) g.drawCircle(r.x, r.y, rad + 1, cols[k]);
    }
    break;
  }
  default: {                                        // Squatch: radar rings off a node
    for (int k = 0; k < 2; k++) {
      const float tk = t - k * 0.22f;
      if (tk <= 0 || tk >= 1) continue;
      const int rad = 3 + (int)(easeOut(tk) * 26);
      g.drawCircle(r.x, r.y, rad, tk < 0.55f ? c.green : c.greenDim);
    }
    g.fillCircle(r.x, r.y, 3, t < 0.3f ? c.txt : c.green);
  }
  }
}

void drawSpark(lgfx::LovyanGFX& g, const Spark& s, uint32_t now) {
  const float a = (now - s.at) / 1000.0f;
  const float t = clamp01((now - s.at) / (float)s.life);
  const int x = (int)(s.x0 + s.vx * a), y = (int)(s.y0 + s.vy * a + 0.5f * s.grav * a * a);
  const Theme& c = T();
  const uint16_t col = t > 0.7f ? c.greenDim : s.color;
  switch (s.kind) {
  case STYLE_BLOCKS: g.fillRect(x - 1, y - 1, 3, 3, col); break;
  case STYLE_HERO:   star(g, x, y, t < 0.5f ? 2 : 1, col); break;
  case STYLE_AURORA: g.fillCircle(x, y, t < 0.5f ? 2 : 1, col); break;
  default: {                                         // a streak with a bright head
    const int tx = (int)(x - s.vx * 0.06f), ty = (int)(y - (s.vy + s.grav * a) * 0.06f);
    g.drawLine(tx, ty, x, y, col);
    g.drawLine(tx + 1, ty, x + 1, y, col);
    g.fillCircle(x, y, t < 0.5f ? 2 : 1, t < 0.6f ? c.txt : col);
  }
  }
}

void drawTick(lgfx::LovyanGFX& g, const Tick& k, uint32_t now) {
  const Theme& c = T();
  const float t = clamp01((now - k.at) / (float)TICK_MS);
  const float d = clamp01(t / 0.35f);               // drawn over the first third
  const int ax = k.x - 6, ay = k.y, bx = k.x - 2, by = k.y + 5, cx = k.x + 7, cy = k.y - 5;
  const float f1 = clamp01(d / 0.35f), f2 = clamp01((d - 0.35f) / 0.65f);
  const int ex1 = ax + (int)((bx - ax) * f1), ey1 = ay + (int)((by - ay) * f1);
  const int ex2 = bx + (int)((cx - bx) * f2), ey2 = by + (int)((cy - by) * f2);
  const uint8_t st = style();
  const uint16_t col = st == STYLE_HERO ? c.green : (t > 0.8f ? c.greenDim : c.green);
  // A badge behind it so it reads over anything: pops in, then stays.
  const int br = t < 0.12f ? (int)(t / 0.12f * 14) : 12;
  if (st == STYLE_BLOCKS) { g.fillRect(k.x - br, k.y - br, 2 * br, 2 * br, c.panel); g.drawRect(k.x - br, k.y - br, 2 * br, 2 * br, col); }
  else { g.fillCircle(k.x, k.y, br, c.panel); g.drawCircle(k.x, k.y, br, col); }
  if (st == STYLE_AURORA) {                          // a glow under the stroke
    thickLine(g, ax, ay, ex1, ey1, 4, c.greenDim);
    if (f2 > 0) thickLine(g, bx, by, ex2, ey2, 4, c.greenDim);
  }
  if (st == STYLE_BLOCKS) {                          // stepped pixel stroke
    for (float u = 0; u <= f1; u += 0.2f) g.fillRect(ax + (int)((bx - ax) * u) - 1, ay + (int)((by - ay) * u) - 1, 3, 3, col);
    for (float u = 0; u <= f2; u += 0.1f) g.fillRect(bx + (int)((cx - bx) * u) - 1, by + (int)((cy - by) * u) - 1, 3, 3, col);
  } else {
    thickLine(g, ax, ay, ex1, ey1, 2, col);
    if (f2 > 0) thickLine(g, bx, by, ex2, ey2, 2, col);
  }
  if (st == STYLE_HERO && d >= 1 && t < 0.75f) star(g, cx + 3, cy - 3, 3, c.amber);
}

void drawCharge(lgfx::LovyanGFX& g, uint32_t now) {
  const Theme& c = T();
  const float t = clamp01((now - chargeAt) / (float)CHARGE_MS);
  const float fill = easeOut(clamp01(t / 0.6f));
  const int shown = (int)(chargePct * fill + 0.5f);
  const int bw = 190, bh = 74, bx = CX - bw / 2, by = CY - bh / 2 + 6;
  if (t > 0.85f && ((int)(t * 40)) % 2) return;     // flickers out at the end
  g.fillRoundRect(bx, by, bw, bh, 12, c.panel);
  g.drawRoundRect(bx, by, bw, bh, 12, c.green);
  const int ix = bx + 40, iy = by + bh / 2;
  switch (style()) {
  case STYLE_BLOCKS: {                              // a pixel battery filling block by block
    g.drawRect(ix - 24, iy - 12, 44, 24, c.txt);
    g.fillRect(ix + 20, iy - 5, 4, 10, c.txt);
    const int blocks = (shown + 9) / 10;
    for (int i = 0; i < 10; i++)
      if (i < blocks) g.fillRect(ix - 21 + (i % 5) * 8, iy - 9 + (i / 5) * 9, 6, 7, i < 2 ? c.red : c.green);
    break;
  }
  case STYLE_HERO: {                                // heart containers filling up
    for (int i = 0; i < 5; i++) {
      const int hx = ix - 26 + i * 13;
      heart(g, hx, iy - 3, 5, c.line);
      if (shown >= (i + 1) * 20 - 10) heart(g, hx, iy - 3, 5, c.red);
    }
    break;
  }
  case STYLE_AURORA: {                              // a ring washing through the aurora's colours
    const uint16_t cols[3] = {c.green, c.blue, c.greenDim};
    g.drawCircle(ix, iy, 24, c.line);
    const float end = 360.0f * shown / 100.0f;
    for (float a = 0; a < end; a += 12) g.fillArc(ix, iy, 20, 25, -90 + a, -90 + min(a + 12, end), cols[(int)(a / 12) % 3]);
    bolt(g, ix, iy, 11, c.txt);
    break;
  }
  default: {                                        // a bolt inside a filling ring
    g.drawCircle(ix, iy, 24, c.line);
    if (shown) g.fillArc(ix, iy, 20, 25, -90, -90 + 360.0f * shown / 100.0f, c.green);
    bolt(g, ix, iy, 12, t < 0.15f ? c.txt : c.amber);
  }
  }
  char b[8];
  snprintf(b, sizeof(b), "%d%%", shown);
  g.setFont(&fonts::Font4);
  g.setTextDatum(textdatum_t::middle_left);
  g.setTextColor(c.txt, c.panel);
  g.drawString(b, bx + 82, iy - 8);
  g.setFont(&fonts::Font2);
  g.setTextColor(c.dim, c.panel);
  g.drawString("charging", bx + 84, iy + 16);
  g.setTextDatum(textdatum_t::top_left);
}

// ---- transitions ------------------------------------------------------------------------
// Each draws the whole screen at progress p (0..1) into dst.

// Squatch: an old CRT. Off squeezes the picture to a line, the line to a dot.
void inwOff(Canvas& f, lgfx::LovyanGFX& d, float p, bool final) {
  const Theme& c = T();
  d.fillScreen(TFT_BLACK);
  const float a = final ? 0.5f : 0.72f, b = final ? 0.72f : 1.0f;
  if (p < a) {
    const float t = p / a;
    f.pushRotateZoom(&d, CX, CY, 0, 1 + 0.1f * t, max(0.012f, 1 - t * t));
    if (t > 0.55f) d.drawFastHLine(0, CY, W, TFT_WHITE);
  } else if (p < b) {
    const float t = (p - a) / (b - a);
    const int half = max(1, (int)(W / 2 * (1 - t * t)));
    d.fillRect(CX - half, CY - 2, half * 2, 5, c.greenDim);
    d.fillRect(CX - half, CY - 1, half * 2, 3, c.green);
    d.drawFastHLine(CX - half, CY, half * 2, TFT_WHITE);
  } else if (final) {
    const float t = (p - b) / (1 - b);
    if (t < 0.4f)       { d.fillCircle(CX, CY, 6, c.greenDim); d.fillCircle(CX, CY, 3, c.green); d.fillCircle(CX, CY, 1, TFT_WHITE); }
    else if (t < 0.75f) { d.fillCircle(CX, CY, 3, c.greenDim); d.fillCircle(CX, CY, 1, c.green); }
    else if (t < 1)     d.fillCircle(CX, CY, 1, c.greenDim);
  }
}
void inwOn(Canvas& f, lgfx::LovyanGFX& d, float p) {
  const Theme& c = T();
  d.fillScreen(TFT_BLACK);
  if (p < 0.14f) {                                  // a dot warms up
    const int r = 1 + (int)(p / 0.14f * 4);
    d.fillCircle(CX, CY, r + 2, c.greenDim);
    d.fillCircle(CX, CY, r, c.green);
    d.fillCircle(CX, CY, 1, TFT_WHITE);
  } else if (p < 0.38f) {                           // stretches into the scanline
    const int half = max(2, (int)(W / 2 * easeOut((p - 0.14f) / 0.24f)));
    d.fillRect(CX - half, CY - 2, half * 2, 5, c.greenDim);
    d.fillRect(CX - half, CY - 1, half * 2, 3, c.green);
    d.drawFastHLine(CX - half, CY, half * 2, TFT_WHITE);
  } else {                                          // and the picture opens out of it
    const float t = (p - 0.38f) / 0.62f;
    f.pushRotateZoom(&d, CX, CY, 0, 1 + 0.08f * (1 - t), max(0.012f, easeOut(t)));
    if (t < 0.3f) d.drawFastHLine(0, CY, W, TFT_WHITE);
  }
}

// Blocks: the screen is 16 px tiles. Off breaks them away top first; on builds
// them up from the ground. A tile about to change shows as a cracked block.
constexpr int TILE = 16, TCOLS = (W + TILE - 1) / TILE, TROWS = (H + TILE - 1) / TILE;
float tileRank(int tx, int ty, bool fromBottom) {
  const float rnd = (hash32(ty * 64 + tx + 1) % 1000) / 1000.0f;
  const float row = ty / (float)(TROWS - 1);
  return rnd * 0.7f + (fromBottom ? 1 - row : row) * 0.3f;
}
void crackedTile(lgfx::LovyanGFX& d, int x, int y, int h, const Theme& c) {
  d.fillRect(x, y, TILE, h, c.panel);
  d.drawRect(x, y, TILE, h, c.greenDim);
  d.drawLine(x + 3, y + 3, x + 8, y + 9, c.bg);
  d.drawLine(x + 8, y + 9, x + 13, y + 6, c.bg);
}
void blocksOff(Canvas& f, lgfx::LovyanGFX& d, float p, bool final) {
  const Theme& c = T();
  const float end = final ? 0.8f : 1.0f;
  if (p >= end) {                                   // the last block blinks out
    d.fillScreen(TFT_BLACK);
    const float t = (p - end) / (1 - end);
    if (t < 0.7f && ((int)(t * 9)) % 2 == 0) { d.fillRect(CX - 8, CY - 8, TILE, TILE, c.green); d.drawRect(CX - 8, CY - 8, TILE, TILE, c.txt); }
    return;
  }
  f.pushSprite(&d, 0, 0);
  const float q = p / end * 1.12f;
  for (int ty = 0; ty < TROWS; ty++)
    for (int tx = 0; tx < TCOLS; tx++) {
      const float r = tileRank(tx, ty, false);
      const int x = tx * TILE, y = ty * TILE, h = min(TILE, H - y);
      if (r < q - 0.1f) d.fillRect(x, y, TILE, h, TFT_BLACK);
      else if (r < q) crackedTile(d, x, y, h, c);
    }
}
void blocksOn(Canvas& f, lgfx::LovyanGFX& d, float p) {
  const Theme& c = T();
  f.pushSprite(&d, 0, 0);
  const float q = p * 1.12f;
  for (int ty = 0; ty < TROWS; ty++)
    for (int tx = 0; tx < TCOLS; tx++) {
      const float r = tileRank(tx, ty, true);
      const int x = tx * TILE, y = ty * TILE, h = min(TILE, H - y);
      if (r >= q) d.fillRect(x, y, TILE, h, TFT_BLACK);
      else if (r > q - 0.1f) { d.fillRect(x, y, TILE, h, c.green); d.drawRect(x, y, TILE, h, c.txt); }   // just placed
    }
}

// Hero: a cartoon iris, with a gold rim. Power off ends on one heartbeat.
const int IRIS_R = (int)sqrtf((float)(W * W + H * H)) / 2 + 3;
void iris(Canvas& f, lgfx::LovyanGFX& d, int r, uint16_t rim) {
  f.pushSprite(&d, 0, 0);
  for (int y = 0; y < H; y++) {
    const int dy = y - CY;
    if (r <= 0 || abs(dy) >= r) { d.drawFastHLine(0, y, W, TFT_BLACK); continue; }
    const int half = (int)sqrtf((float)(r * r - dy * dy));
    if (CX - half > 0) d.drawFastHLine(0, y, CX - half, TFT_BLACK);
    if (CX + half < W) d.drawFastHLine(CX + half, y, W - CX - half, TFT_BLACK);
  }
  if (r > 3 && r < IRIS_R) { d.drawCircle(CX, CY, r, rim); d.drawCircle(CX, CY, r + 1, rim); d.drawCircle(CX, CY, r + 2, TFT_BLACK); }
}
void heroOff(Canvas& f, lgfx::LovyanGFX& d, float p, bool final) {
  const Theme& c = T();
  const float end = final ? 0.7f : 1.0f;
  if (p < end) { iris(f, d, (int)(IRIS_R * (1 - easeIn(p / end))), c.green); return; }
  d.fillScreen(TFT_BLACK);
  const float t = (p - end) / (1 - end);            // lub-dub, then gone
  const float s = t < 0.25f ? 1 + t / 0.25f * 0.5f : t < 0.45f ? 1.5f - (t - 0.25f) * 2.5f
                : t < 0.6f ? 1 + (t - 0.45f) * 3 : max(0.0f, 1.45f * (1 - (t - 0.6f) / 0.4f));
  heart(d, CX, CY - 4, (int)(8 * s), c.red);
}
void heroOn(Canvas& f, lgfx::LovyanGFX& d, float p) { iris(f, d, (int)(IRIS_R * easeOut(p)), T().green); }

// Aurora: two curtains of light with a wavy, shimmering seam.
void curtains(Canvas& f, lgfx::LovyanGFX& d, int covered, float phase, bool picture) {
  const Theme& c = T();
  if (picture) f.pushSprite(&d, 0, 0); else d.fillScreen(TFT_BLACK);
  const uint16_t cols[3] = {c.green, c.blue, c.greenDim};
  for (int y = 0; y < H; y++) {
    const int off = (int)(sinf(y * 0.075f + phase) * 5 + sinf(y * 0.021f - phase * 0.6f) * 3);
    const int l = max(0, covered + off), r = min(W, W - covered - off);
    if (l > 0) d.drawFastHLine(0, y, l, TFT_BLACK);
    if (r < W) d.drawFastHLine(r, y, W - r, TFT_BLACK);
    if (covered <= 0) continue;
    if (l < r) {
      for (int k = 0; k < 3; k++) {
        const uint16_t col = cols[(k + y / 24) % 3];
        d.drawFastHLine(l + k * 2, y, 2, col);
        d.drawFastHLine(r - (k + 1) * 2, y, 2, col);
      }
    } else {                                        // the edges have met: this row is the seam
      const int m = (l + r) / 2;
      for (int k = 0; k < 3; k++) d.drawFastHLine(m - 3 + k * 2, y, 2, cols[(k + y / 24) % 3]);
    }
  }
}
void seam(lgfx::LovyanGFX& d, float glow) {        // the glowing line where they meet
  const Theme& c = T();
  d.fillScreen(TFT_BLACK);
  if (glow <= 0) return;
  const int w = max(1, (int)(glow * 4));
  d.fillRect(CX - w - 2, 0, 2 * w + 5, H, c.greenDim);
  d.fillRect(CX - w, 0, 2 * w + 1, H, c.blue);
  d.drawFastVLine(CX, 0, H, glow > 0.4f ? c.green : c.blue);
}
void auroraOff(Canvas& f, lgfx::LovyanGFX& d, float p, bool final) {
  const float end = final ? 0.7f : 1.0f;
  if (p < end) { curtains(f, d, (int)((W / 2 + 12) * easeInOut(p / end)), p * 14, true); return; }
  seam(d, 1 - (p - end) / (1 - end));
}
void auroraOn(Canvas& f, lgfx::LovyanGFX& d, float p) {
  if (p < 0.15f) { seam(d, p / 0.15f); return; }
  curtains(f, d, (int)((W / 2 + 12) * (1 - easeInOut((p - 0.15f) / 0.85f))), p * 14, true);
}

Canvas s_scratch;
bool s_scratchMade = false;

void play(uint8_t kind, Canvas& frame, uint16_t ms) {
  Canvas* o = scratch();
  LGFX* d = nav.display();
  if (!o) {                                          // no memory for it: just cut
    if (kind == 0) frame.pushSprite(d, 0, 0); else d->fillScreen(TFT_BLACK);
    return;
  }
  const uint32_t t0 = millis();
  bool buzzed = kind != 2;
  for (;;) {
    const float p = clamp01((millis() - t0) / (float)ms);
    if (!buzzed && p > 0.55f) { buzzed = true; haptic.buzz(1); }
    render(kind, frame, *o, p);
    o->pushSprite(d, 0, 0);
    if (p >= 1) break;
  }
  if (kind == 0) frame.pushSprite(d, 0, 0);          // land exactly on the real frame
  else d->fillScreen(TFT_BLACK);
}

}  // namespace

// ---- public ---------------------------------------------------------------------------------
void ping(int x, int y) {
  Ring* r = freeSlot(rings);
  *r = {(int16_t)x, (int16_t)y, millis(), (uint16_t)(style() == STYLE_AURORA ? 1000 : 750), true};
}

void burst(int x, int y) {
  const Theme& c = T();
  const uint8_t st = style();
  const uint32_t now = millis();
  const int n = st == STYLE_BLOCKS ? 14 : 12;
  for (int i = 0; i < n; i++) {
    Spark* s = freeSlot(sparks);
    const uint32_t h = hash32(now * 31 + i * 7919);
    const float ang = (h & 1023) / 1023.0f * 6.2832f;
    const float sp = 45 + (h >> 10) % 85;
    s->x0 = x; s->y0 = y;
    s->vx = cosf(ang) * sp;
    s->vy = sinf(ang) * sp;
    s->grav = 0;
    s->life = 650;
    if (st == STYLE_BLOCKS || st == STYLE_HERO) { s->vy -= 70; s->grav = 320; }   // thrown up, then falls
    if (st == STYLE_AURORA) { s->vx *= 0.35f; s->vy = -18.0f - (h >> 20) % 34; s->life = 1000; }   // drifts up
    const uint16_t pal[4] = {c.green, c.txt, st == STYLE_BLOCKS ? c.amber : c.greenDim, c.blue};
    s->color = pal[(h >> 24) % (st == STYLE_AURORA ? 4 : 3)];
    s->kind = st;
    s->at = now;
    s->used = true;
  }
}

void check(int x, int y) { Tick* k = freeSlot(ticks); *k = {(int16_t)x, (int16_t)y, millis(), true}; }
void fail() { shakeAt = millis() | 1; }
void charge(uint8_t pct) { chargeAt = millis() | 1; chargePct = pct > 100 ? 100 : pct; }

bool active() {
  const uint32_t now = millis();
  bool any = false;
  for (auto& r : rings)  { if (r.used && now - r.at >= r.life) r.used = false; any |= r.used; }
  for (auto& s : sparks) { if (s.used && now - s.at >= s.life) s.used = false; any |= s.used; }
  for (auto& k : ticks)  { if (k.used && now - k.at >= TICK_MS) k.used = false; any |= k.used; }
  if (shakeAt && (int32_t)(now - shakeAt) >= SHAKE_MS) shakeAt = 0;
  if (chargeAt && (int32_t)(now - chargeAt) >= CHARGE_MS) chargeAt = 0;
  return any || shakeAt || chargeAt;
}

void draw(lgfx::LovyanGFX& g) {
  if (!active()) return;
  const uint32_t now = millis();
  for (auto& r : rings)  if (r.used) drawRing(g, r, now);
  for (auto& s : sparks) if (s.used) drawSpark(g, s, now);
  for (auto& k : ticks)  if (k.used) drawTick(g, k, now);
  if (shakeAt) {                                     // the red flash round the edge
    const float t = (now - shakeAt) / (float)SHAKE_MS;
    if (t < 0.75f) for (int i = 0; i < 3; i++) g.drawRect(i, i, W - 2 * i, H - 2 * i, T().red);
  }
  if (chargeAt) drawCharge(g, now);
}

int shakeX() {
  if (!shakeAt) return 0;
  const float t = (millis() - shakeAt) / (float)SHAKE_MS;
  if (t >= 1) return 0;
  return (int)(sinf(t * 38) * 6 * (1 - t));
}

void render(uint8_t kind, Canvas& f, lgfx::LovyanGFX& d, float p) {
  p = clamp01(p);
  switch (style()) {
  case STYLE_BLOCKS: kind == 0 ? blocksOn(f, d, p) : blocksOff(f, d, p, kind == 2); break;
  case STYLE_HERO:   kind == 0 ? heroOn(f, d, p)   : heroOff(f, d, p, kind == 2);   break;
  case STYLE_AURORA: kind == 0 ? auroraOn(f, d, p) : auroraOff(f, d, p, kind == 2); break;
  default:           kind == 0 ? inwOn(f, d, p)    : inwOff(f, d, p, kind == 2);
  }
}

Canvas* scratch() {
  if (!s_scratchMade) {
    s_scratch.setColorDepth(16);
    s_scratch.setPsram(true);
    s_scratchMade = s_scratch.createSprite(W, H) != nullptr;
  }
  return s_scratchMade ? &s_scratch : nullptr;
}

void radar(lgfx::LovyanGFX& g, int cx, int cy, int r, float sweep) {
  const Theme& c = T();
  const uint8_t st = style();
  // The scope: square rings for Blocks, round for the rest.
  for (int k = 1; k <= 3; k++) {
    const int rr = r * k / 3;
    if (st == STYLE_BLOCKS) g.drawRect(cx - rr, cy - rr, 2 * rr + 1, 2 * rr + 1, k == 3 ? c.greenDim : c.line);
    else g.drawCircle(cx, cy, rr, k == 3 ? c.greenDim : c.line);
  }
  g.drawFastHLine(cx - r, cy, 2 * r + 1, c.line);
  g.drawFastVLine(cx, cy - r, 2 * r + 1, c.line);
  if (st == STYLE_HERO) {                             // a compass rose on the map
    g.setFont(&fonts::Font0);
    g.setTextColor(c.green);
    g.setTextDatum(textdatum_t::middle_center);
    g.drawString("N", cx, cy - r - 6);
    g.setTextDatum(textdatum_t::top_left);
    g.setFont(&fonts::Font2);
  }
  if (sweep < 0) return;
  // The beam: a bright leading edge with a trail fading behind it.
  const int trail = st == STYLE_AURORA ? 14 : 9;
  const uint16_t auroraCols[3] = {c.green, c.blue, c.greenDim};
  for (int k = trail; k >= 0; k--) {
    const float a = sweep - k * 0.045f;
    const int x = cx + (int)(cosf(a) * r), y = cy + (int)(sinf(a) * r);
    uint16_t col = k == 0 ? c.txt : k < trail / 3 ? c.green : c.greenDim;
    if (st == STYLE_AURORA) col = k == 0 ? c.txt : auroraCols[(k / 3) % 3];
    if (st == STYLE_HERO && k > 0) col = k < trail / 2 ? c.green : c.amber;
    if (st == STYLE_BLOCKS) {                        // a beam of blocks
      for (int s = 4; s < r; s += 6) g.fillRect(cx + (int)(cosf(a) * s) - 1, cy + (int)(sinf(a) * s) - 1, 3, 3, col);
    } else {
      g.drawLine(cx, cy, x, y, col);
    }
  }
  g.fillCircle(cx, cy, 2, c.txt);
}

void blip(lgfx::LovyanGFX& g, int x, int y, float fresh, bool focus) {
  const Theme& c = T();
  const uint16_t col = fresh > 0.5f ? c.txt : c.green;
  switch (style()) {
  case STYLE_BLOCKS: g.fillRect(x - 3, y - 3, 7, 7, col); if (focus) g.drawRect(x - 6, y - 6, 13, 13, c.amber); break;
  case STYLE_HERO:   star(g, x, y, fresh > 0.5f ? 5 : 3, focus ? c.amber : col); if (focus) g.drawCircle(x, y, 8, c.amber); break;
  case STYLE_AURORA: g.fillCircle(x, y, 4, c.greenDim); g.fillCircle(x, y, 2, col); if (focus) g.drawCircle(x, y, 8, c.blue); break;
  default:           g.fillCircle(x, y, 3, col); if (focus) g.drawCircle(x, y, 7, c.amber);
  }
}

void screenOn(Canvas& frame)  { play(0, frame, 320); }
void screenOff(Canvas& frame) { play(1, frame, 280); }
void powerDown(Canvas& frame) { play(2, frame, 1150); }

}  // namespace fx
