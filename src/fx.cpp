#include "fx.h"
#include "fx_internal.h"
#include <math.h>
#include "app.h"
#include "haptic.h"

namespace fx {
using namespace k;
namespace {

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

// ---- screen-to-screen transitions ---------------------------------------------------------
// All drawn straight to the panel, and only what changes each frame, so they run at
// the panel's speed rather than the canvas's.
namespace k {
// Normally the panel and the real clock. transitionFrame() points them at a
// sprite and a clock that ticks 16 ms per frame, and stops at a chosen moment.
lgfx::LovyanGFX* s_target = nullptr;
bool s_test = false, s_stopped = false;
uint32_t s_vclock = 0, s_stopMs = 0, s_frames = 0;
lgfx::LovyanGFX* P() { return s_target ? s_target : (lgfx::LovyanGFX*)nav.display(); }
uint32_t tnow() { return s_test ? s_vclock : millis(); }
bool testStop(uint32_t t0) {
  s_frames++;
  if (!s_test) return false;
  s_vclock += 16;
  if (s_vclock - t0 > s_stopMs) s_stopped = true;
  return s_stopped;
}

// Copy one rectangle of a full-screen sprite to the same place on the panel.
void pushRect(Canvas& src, int x, int y, int w, int h, int dx, int dy) {
  if (w <= 0 || h <= 0) return;
  lgfx::LovyanGFX* d = P();
  d->setClipRect(x, y, w, h);
  src.pushSprite(d, dx, dy);
  d->clearClipRect();
}
bool stopped() { return s_stopped; }
void buzzOnce() { if (!s_test) haptic.buzz(1); }

Canvas* work() {
  static Canvas c;
  static bool made = false;
  if (!made) { c.setColorDepth(16); c.setPsram(true); made = c.createSprite(W, H) != nullptr; }
  return made ? &c : nullptr;
}

int g_rowLo = 0, g_rowHi = H;

// Two strips' worth of internal RAM (33 kB), taken for a screen change and given back
// after it for the radio stacks: one is built while the other goes out. Each has a
// spare row under it: drawWideLine clips itself one row short of the canvas it draws
// on, so `band` is made a row taller than the strip and the row it misses is the spare.
constexpr int BAND = 16;
static uint16_t* s_band[2] = {nullptr, nullptr};
static void releaseBands() { for (auto& b : s_band) { free(b); b = nullptr; } }
static bool bandsReady() {
  if (s_band[0]) return true;
  for (auto& b : s_band) b = (uint16_t*)heap_caps_malloc(W * (BAND + 1) * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (s_band[0] && s_band[1]) return true;
  releaseBands();
  return false;
}

void frameStrips(int ya, int yb, const std::function<void(Strip&)>& fn, int sx, int sy) {
  ya = max(ya, 0); yb = min(yb, H);
  if (yb <= ya) return;
  lgfx::LovyanGFX* d = P();
  static Canvas tall, band;
  if (!bandsReady()) {
    // No internal RAM to spare: build the frame whole in a PSRAM sprite (slower).
    Canvas* w = work();
    if (!w) return;
    Strip s{bufOf(*w), ya, yb, *w, *w, 0};
    w->setClipRect(0, ya, W, yb - ya);
    g_rowLo = ya; g_rowHi = yb;
    fn(s);
    g_rowLo = 0; g_rowHi = H;
    w->clearClipRect();
    pushRect(*w, 0, ya, W, yb - ya, sx, sy);
  } else {
    d->startWrite();
    int k2 = 0;
    for (int y0 = ya; y0 < yb; y0 += BAND, k2++) {
      const int y1 = min(yb, y0 + BAND);
      uint16_t* buf = s_band[k2 & 1];
      // `tall` spans the whole frame but only this strip's rows are real memory, so
      // it's clipped to them, and so are the row helpers. `band` is just the strip.
      uint16_t* out = buf - y0 * W;
      tall.setBuffer(out, W, H, 16);
      tall.setClipRect(0, y0, W, y1 - y0);
      band.setBuffer(buf, W, y1 - y0 + 1, 16);
      g_rowLo = y0; g_rowHi = y1;
      Strip s{out, y0, y1, tall, band, y0};
      fn(s);
      // The DMA of the strip before this one finishes before this one starts, so
      // its buffer is free again by the time the next strip is built in it.
      d->pushImageDMA(sx, y0 + sy, W, y1 - y0, (const lgfx::swap565_t*)buf);
    }
    g_rowLo = 0; g_rowHi = H;
    d->endWrite();
    d->waitDMA();
  }
  if (sx > 0) d->fillRect(0, 0, sx, H, TFT_BLACK);
  if (sx < 0) d->fillRect(W + sx, 0, -sx, H, TFT_BLACK);
  if (sy > 0 && ya == 0) d->fillRect(0, 0, W, sy, TFT_BLACK);
  if (sy < 0 && yb == H) d->fillRect(0, H + sy, W, -sy, TFT_BLACK);
}
Canvas* work2() {
  static Canvas c;
  static bool made = false;
  if (!made) { c.setColorDepth(16); c.setPsram(true); made = c.createSprite(W, H) != nullptr; }
  return made ? &c : nullptr;
}
}  // namespace k

namespace {

// Squatch: a scanline sweeps the new screen on, down going in, up coming back.
void scanWipe(Canvas& from, Canvas& to, bool down, uint16_t ms) {
  const Theme& c = T();
  lgfx::LovyanGFX* d = P();
  int prev = 0;
  for (uint32_t t0 = tnow();;) {
    const float p = clamp01((tnow() - t0) / (float)ms);
    const int y = (int)(H * easeInOut(p));
    if (down) pushRect(to, 0, prev, W, y - prev);
    else      pushRect(to, 0, H - y, W, y - prev);
    if (p >= 1) break;
    // The beam, on the old side of the edge (which the next frame paints over).
    // Going up, row H-y is already new: the beam starts one row above it.
    const int e = down ? y : H - y - 1;
    const int s = down ? 1 : -1;
    d->drawFastHLine(0, e, W, TFT_WHITE);
    d->drawFastHLine(0, e + s, W, c.green);
    d->drawFastHLine(0, e + 2 * s, W, c.green);
    d->drawFastHLine(0, e + 3 * s, W, c.greenDim);
    prev = y;
    if (testStop(t0)) break;
  }
  (void)from;
}

// Blocks: the new screen flips on tile by tile in a diagonal wave; a tile shows
// a bright edge the frame it lands.
void tileWave(Canvas& from, Canvas& to, bool fromRight, uint16_t ms) {
  const Theme& c = T();
  lgfx::LovyanGFX* d = P();
  static uint8_t state[TROWS][TCOLS];                // 0 old, 1 just landed, 2 done
  memset(state, 0, sizeof(state));
  const float span = (TCOLS - 1) + (TROWS - 1) * 0.6f;
  for (uint32_t t0 = tnow();;) {
    const float p = clamp01((tnow() - t0) / (float)ms);
    for (int ty = 0; ty < TROWS; ty++)
      for (int tx = 0; tx < TCOLS; tx++) {
        const int x = tx * TILE, y = ty * TILE, h = min(TILE, H - y);
        if (state[ty][tx] == 1) { pushRect(to, x, y, TILE, h); state[ty][tx] = 2; continue; }
        if (state[ty][tx]) continue;
        const float start = ((fromRight ? TCOLS - 1 - tx : tx) + ty * 0.6f) / span * 0.8f;
        if (p < start && p < 1) continue;
        pushRect(to, x, y, TILE, h);
        if (p < 1) { d->drawRect(x, y, TILE, h, c.green); state[ty][tx] = 1; }
        else state[ty][tx] = 2;
      }
    if (p >= 1 || testStop(t0)) break;
  }
  if (!s_stopped) pushRect(to, 0, 0, W, H);
  (void)from;
}

// Hero: the new screen slides in over the old, with gold speed lines at the seam.
void comicSlide(Canvas& from, Canvas& to, bool fromRight, uint16_t ms) {
  const Theme& c = T();
  lgfx::LovyanGFX* d = P();
  for (uint32_t t0 = tnow();;) {
    const float p = clamp01((tnow() - t0) / (float)ms);
    const int x = (int)(W * easeOut(p));
    if (fromRight) { from.pushSprite(d, -x, 0); to.pushSprite(d, W - x, 0); }
    else           { from.pushSprite(d, x, 0);  to.pushSprite(d, x - W, 0); }
    if (p >= 1) break;
    const int seam = fromRight ? W - x : x;
    const int dir = fromRight ? 1 : -1;               // speed lines trail behind the new screen
    for (int k = 0; k < 4; k++) {
      const int y = 30 + k * 52 + ((k * 37 + x / 7) % 14);
      const int len = 18 + (k * 23 % 30);
      d->drawFastHLine(fromRight ? seam : seam - len, y, len, k % 2 ? c.amber : c.green);
      d->drawFastHLine(fromRight ? seam + 6 * dir : seam - len - 6, y + 3, len / 2, c.green);
    }
    if (testStop(t0)) break;
  }
  if (!s_stopped) pushRect(to, 0, 0, W, H);
}

// Aurora: a seam of light sweeps across, the new screen behind it.
void lightSweep(Canvas& from, Canvas& to, bool leftward, uint16_t ms) {
  const Theme& c = T();
  lgfx::LovyanGFX* d = P();
  const uint16_t cols[3] = {c.green, c.blue, c.greenDim};
  int prev = 0;
  for (uint32_t t0 = tnow();;) {
    const float p = clamp01((tnow() - t0) / (float)ms);
    const int w = (int)(W * easeInOut(p));           // how much is new
    if (leftward) pushRect(to, W - w, 0, w - prev, H);
    else          pushRect(to, prev, 0, w - prev, H);
    if (p >= 1) break;
    const int e = leftward ? W - w - 1 : w;           // the seam, on the old side
    const int s = leftward ? -1 : 1;
    d->drawFastVLine(e, 0, H, TFT_WHITE);
    for (int k = 0; k < 3; k++) { d->drawFastVLine(e + s * (1 + 2 * k), 0, H, cols[k]); d->drawFastVLine(e + s * (2 + 2 * k), 0, H, cols[k]); }
    prev = w;
    if (testStop(t0)) break;
  }
  (void)from;
}

// Unlock: the lock screen slides up and off, the new screen standing still behind
// it, with the theme's edge under it.
void slideUp(Canvas& from, Canvas& to, uint16_t ms) {
  const Theme& c = T();
  lgfx::LovyanGFX* d = P();
  const uint8_t st = style();
  int prev = 0;
  for (uint32_t t0 = tnow();;) {
    const float p = clamp01((tnow() - t0) / (float)ms);
    const int dy = (int)(H * easeIn(p));
    pushRect(to, 0, H - dy, W, dy - prev + 6);        // uncovered at the bottom
    pushRect(from, 0, 0, W, H - dy, 0, -dy);          // the lock screen, moved up
    if (p >= 1) break;
    const int e = H - dy;
    if (st == STYLE_AURORA) { d->drawFastHLine(0, e, W, c.green); d->drawFastHLine(0, e + 1, W, c.blue); d->drawFastHLine(0, e + 2, W, c.greenDim); }
    else if (st == STYLE_HERO) { d->fillRect(0, e, W, 2, c.green); for (int k = 0; k < 6; k++) star(*d, 40 + k * 80, e + 5, 2, c.amber); }
    else { d->fillRect(0, e, W, 2, c.green); d->drawFastHLine(0, e + 2, W, c.greenDim); }
    prev = dy;
    if (testStop(t0)) break;
  }
  if (!s_stopped) pushRect(to, 0, 0, W, H);
}

// Blocks unlock: the lock screen shatters, its tiles dropping away under gravity,
// the bottom rows first.
void shatter(Canvas& from, Canvas& to, uint16_t ms) {
  lgfx::LovyanGFX* d = P();
  for (uint32_t t0 = tnow();;) {
    const float p = clamp01((tnow() - t0) / (float)ms);
    to.pushSprite(d, 0, 0);
    bool any = false;
    for (int ty = 0; ty < TROWS; ty++)
      for (int tx = 0; tx < TCOLS; tx++) {
        const float delay = (hash32(ty * 64 + tx + 7) % 1000) / 1000.0f * 0.3f + (TROWS - 1 - ty) * 0.018f;
        const float tf = max(0.0f, p - delay);
        const int off = (int)(tf * tf * 1400);          // px: falls about the screen in ~0.4 of the run
        const int x = tx * TILE, y = ty * TILE, h = min(TILE, H - y);
        if (y + off >= H) continue;
        any = true;
        const int drift = off ? (int)(((int)(hash32(tx * 131 + ty) % 7) - 3) * tf * 30) : 0;
        pushRect(from, x + drift, y + off, TILE, min(h, H - y - off), drift, off);
      }
    if (p >= 1 || !any || testStop(t0)) break;
  }
  if (!s_stopped) to.pushSprite(d, 0, 0);
}

// Lock: the lock screen slides down over whatever was showing.
void slideDown(Canvas& from, Canvas& to, uint16_t ms) {
  const Theme& c = T();
  lgfx::LovyanGFX* d = P();
  for (uint32_t t0 = tnow();;) {
    const float p = clamp01((tnow() - t0) / (float)ms);
    const int dy = (int)(H * easeOut(p));
    pushRect(to, 0, 0, W, dy, 0, dy - H);
    if (p >= 1) break;
    d->fillRect(0, dy, W, 2, c.green);
    d->drawFastHLine(0, dy + 2, W, c.greenDim);
    if (testStop(t0)) break;
  }
  if (!s_stopped) pushRect(to, 0, 0, W, H);
  (void)from;
}
}  // namespace

// Each theme's own file (fx_<theme>.cpp) goes first; these stand in until it
// exists, and whenever it returns false the defaults below run.
__attribute__((weak)) bool squatchTransition(Trans, Canvas&, Canvas&) { return false; }
__attribute__((weak)) bool blocksTransition(Trans, Canvas&, Canvas&) { return false; }
__attribute__((weak)) bool heroTransition(Trans, Canvas&, Canvas&) { return false; }
__attribute__((weak)) bool auroraTransition(Trans, Canvas&, Canvas&) { return false; }
__attribute__((weak)) bool squatchWake(Canvas&) { return false; }
__attribute__((weak)) bool squatchSleep(Canvas&) { return false; }
__attribute__((weak)) bool blocksWake(Canvas&) { return false; }
__attribute__((weak)) bool blocksSleep(Canvas&) { return false; }
__attribute__((weak)) bool heroWake(Canvas&) { return false; }
__attribute__((weak)) bool heroSleep(Canvas&) { return false; }
__attribute__((weak)) bool auroraWake(Canvas&) { return false; }
__attribute__((weak)) bool auroraSleep(Canvas&) { return false; }

namespace {
bool themeTransition(Trans kind, Canvas& from, Canvas& to) {
  switch (style()) {
  case STYLE_BLOCKS: return blocksTransition(kind, from, to);
  case STYLE_HERO:   return heroTransition(kind, from, to);
  case STYLE_AURORA: return auroraTransition(kind, from, to);
  default:           return squatchTransition(kind, from, to);
  }
}
bool themeWake(Canvas& to) {
  switch (style()) {
  case STYLE_BLOCKS: return blocksWake(to);
  case STYLE_HERO:   return heroWake(to);
  case STYLE_AURORA: return auroraWake(to);
  default:           return squatchWake(to);
  }
}
bool themeSleep(Canvas& from) {
  switch (style()) {
  case STYLE_BLOCKS: return blocksSleep(from);
  case STYLE_HERO:   return heroSleep(from);
  case STYLE_AURORA: return auroraSleep(from);
  default:           return squatchSleep(from);
  }
}
// The default wake / sleep / power-down: render() frames through a spare sprite.
void playTo(uint8_t kind, Canvas& frame, uint16_t ms) {
  Canvas* o = work();
  if (!o) { if (kind == 0) pushFull(frame); else P()->fillScreen(TFT_BLACK); return; }
  bool buzzed = kind != 2 || s_test;
  for (uint32_t t0 = tnow();;) {
    const float p = clamp01((tnow() - t0) / (float)ms);
    if (!buzzed && p > 0.55f) { buzzed = true; haptic.buzz(1); }
    render(kind, frame, *o, p);
    o->pushSprite(P(), 0, 0);
    if (p >= 1 || testStop(t0)) break;
  }
  if (!stopped()) { if (kind == 0) pushFull(frame); else P()->fillScreen(TFT_BLACK); }
}
}  // namespace

void transition(Trans kind, Canvas& from, Canvas& to) {
  if (kind == Trans::None) { to.pushSprite(P(), 0, 0); return; }
  if (kind == Trans::Wake)  { screenOn(to); return; }
  if (kind == Trans::Sleep) { screenOff(from); return; }
  if (themeTransition(kind, from, to)) { releaseBands(); return; }
  const uint8_t st = style();
  if (kind == Trans::Unlock) { st == STYLE_BLOCKS ? shatter(from, to, 620) : slideUp(from, to, 300); return; }
  if (kind == Trans::Lock)   { slideDown(from, to, 260); return; }
  const bool fwd = kind == Trans::Forward;
  switch (st) {
  case STYLE_BLOCKS: tileWave(from, to, fwd, 230); break;
  case STYLE_HERO:   comicSlide(from, to, fwd, 200); break;
  case STYLE_AURORA: lightSweep(from, to, fwd, 220); break;
  default:           scanWipe(from, to, fwd, 200);
  }
}

// One moment of a screen change, for checking it over USB: dst starts as `from`
// (as the panel would), the transition runs on a 16 ms-a-frame clock, and stops
// once atMs has passed.
void transitionFrame(Trans kind, Canvas& from, Canvas& to, lgfx::LovyanGFX& dst, uint32_t atMs) {
  s_target = &dst; s_test = true; s_stopped = false; s_vclock = 0; s_stopMs = atMs;
  if (kind == Trans::Wake) dst.fillScreen(TFT_BLACK);      // waking starts from dark
  else from.pushSprite(&dst, 0, 0);
  transition(kind, from, to);
  s_target = nullptr; s_test = false; s_stopped = false;
}

uint32_t framesDrawn() { return s_frames; }

void screenOn(Canvas& frame)  { if (!themeWake(frame)) playTo(0, frame, 320); }
void screenOff(Canvas& frame) { if (!themeSleep(frame)) playTo(1, frame, 280); }
void powerDown(Canvas& frame) { playTo(2, frame, 1150); }

}  // namespace fx
