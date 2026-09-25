// Shared kit for the animation layer (fx.cpp and each theme's fx_<theme>.cpp).
// Not for use outside the fx files.
//
// Writing a screen change (see fx_squatch.cpp etc. for real ones):
//   - The panel (P()) already shows `from` when you start; end exactly on `to`.
//   - Time with tnow(), never millis(): a USB test runs the same code on a clock
//     that ticks 16 ms a frame and stops partway.
//   - Loop:  for (uint32_t t0 = tnow();;) {
//              const float p = clamp01((tnow() - t0) / (float)ms);
//              ...work out frame p, then build and send it with frameStrips()...
//              if (p >= 1 || testStop(t0)) break;
//            }
//            if (!stopped()) pushFull(to);
//   - Build frames with frameStrips(), not in a full-screen sprite: PSRAM is slow
//     to write (filling a frame there costs 14 ms, copying one 23 ms), while a strip
//     in internal RAM is quick to build and goes out by DMA as the next is built,
//     so a whole frame costs about the 22 ms the panel needs anyway.
//   - Draw to P(), not the display: tests point it at a sprite.
//   - Sprite buffers (getBuffer()) hold RGB565 with the two bytes SWAPPED;
//     theme colours (T().green...) are plain RGB565: use sw() between them.

#pragma once
#include <math.h>
#include <functional>
#include "ui.h"
#include "fx.h"

namespace fx {
namespace k {

constexpr int W = L::W, H = L::H, CX = W / 2, CY = H / 2;
constexpr int TILE = 16, TCOLS = (W + TILE - 1) / TILE, TROWS = (H + TILE - 1) / TILE;

// The rows of the frame being built that are in memory: one strip inside
// frameStrips(), else the whole frame. The row helpers below skip any others.
extern int g_rowLo, g_rowHi;
inline bool rowOk(int y) { return y >= g_rowLo && y < g_rowHi; }

// Working tables for the animations live in PSRAM, taken the first time they're
// needed: internal RAM is tight with WiFi on, and every sound needs some of it.
template <typename T> T* psram(T*& p) {
  if (!p) p = (T*)heap_caps_calloc(1, sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p;
}

inline const Theme& T() { return nav.theme(); }
inline uint8_t style() { return nav.theme().style; }

// ---- maths -------------------------------------------------------------------------
inline float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float easeIn(float t) { return t * t; }
inline float easeOut(float t) { return 1 - (1 - t) * (1 - t); }
inline float easeInOut(float t) { return t < 0.5f ? 2 * t * t : 1 - 2 * (1 - t) * (1 - t); }
inline float easeOutCubic(float t) { const float u = 1 - t; return 1 - u * u * u; }
inline float easeInCubic(float t) { return t * t * t; }
inline float easeOutBack(float t, float s = 1.7f) { const float u = t - 1; return 1 + (s + 1) * u * u * u + s * u * u; }
// Phase of p inside [a, b], 0..1.
inline float seg(float p, float a, float b) { return clamp01((p - a) / (b - a)); }
inline uint32_t hash32(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16;
  return x;
}
inline float hashf(uint32_t x) { return (hash32(x) & 0xFFFF) / 65535.0f; }   // 0..1

// ---- colour (plain RGB565 unless noted) --------------------------------------------
inline uint16_t sw(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }    // plain <-> sprite buffer
inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)); }
// a..b by t (0..255).
inline uint16_t mix565(uint16_t a, uint16_t b, uint8_t t) {
  const int ra = a >> 11, ga = (a >> 5) & 63, ba = a & 31, rb = b >> 11, gb = (b >> 5) & 63, bb = b & 31;
  return (uint16_t)(((ra + ((rb - ra) * t >> 8)) << 11) | ((ga + ((gb - ga) * t >> 8)) << 5) | (ba + ((bb - ba) * t >> 8)));
}
inline uint16_t scale565(uint16_t c, uint8_t t) { return mix565(0, c, t); }   // darken toward black

// ---- little shapes -----------------------------------------------------------------
inline void heart(lgfx::LovyanGFX& g, int x, int y, int s, uint16_t c) {
  if (s < 1) return;
  const int r = s / 2 + 1;
  g.fillCircle(x - r + 1, y, r, c);
  g.fillCircle(x + r - 1, y, r, c);
  g.fillTriangle(x - 2 * r + 1, y + 1, x + 2 * r - 1, y + 1, x, y + 2 * r + 1, c);
}
inline void star(lgfx::LovyanGFX& g, int x, int y, int s, uint16_t c) {
  g.drawFastHLine(x - s, y, 2 * s + 1, c);
  g.drawFastVLine(x, y - s, 2 * s + 1, c);
  if (s > 1) { g.drawPixel(x - 1, y - 1, c); g.drawPixel(x + 1, y + 1, c);
               g.drawPixel(x + 1, y - 1, c); g.drawPixel(x - 1, y + 1, c); }
}
inline void bolt(lgfx::LovyanGFX& g, int x, int y, int s, uint16_t c) {
  g.fillTriangle(x + s / 3, y - s, x - s / 2, y + s / 6, x + s / 8, y + s / 6, c);
  g.fillTriangle(x - s / 3, y + s, x + s / 2, y - s / 6, x - s / 8, y - s / 6, c);
}
inline void thickLine(lgfx::LovyanGFX& g, int x0, int y0, int x1, int y1, int w, uint16_t c) {
  for (int k = -(w / 2); k <= w / 2; k++) { g.drawLine(x0, y0 + k, x1, y1 + k, c); g.drawLine(x0 + k, y0, x1 + k, y1, c); }
}

// ---- straight into sprite memory -----------------------------------------------------
// Rows are W pixels of byte-swapped RGB565. These are the fast path: a whole-row
// memcpy is ~4 us, so compositing a frame from rows of other frames costs a few ms.
inline uint16_t* bufOf(Canvas& c) { return (uint16_t*)c.getBuffer(); }

// n pixels of src row sy from column sx to dst row dy at column dx, clipped.
inline void copySeg(uint16_t* dst, int dy, int dx, const uint16_t* src, int sy, int sx, int n) {
  if (!rowOk(dy) || sy < 0 || sy >= H || n <= 0) return;
  if (dx < 0) { sx -= dx; n += dx; dx = 0; }
  if (sx < 0) { dx -= sx; n += sx; sx = 0; }
  if (dx + n > W) n = W - dx;
  if (sx + n > W) n = W - sx;
  if (n > 0) memcpy(dst + dy * W + dx, src + sy * W + sx, (size_t)n * 2);
}
inline void copyRow(uint16_t* dst, int dy, const uint16_t* src, int sy) {
  if (rowOk(dy) && sy >= 0 && sy < H) memcpy(dst + dy * W, src + sy * W, W * 2);
}
// dst row dy = src row sy moved right by off, wrapping round (a TV losing sync).
inline void copyRowWrap(uint16_t* dst, int dy, const uint16_t* src, int sy, int off) {
  if (!rowOk(dy) || sy < 0 || sy >= H) return;
  off %= W; if (off < 0) off += W;
  memcpy(dst + dy * W + off, src + sy * W, (size_t)(W - off) * 2);
  if (off) memcpy(dst + dy * W, src + sy * W + (W - off), (size_t)off * 2);
}
// n pixels of a colour already swapped with sw().
inline void fillSeg(uint16_t* dst, int y, int x, int n, uint16_t swc) {
  if (!rowOk(y)) return;
  if (x < 0) { n += x; x = 0; }
  if (x + n > W) n = W - x;
  uint16_t* p = dst + y * W + x;
  for (int i = 0; i < n; i++) p[i] = swc;
}
// Split a row's colour channels: red slides right by r px, blue left by b px -
// the "signal" fringe of the power-off's tears. Masks are for swapped pixels.
inline void chromaRow(uint16_t* dst, int y, int r, int b) {
  if (!rowOk(y)) return;
  uint16_t* p = dst + y * W;
  if (r > 0) for (int x = W - 1; x >= r; x--) p[x] = (uint16_t)((p[x] & ~0x00F8) | (p[x - r] & 0x00F8));
  if (b > 0) for (int x = 0; x < W - b; x++) p[x] = (uint16_t)((p[x] & ~0x1F00) | (p[x + b] & 0x1F00));
}
// Tile (tx, ty) of the 16 px grid of src, drawn with its top-left at (x, y).
inline void blitTile(uint16_t* dst, const uint16_t* src, int tx, int ty, int x, int y) {
  const int sx = tx * TILE, sy = ty * TILE, h = (sy + TILE <= H) ? TILE : H - sy;
  const int r0 = max(0, g_rowLo - y), r1 = min(h, g_rowHi - y);
  for (int r = r0; r < r1; r++) copySeg(dst, y + r, x, src, sy + r, sx, TILE);
}
// A row of analogue static in the theme's colours (runs of 1-8 px).
inline void noiseRow(uint16_t* dst, int y, uint32_t seed, uint16_t c0, uint16_t c1, uint16_t c2, uint16_t c3) {
  if (!rowOk(y)) return;
  const uint16_t s0 = sw(c0), s1 = sw(c1), s2 = sw(c2), s3 = sw(c3);
  uint16_t* p = dst + y * W;
  for (int x = 0; x < W;) {
    const uint32_t h = hash32(seed ^ (uint32_t)(x * 2654435761u));
    const int run = 1 + (h & 7);
    const uint8_t v = (h >> 8) & 15;
    const uint16_t c = v < 8 ? s0 : v < 12 ? s1 : v < 15 ? s2 : s3;
    for (int i = 0; i < run && x < W; i++) p[x++] = c;
  }
}

// A handful of particles, updated on the transition's own clock.
struct Mote { float x, y, vx, vy, life, age; uint16_t c; uint8_t size; bool on; };
template <size_t N> void spawnMote(Mote (&m)[N], float x, float y, float vx, float vy, float life, uint16_t c, uint8_t size) {
  for (auto& q : m) if (!q.on) { q = {x, y, vx, vy, life, 0, c, size, true}; return; }
}
template <size_t N> void stepMotes(Mote (&m)[N], float dt, float grav) {
  for (auto& q : m) if (q.on) {
    q.vy += grav * dt; q.x += q.vx * dt; q.y += q.vy * dt; q.age += dt;
    if (q.age >= q.life || q.y > H + 10 || q.x < -10 || q.x > W + 10) q.on = false;
  }
}
template <size_t N> void drawMotes(lgfx::LovyanGFX& g, Mote (&m)[N], bool square) {
  for (auto& q : m) if (q.on) {
    if (q.y + q.size < g_rowLo || q.y - q.size >= g_rowHi) continue;
    const int s = q.age < q.life * 0.6f ? q.size : (q.size > 1 ? q.size - 1 : 1);
    if (square) g.fillRect((int)q.x - s / 2, (int)q.y - s / 2, s, s, q.c);
    else g.fillCircle((int)q.x, (int)q.y, s / 2 > 0 ? s / 2 : 1, q.c);
  }
}


// ---- building a frame in strips ---------------------------------------------------------
// One strip of the frame being built: rows [y0, y1) of `out`, which is indexed like a
// whole frame (out + y * W) though only those rows exist. Row helpers skip rows
// outside it; loops over rows should run y0..y1.
//   g     a canvas over the same memory in whole-frame coordinates, clipped to the
//         strip. Its memory is only the strip, so use it only for plain shapes that
//         keep to the clip: fillRect, drawRect, drawFastH/VLine, drawPixel, drawLine,
//         fill/drawCircle, fillTriangle (and star, heart, thickLine).
//   band  the strip as a canvas of its own, whose row 0 is frame row bandY. Anything
//         else goes here, moved up by bandY: drawWideLine (so drawSasquatch) reads
//         pixels outside the clip and clears it when done.
struct Strip { uint16_t* out; int y0, y1; lgfx::LovyanGFX& g; lgfx::LovyanGFX& band; int bandY; };
// Build rows [ya, yb) of a frame a strip at a time with fn, each strip going to the
// panel by DMA while the next is built. The frame lands shaken by (sx, sy), with the
// edge that uncovers blacked out.
void frameStrips(int ya, int yb, const std::function<void(Strip&)>& fn, int sx = 0, int sy = 0);
inline bool touches(const Strip& s, int top, int bottom) { return bottom > s.y0 && top < s.y1; }

// ---- where frames go, and the clock (fx.cpp) ----------------------------------------
lgfx::LovyanGFX* P();                  // the panel, or a test sprite
uint32_t tnow();                       // millis(), or the test clock
bool testStop(uint32_t t0);            // call once per frame; true: stop here (tests)
bool stopped();                        // a test stopped partway: skip the final push
// One rectangle of a full-screen sprite to the same place on P(), optionally
// shifted by (dx, dy) (the rectangle is where it lands).
void pushRect(Canvas& src, int x, int y, int w, int h, int dx = 0, int dy = 0);
inline void pushFull(Canvas& src) { src.pushSprite(P(), 0, 0); }
Canvas* work();                        // a spare full-screen PSRAM sprite
// The haptic buzz, but not while a USB test is stepping through frames.
void buzzOnce();

}  // namespace k

// ---- each theme's own screen changes (fx_<theme>.cpp) --------------------------------
// Return true when handled; false: a screen change cuts, a wake / sleep uses the default.
// Wake: reveal `to` from a dark screen. Sleep: take `from` to dark.
bool squatchTransition(Trans kind, Canvas& from, Canvas& to);
bool blocksTransition(Trans kind, Canvas& from, Canvas& to);
bool heroTransition(Trans kind, Canvas& from, Canvas& to);
bool auroraTransition(Trans kind, Canvas& from, Canvas& to);
bool squatchWake(Canvas& to);   bool squatchSleep(Canvas& from);
bool blocksWake(Canvas& to);    bool blocksSleep(Canvas& from);
bool heroWake(Canvas& to);      bool heroSleep(Canvas& from);
bool auroraWake(Canvas& to);    bool auroraSleep(Canvas& from);

}  // namespace fx
