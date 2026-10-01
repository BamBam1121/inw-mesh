// Halloween: its own screen changes (the theme has the Squatch look everywhere else).
//   forward  Slime - it runs down the screen in fingers of different lengths, and the
//            new screen is what it leaves behind.
//   back     Bats - a swarm crosses right to left and takes the old screen with it.
//   unlock   The doors - the lock screen parts down the middle and swings open, a few
//            bats get out, and home is behind it.
//   lock     Lights out - the dark closes in, a jack-o'-lantern's face lights up with
//            the lock screen showing through its eyes and grin, then the rest follows.
//   wake     Lightning: two flashes, and the picture is there.
//   sleep    The picture melts off the bottom of the screen. Powering down, a carved
//            grin is the last thing to go out.
// Everything is measured from the screen's own width and height (k::W, k::H), so the
// pager's wide screen and the T-Deck's taller one each get it at their own size.

#include "fx_internal.h"

namespace fx {
using namespace k;
namespace {

// A bat at (x, y), sc times the small size, wings at flap (-1..1). Plain shapes only,
// so it can go on a strip's clipped canvas.
void bat(lgfx::LovyanGFX& g, int x, int y, int sc, float flap, uint16_t c) {
  const int tip = (int)(flap * 4 * sc);
  g.fillTriangle(x - sc, y, x - 8 * sc, y - 2 * sc + tip, x - 4 * sc, y + 2 * sc, c);
  g.fillTriangle(x + sc, y, x + 8 * sc, y - 2 * sc + tip, x + 4 * sc, y + 2 * sc, c);
  g.fillRect(x - sc, y - sc, 2 * sc + 1, 2 * sc + 1, c);
  g.drawPixel(x - sc, y - sc - 1, c); g.drawPixel(x + sc, y - sc - 1, c);
}

inline int shag(int y) { return (int)(hash32((y >> 2) * 7 + 3) % 11) - 5; }

// ---- forward: slime ----------------------------------------------------------------------
void slime(Canvas& from, Canvas& to, uint16_t ms) {
  const Theme& c = T();
  const uint16_t* A = bufOf(from);
  const uint16_t* B = bufOf(to);
  constexpr int CW = 12, NC = (W + CW - 1) / CW;
  const int thick = H / 4;
  float lag[NC];                                            // how far each finger hangs back
  for (int i = 0; i < NC; i++) lag[i] = (0.5f + 0.5f * sinf(i * 0.47f + 1.1f)) * 0.2f * H + (0.5f + 0.5f * sinf(i * 1.9f)) * 0.07f * H + hashf(i * 7 + 3) * 0.05f * H;
  const float travel = H + thick + 0.32f * H + 4;
  const uint16_t body = sw(c.amber), shine = sw(mix565(c.amber, 0xFFFF, 120)), edge = sw(mix565(c.amber, 0, 90));
  for (uint32_t t0 = tnow();;) {
    const float p = clamp01((tnow() - t0) / (float)ms);
    const float adv = travel * easeInOut(p);
    frameStrips(0, H, [&](Strip& s) {
      for (int y = s.y0; y < s.y1; y++)
        for (int i = 0; i < NC; i++) {
          const int x = i * CW, n = min(CW, W - x), head = (int)(adv - lag[i]);
          if (head <= y) { copySeg(s.out, y, x, A, y, x, n); continue; }          // not reached yet
          if (y < head - thick) { copySeg(s.out, y, x, B, y, x, n); continue; }   // left behind: the new screen
          const int up = head - 1 - y;                       // rows above the finger's tip
          if (up < 3) {                                      // the tip is rounded
            copySeg(s.out, y, x, A, y, x, n);
            fillSeg(s.out, y, x + 3 - up, n - 2 * (3 - up), body);
          } else {
            fillSeg(s.out, y, x, n, y == head - thick ? edge : body);
            fillSeg(s.out, y, x + 2, 2, shine);
            fillSeg(s.out, y, x + n - 1, 1, edge);
          }
        }
    });
    if (p >= 1 || testStop(t0)) break;
  }
  if (!stopped()) pushFull(to);
}

// ---- back: bats ----------------------------------------------------------------------------
void bats(Canvas& from, Canvas& to, uint16_t ms) {
  const Theme& c = T();
  const uint16_t* A = bufOf(from);
  const uint16_t* B = bufOf(to);
  const uint16_t dark = mix565(c.blue, c.bg, 150);
  constexpr int N = 20;
  for (uint32_t t0 = tnow();;) {
    const uint32_t el = tnow() - t0;
    const float p = clamp01(el / (float)ms), t = el / 1000.0f;
    const float front = lerpf(W + 80, -80, easeInOut(p));   // the swarm's middle; behind it, the screen before
    frameStrips(0, H, [&](Strip& s) {
      for (int y = s.y0; y < s.y1; y++) {
        const int e = constrain((int)(front + 12 * sinf(y * 0.07f + t * 16)) + shag(y), 0, W);
        if (e > 0) copySeg(s.out, y, 0, A, y, 0, e);
        if (e < W) copySeg(s.out, y, e, B, y, e, W - e);
        if (!(y & 1) && e > 0 && e < W - 2) fillSeg(s.out, y, e, 2, sw(c.green));
      }
      for (int i = 0; i < N; i++) {
        const int sc = i % 4 == 0 ? 3 : i % 2 ? 2 : 1;
        const int bx = (int)(front + (hashf(i * 5 + 1) - 0.45f) * 150 + sinf(t * 7 + i) * 8);
        const int by = (int)(hashf(i * 3 + 11) * (H + 20)) - 10 + (int)(sinf(t * 9 + i * 1.7f) * 10);
        if (!touches(s, by - 12 * sc, by + 8 * sc)) continue;
        bat(s.g, bx, by, sc, sinf(t * 28 + i * 2.3f), i % 3 ? c.blue : dark);
        if (sc == 3) { s.g.drawPixel(bx - 1, by, c.green); s.g.drawPixel(bx + 1, by, c.green); }
      }
    });
    if (p >= 1 || testStop(t0)) break;
  }
  if (!stopped()) pushFull(to);
}

// ---- unlock: the doors -----------------------------------------------------------------------
void doors(Canvas& from, Canvas& to, uint16_t ms) {
  const Theme& c = T();
  const uint16_t* A = bufOf(from);
  const uint16_t* B = bufOf(to);
  static int16_t col[W];                                    // for each door column, the lock screen's column it shows
  uint32_t frame = 0;
  for (uint32_t t0 = tnow();; frame++) {
    const uint32_t el = tnow() - t0;
    const float p = clamp01(el / (float)ms), t = el / 1000.0f;
    const float e = easeInOut(seg(p, 0.14f, 1));
    const int wd = CX - (int)(CX * e);                      // how wide each door still looks, hinged at its outer edge
    for (int x = 0; x < wd; x++) { col[x] = (int16_t)(x * CX / wd); col[W - 1 - x] = (int16_t)(W - 1 - x * CX / wd); }
    // They rattle before they give.
    const int shake = p < 0.14f ? (int)((hashf(frame * 3 + 1) - 0.5f) * 5) : 0;
    const float u = seg(p, 0.2f, 0.95f);                    // the bats that get out
    frameStrips(0, H, [&](Strip& s) {
      for (int y = s.y0; y < s.y1; y++) {
        uint16_t* o = s.out + y * W;
        const uint16_t* a = A + y * W;
        if (wd < CX) copySeg(s.out, y, wd, B, y, wd, W - 2 * wd);
        for (int x = 0; x < wd; x++) { o[x] = a[col[x]]; o[W - 1 - x] = a[col[W - 1 - x]]; }
      }
      if (wd > 0 && wd < CX) {                              // light from inside, down each door's edge
        s.g.drawFastVLine(wd, s.y0, s.y1 - s.y0, c.green);
        s.g.drawFastVLine(wd + 1, s.y0, s.y1 - s.y0, c.greenDim);
        s.g.drawFastVLine(W - 1 - wd, s.y0, s.y1 - s.y0, c.green);
        s.g.drawFastVLine(W - 2 - wd, s.y0, s.y1 - s.y0, c.greenDim);
      }
      if (u > 0 && u < 1)
        for (int i = 0; i < 9; i++) {
          const float dir = i % 2 ? 1.0f : -1.0f, r1 = hashf(i * 7 + 2), r2 = hashf(i * 11 + 5);
          const int bx = CX + (int)(dir * (10 + r1 * W * 0.55f) * u);
          const int by = CY + (int)((r2 - 0.35f) * H * 0.3f) - (int)(u * H * (0.25f + r2 * 0.4f)) + (int)(sinf(t * 10 + i) * 6);
          const int sc = i % 3 == 0 ? 2 : 1;
          if (touches(s, by - 8 * sc, by + 6 * sc)) bat(s.g, bx, by, sc, sinf(t * 30 + i * 2.1f), c.blue);
        }
    }, shake, 0);
    if (p >= 1 || testStop(t0)) break;
  }
  if (!stopped()) pushFull(to);
}

// ---- lock: lights out, and a jack-o'-lantern ---------------------------------------------------
// The face, in units of its own size about its middle: two eyes, a nose, a grin with teeth.
inline float tri01(float v) { v -= floorf(v); return v < 0.5f ? v * 2 : 2 - v * 2; }
bool inFace(float x, float y) {
  const float ax = fabsf(x);
  if (y > -0.68f && y < -0.16f && fabsf(ax - 0.55f) < (y + 0.68f) / 0.52f * 0.30f) return true;   // eyes
  if (y > -0.10f && y < 0.14f && ax < (y + 0.10f) / 0.24f * 0.13f) return true;                   // nose
  if (ax < 1.12f) {                                                                              // the grin
    const float top = 0.36f - 0.30f * x * x + 0.10f * tri01(x * 2.5f + 0.25f);
    const float bot = 0.80f - 0.55f * x * x - 0.10f * tri01(x * 2.5f + 0.75f);
    if (y > top && y < bot) return true;
  }
  return false;
}

void lantern(Canvas& from, Canvas& to, uint16_t ms) {
  const Theme& c = T();
  const uint16_t* A = bufOf(from);
  const uint16_t* B = bufOf(to);
  const float unit = H * 0.36f, shut = 0.22f;
  const float R0 = sqrtf((float)(CX * CX + CY * CY)) + 4;
  const uint16_t skin = sw(mix565(c.bg, c.green, 120)), rim = sw(mix565(c.green, 0xFFFF, 90));   // pumpkin skin, and the lit edge of each cut
  bool lit = false;
  for (uint32_t t0 = tnow();;) {
    const float p = clamp01((tnow() - t0) / (float)ms);
    if (!lit && p >= shut) { lit = true; buzzOnce(); }
    const float r = R0 * (1 - easeIn(seg(p, 0, shut)));     // the dark closing in on the old screen
    // The face pops alight, sits a moment, then swells as the rest of the screen comes through.
    const float sc = p < shut ? 0 : easeOutBack(seg(p, shut, 0.42f)) * (1 + 0.45f * easeIn(seg(p, 0.6f, 1)));
    const float q = seg(p, 0.58f, 0.97f);                   // how much of the skin has gone
    frameStrips(0, H, [&](Strip& s) {
      for (int y = s.y0; y < s.y1; y++) {
        if (p < shut) {
          const float dy = y - CY, span = r * r - dy * dy;
          const int half = span > 0 ? (int)sqrtf(span) : 0;
          const int a = max(0, CX - half), b = min(W, CX + half);
          fillSeg(s.out, y, 0, a, skin);
          if (b > a) copySeg(s.out, y, a, A, y, a, b - a);
          fillSeg(s.out, y, b, W - b, skin);
          continue;
        }
        const float yu = (y - CY) / (unit * sc), k = 1.0f / (unit * sc);
        const uint32_t rowSeed = (uint32_t)(y / 8) * 977u;
        bool was = false;
        int start = 0;
        for (int x = 0; x <= W; x += 2) {
          const bool in = x < W && sc > 0.05f && inFace((x - CX) * k, yu);
          if (x < W && in == was) continue;
          const int end = min(x, W);
          if (was) copySeg(s.out, y, start, B, y, start, end - start);
          else                                               // skin: dark, then gone a block at a time
            for (int bx = start; bx < end;) {
              const int nx = min(end, (bx / 8 + 1) * 8);
              if (q > 0 && hashf((uint32_t)(bx / 8) + rowSeed) < q) copySeg(s.out, y, bx, B, y, bx, nx - bx);
              else fillSeg(s.out, y, bx, nx - bx, skin);
              bx = nx;
            }
          if (q < 0.6f && x < W && x > 0) fillSeg(s.out, y, x - 1, 2, rim);   // the carved edge glows
          was = in; start = x;
        }
      }
    });
    if (p >= 1 || testStop(t0)) break;
  }
  if (!stopped()) pushFull(to);
}

// ---- wake and sleep (whole frames into a sprite, fx.cpp's render) ------------------------------
void lightning(lgfx::LovyanGFX& d, int x, uint32_t seed, uint16_t col) {
  int px = x, py = 0;
  for (int i = 0; i < 7 && py < H; i++) {
    const int nx = px + (int)((hashf(seed + i * 13) - 0.5f) * W * 0.16f), ny = py + H / 7 + (int)(hashf(seed + i * 5 + 1) * 8);
    d.drawLine(px, py, nx, ny, col); d.drawLine(px + 1, py, nx + 1, ny, col);
    if (i == 3) d.drawLine(nx, ny, nx + W / 10, ny + H / 6, col);    // a fork
    px = nx; py = ny;
  }
}

void on(Canvas& f, lgfx::LovyanGFX& d, float p) {
  const Theme& c = T();
  if (p < 0.10f) d.fillScreen(TFT_BLACK);
  else if (p < 0.19f) { d.fillScreen(mix565(c.bg, c.blue, 110)); lightning(d, W * 2 / 3, 11, c.white); }
  else if (p < 0.33f) f.pushSprite(&d, 0, 0);
  else if (p < 0.43f) d.fillScreen(TFT_BLACK);
  else if (p < 0.50f) { d.fillScreen(mix565(c.bg, c.blue, 70)); lightning(d, W / 4, 47, c.white); }
  else f.pushSprite(&d, 0, 0);
}

void grin(lgfx::LovyanGFX& d, float size, uint16_t col) {    // a small carved face, for the last of the power-down
  const float u = H * 0.2f * size;
  if (u < 2) return;
  for (int dir = -1; dir <= 1; dir += 2) {
    const int ex = CX + (int)(dir * 0.55f * u);
    d.fillTriangle(ex - (int)(0.3f * u), CY - (int)(0.16f * u), ex + (int)(0.3f * u), CY - (int)(0.16f * u), ex, CY - (int)(0.68f * u), col);
  }
  for (int x = -(int)u; x <= (int)u; x++) {
    const float xu = x / u;
    const int top = CY + (int)((0.36f - 0.30f * xu * xu + 0.10f * tri01(xu * 2.5f + 0.25f)) * u);
    const int bot = CY + (int)((0.80f - 0.55f * xu * xu - 0.10f * tri01(xu * 2.5f + 0.75f)) * u);
    if (bot > top) d.drawFastVLine(CX + x, top, bot - top, col);
  }
}

void off(Canvas& f, lgfx::LovyanGFX& d, float p, bool final) {
  const Theme& c = T();
  d.fillScreen(TFT_BLACK);
  constexpr int CW = 16;                                   // wide columns: each is a clipped copy out of PSRAM, and 30 of them keep the frame rate up
  const float melt = final ? clamp01(p / 0.55f) : p;
  for (int x = 0, i = 0; x < W; x += CW, i++) {
    const float delay = hashf(i / 3 + 5) * 0.3f + hashf(i * 7 + 1) * 0.15f;
    const int drop = (int)(H * 1.1f * easeIn(clamp01((melt * 1.45f - delay))));
    if (drop >= H) continue;
    d.setClipRect(x, drop, CW, H - drop);
    f.pushSprite(&d, 0, drop);
    d.clearClipRect();
    if (drop > 0) d.fillRect(x, drop, CW, 2, (i & 1) ? c.green : c.greenDim);   // the edge it melts from
  }
  if (final && p > 0.5f) {                                  // powering down: the grin, guttering out
    const float t = seg(p, 0.5f, 1);
    const bool flick = t > 0.6f && ((int)(t * 40) % 3 == 0);
    if (!flick && t < 0.97f) grin(d, 1 - 0.35f * t, mix565(TFT_BLACK, c.green, (uint8_t)(255 * (1 - t * t))));
  }
}

}  // namespace

bool halloweenTransition(Trans kind, Canvas& from, Canvas& to) {
  switch (kind) {
  case Trans::Forward: slime(from, to, 520); return true;
  case Trans::Back:    bats(from, to, 540); return true;
  case Trans::Unlock:  doors(from, to, 900); return true;
  case Trans::Lock:    lantern(from, to, 1100); return true;
  default:             return false;
  }
}

void halloweenRender(uint8_t kind, Canvas& f, lgfx::LovyanGFX& d, float p) {
  if (kind == 0) on(f, d, p);
  else off(f, d, p, kind == 2);
}

}  // namespace fx
