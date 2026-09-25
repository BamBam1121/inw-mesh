// Aurora: the northern lights over a pine ridge.
//   forward  Curtain of light: a ribbon of aurora ignites along the top and the new
//            screen pours down out of it like a hanging sheet of light - rippling,
//            pleated, fringed pink above and violet below like the real thing - and
//            lands with a bounce.
//   back     Corona: the screen you're leaving tips back into the sky, streams up
//            into converging rays, and flares out at the zenith as a corona.
//   unlock   Substorm liftoff: the lights erupt and the clock glitches, the ground
//            falls away as we rise into the aurora, and we fly out through its rays
//            onto home.
//   lock     Touchdown: the rays close in, and we sink back down through them onto
//            the ridge, the sasquatch still walking.
// Wake and sleep stay the curtains (fx.cpp).

#include "fx_internal.h"
#include "scenes.h"

namespace fx {
using namespace k;
namespace {

constexpr uint16_t CH_R = 0x00F8, CH_G = 0xE007, CH_B = 0x1F00;   // a sprite pixel's channels (swapped)
constexpr int SB = L::STATUS_H, CLK = 172;                          // lock screen: status bar | scene | clock
constexpr float PHASE_RATE = 0.32f / 0.033f;                        // the lock scene's clocks, per second (home.cpp)
constexpr float SCROLL_RATE = 2.0f / 0.033f;

// A sprite pixel at 3/4 brightness.
inline uint16_t darken(uint16_t s) { uint16_t n = sw(s); n -= (n >> 2) & 0x39E7; return sw(n); }
inline uint16_t blendSw(uint16_t s, uint16_t c, uint8_t a) { return sw(mix565(sw(s), c, a)); }

// ---- the rays ------------------------------------------------------------------------
// The lock scene's two ribbons (scenes::aurora), worked up into a storm. ph is the
// scene's ribbon phase (its phase * 0.05). All zero is the scene exactly. gain 1 is
// erupting: longer, wilder rays with oxygen-red tops. zoom spreads the rays out from
// the centre, fill is how much of its slot each ray fills, reach stretches them from
// their ribbon to the full height. Worked out once a frame, drawn strip by strip.
struct Storm { float gain, zoom, fill, reach; };
struct Ray { int16_t x, y, len, wd; uint16_t col[4]; bool hem; };
struct Rays { Ray r[320]; };
Rays* s_rays = nullptr;
int s_nRays = 0;

void buildRays(const Theme& t, float ph, const Storm& s, uint32_t flick) {
  s_nRays = 0;
  if (!psram(s_rays)) return;
  const bool apart = s.zoom > 1.01f;                     // spread out, the bands take turns
  const int wd = s.fill <= 0 ? 1 : max(1, (int)(3 * s.zoom * s.fill + 0.5f));
  for (int band = 0; band < 2; band++) {
    const uint16_t c = band ? t.greenDim : t.green;
    const int base = band ? 42 : 64;
    for (int i = 0; i < 160; i++) {
      if (apart && (i & 1) != band) continue;
      const int x0 = i * 3;
      const int x = (int)lroundf(CX + (x0 - CX) * s.zoom);
      if (x + wd <= 0 || x >= W) continue;
      const float fx = x0 * 0.011f;
      float y = base + (int)((sinf(fx * (band ? 1.6f : 1.0f) + ph * (band ? -1.3f : 1.0f)) * 16 + sinf(fx * 3.1f - ph * 0.7f) * 6) * (1 + 1.2f * s.gain));
      float len = (26 + (int)((sinf(fx * 2.3f + ph * 1.9f) + 1) * 16)) * (1 + 1.5f * s.gain);
      if (s.reach > 0) { y = lerpf(y, 0, s.reach); len = lerpf(len, H, s.reach); }
      const float glow = 0.55f + 0.45f * sinf(fx * 1.7f + ph * 2.4f);
      const bool dim = s.gain > 0.2f && hash32(i * 31 + flick * 7) % 5 == 0;   // rays flicker
      Ray& r = s_rays->r[s_nRays++];
      r.x = (int16_t)x; r.y = (int16_t)y; r.len = (int16_t)len; r.wd = (int16_t)wd;
      r.hem = s.gain > 0.5f && s.reach < 0.5f;
      for (int k2 = 0; k2 < 4; k2++) {
        float f = (1.0f - (k2 + 1) / 5.0f * glow) * (1.0f - k2 * 0.22f);
        if (dim) f *= 0.55f;
        uint16_t col = scenes::mix(t.bg, c, f);
        if (k2 == 0 && s.gain > 0.3f) col = mix565(col, t.red, (uint8_t)(170 * s.gain));
        r.col[k2] = col;
      }
    }
  }
}

void drawRays(const Strip& s, const Theme& t) {
  for (int i = 0; i < s_nRays; i++) {
    const Ray& r = s_rays->r[i];
    if (!touches(s, r.y, r.y + r.len + 2)) continue;
    for (int k2 = 0; k2 < 4; k2++) {
      const int yy = r.y + k2 * r.len / 4, hh = r.len / 4 + 1;
      if (!touches(s, yy, yy + hh)) continue;
      if (r.wd == 1) s.g.drawFastVLine(r.x, yy, hh, r.col[k2]);
      else s.g.fillRect(r.x, yy, r.wd, hh, r.col[k2]);
    }
    if (r.hem) s.g.drawFastHLine(r.x, r.y + r.len, r.wd, t.red);   // a pink hem
  }
}

// The scene's stars (scenes::stars), dy px lower, streaked into lines as we rush past.
void starField(const Strip& s, const Theme& t, float phase, int dy, int streak) {
  for (int i = 0; i < 70; i++) {
    const uint32_t h = scenes::hash(i + 7);
    const int x = h % 480, y = 20 + (h >> 9) % (150 - 20) + dy;
    if (!touches(s, y - streak, y + 1) || ((int)(phase * 0.2f) + i) % 7 == 0) continue;
    const uint16_t col = (i % 5 == 0) ? scenes::mix(t.dim, 0xFFFF, 0.6f) : t.dim;
    if (streak <= 0) s.g.drawPixel(x, y, col);
    else s.g.drawFastVLine(x, y - streak, streak + 1, col);
  }
}
void moon(const Strip& s, const Theme& t, int dy) {
  if (!touches(s, 29 + dy, 52 + dy)) return;
  s.g.fillCircle(88, 40 + dy, 11, scenes::rgb(0xf2f5ff));
  s.g.fillCircle(93, 36 + dy, 10, t.bg);
}
// The ridge and the sasquatch (whose limbs are drawWideLine: see Strip).
void ground(const Strip& s, const Theme& t, float phase, float scroll, bool unread, int dy) {
  if (touches(s, 104 + dy - 2, scenes::GROUND + dy + 2)) scenes::auroraGround(s.band, t, phase, scroll, unread, dy - s.bandY);
}

// The strip's rows of src with red from `cr` rows below and blue from `cr` rows above:
// the colours of the aurora's layers, pulled apart.
void emission(const Strip& s, const uint16_t* src, int cr) {
  for (int y = s.y0; y < s.y1; y++) {
    if (!cr) { copyRow(s.out, y, src, y); continue; }
    uint16_t* o = s.out + y * W;
    const uint16_t* g = src + y * W;
    const uint16_t* r = src + min(H - 1, y + cr) * W;
    const uint16_t* b = src + max(0, y - cr) * W;
    for (int x = 0; x < W; x++) o[x] = (r[x] & CH_R) | (g[x] & CH_G) | (b[x] & CH_B);
  }
}

// ---- forward: curtain of light -------------------------------------------------------
// Timings are in the design's own 420 ms and scale with ms.
void curtain(Canvas& from, Canvas& to, uint16_t ms) {
  const Theme& c = T();
  const uint16_t* A = bufOf(from);
  const uint16_t* B = bufOf(to);
  constexpr int NS = W / 4;                              // 4 px columns of the curtain
  struct Cols { int16_t hem[NS], sxo[NS]; bool shade[NS]; uint32_t step[NS]; };
  static Cols* cols = nullptr;
  if (!psram(cols)) { pushFull(to); return; }
  int16_t* hem = cols->hem; int16_t* sxo = cols->sxo; bool* shade = cols->shade; uint32_t* step = cols->step;
  const uint16_t rayTop = mix565(c.red, c.green, 110), rayLow = mix565(c.green, TFT_WHITE, 120);
  int lastRows = 0;
  uint32_t frame = 0;
  for (uint32_t t0 = tnow();; frame++) {
    const float p = clamp01((tnow() - t0) / (float)ms);
    const float tm = p * 420, u = tm / 240;
    // Where the hem is: pouring down (60-300), landing with a bounce (300-360), down.
    float base = 0, amp = 0;
    if (tm >= 360) base = H;
    else if (tm >= 300) { const float v = (tm - 300) / 60; base = H + 8 * sinf(v * 3 * PI) * (1 - v); amp = 0.35f * (1 - v); }
    else if (tm >= 60) { const float v = (tm - 60) / 240; base = lerpf(24, H, easeOutCubic(v)); amp = 0.35f + 0.65f * sinf(PI * v); }
    int maxHem = 0;
    for (int s = 0; s < NS; s++) {
      const float x = s * 4 + 2;
      const int h = tm < 60 ? 0 : (int)(base + amp * (10 * sinf(0.028f * x + 11 * u) + 4 * sinf(0.09f * x - 23 * u)));
      hem[s] = (int16_t)constrain(h, 0, H + 16);
      // Pleats: each column's picture slides sideways a little, and where they bunch
      // up the fold is in shadow.
      const float ph = 0.07f * x + 8 * u;
      sxo[s] = (int16_t)((int)(5 * amp * sinf(ph)) & ~1);
      shade[s] = amp * cosf(ph) > 0.45f;
      step[s] = hem[s] > 0 ? ((uint32_t)H << 16) / (uint32_t)hem[s] : 0;
      if (hem[s] > maxHem) maxHem = hem[s];
    }
    // Red rides above, violet below, then they snap together as it locks on.
    const int cr = tm < 360 ? 3 : (int)(3.99f * (1 - seg(tm, 360, 405)));
    const int rows = min(H, max(maxHem + 8, 30));
    const int comp = max(rows, lastRows);                 // rows it left last frame go back to `from`
    const float lit = easeOut(seg(tm, 0, 60)), rk = 1 - seg(tm, 365, 410);
    const int la = (int)(320 * (1 - lit)), lb = (int)(320 + 160 * lit);
    frameStrips(0, comp, [&](Strip& st) {
      for (int y = st.y0; y < st.y1; y++) {
        uint16_t* o = st.out + y * W;
        const uint16_t* fa = A + y * W;
        for (int s = 0; s < NS; s++) {
          const int x0 = s * 4;
          uint16_t* d = o + x0;
          if (y >= hem[s]) { memcpy(d, fa + x0, 8); continue; }
          const uint32_t stp = step[s];
          const int sx = constrain(x0 + sxo[s], 0, W - 4);
          const uint16_t* g = B + min((int)((y * stp) >> 16), H - 1) * W + sx;
          if (cr) {
            const uint16_t* r = B + min((int)(((y + cr) * stp) >> 16), H - 1) * W + sx;
            const uint16_t* b = B + (int)((max(0, y - cr) * stp) >> 16) * W + sx;
            for (int i = 0; i < 4; i++) d[i] = (r[i] & CH_R) | (g[i] & CH_G) | (b[i] & CH_B);
          } else memcpy(d, g, 8);
          if (shade[s]) for (int i = 0; i < 4; i++) d[i] = darken(d[i]);
        }
      }
      // The hem: violet light spilling onto the old screen, a pink edge, white glints
      // where it hangs lowest.
      if (tm >= 60 && tm < 360)
        for (int s = 0; s < NS; s++) {
          const int hy = hem[s], x0 = s * 4;
          if (hy >= H || hy + 7 < st.y0 || hy >= st.y1) continue;
          for (int k2 = 0; k2 < 5; k2++) {
            const int y = hy + 2 + k2;
            if (!rowOk(y)) continue;
            uint16_t* o = st.out + y * W + x0;
            for (int i = 0; i < 4; i++) o[i] = blendSw(o[i], c.greenDim, (uint8_t)(170 - 34 * k2));
          }
          fillSeg(st.out, hy, x0, 4, sw(c.red));
          fillSeg(st.out, hy + 1, x0, 4, sw(c.red));
          const int l = s ? hem[s - 1] : hy, r = s < NS - 1 ? hem[s + 1] : hy;
          if (hy >= l && hy >= r) fillSeg(st.out, hy, x0, 4, 0xFFFF);
        }
      // The ribbon it pours from: lit outward from x = 320, its rays drawn back up
      // into the status bar at the end.
      if (rk > 0 && lb > la && st.y0 < 32) {
        for (int x = la - la % 3; x < lb; x += 3) {
          if (x < la) continue;
          const float fx = x * 0.011f;
          const int len = (int)((13 + 7 * sinf(fx * 2.3f + u * 5) + 5 * sinf(fx * 6.1f - u * 8)) * rk);
          if (len < 3) continue;
          const uint8_t lv = hash32(x * 13 + frame * 7) % 6 == 0 ? 140 : 255;
          st.g.fillRect(x, 0, 2, len / 3 + 1, scale565(rayTop, lv));
          st.g.fillRect(x, len / 3, 2, len / 3 + 1, scale565(c.green, lv));
          st.g.fillRect(x, 2 * len / 3, 2, len - 2 * len / 3, scale565(rayLow, lv));
          st.g.drawFastVLine(x, len, 3, c.greenDim);
        }
        if (tm < 60) { st.g.fillRect(max(0, la - 1), 0, 3, 16, TFT_WHITE); st.g.fillRect(min(W - 3, lb - 2), 0, 3, 16, TFT_WHITE); }
      }
    });
    lastRows = rows;
    if (p >= 1 || testStop(t0)) break;
  }
  if (!stopped()) pushFull(to);
}

// ---- back: corona --------------------------------------------------------------------
void corona(Canvas& from, Canvas& to, uint16_t ms) {
  const Theme& c = T();
  const uint16_t* A = bufOf(from);
  const uint16_t* B = bufOf(to);
  const int VY = 18;                                      // the vanishing point, top centre
  int lastA = 0, lastB = H;
  for (uint32_t t0 = tnow();;) {
    const float p = clamp01((tnow() - t0) / (float)ms);
    const float tm = p * 400;
    if (tm < 300) {
      // The screen lies back into the sky in true perspective: rows bunch up toward
      // the far edge, widths shrink toward the vanishing point.
      const float u = tm / 300;
      const float ytop = VY * easeOut(u), yb = lerpf(H, VY + 2, powf(u, 1.6f)), kk = 6 * easeIn(u);
      const int y0 = (int)ytop, y1 = min(H, (int)ceilf(yb));
      frameStrips(min(y0, lastA), max(y1, lastB), [&](Strip& s) {
        for (int y = s.y0; y < s.y1; y++) {
          copyRow(s.out, y, B, y);
          if (y < y0 || y >= y1) continue;
          const float d = (yb - y) / (yb - ytop);
          const float v = d / (1 + kk - kk * d);         // 0 near edge .. 1 far edge
          const float sc = 1 / (1 + kk * v);
          const int srow = constrain((int)((H - 1) * (1 - v)), 0, H - 1);
          const int x0 = (int)(CX - sc * W / 2), x1 = (int)(CX + sc * W / 2);
          uint16_t* o = s.out + y * W;
          if (sc >= 0.45f) {
            const int span = x1 - x0;
            if (span <= 0) continue;
            const uint16_t* src = A + srow * W;
            const uint16_t* srcR = A + max(0, srow - 2) * W;
            const uint32_t stp = ((uint32_t)W << 16) / span;
            uint32_t acc = 0;
            if (v > 0.7f) for (int x = x0; x < x1; x++, acc += stp) { const int sx = acc >> 16; o[x] = (uint16_t)((src[sx] & ~CH_R) | (srcR[sx] & CH_R)); }
            else for (int x = x0; x < x1; x++, acc += stp) o[x] = src[acc >> 16];
          } else {
            // Too far off to be a picture: rays converging on the point, teal near,
            // violet further, pink at the tip.
            const int rw = max(1, (int)lroundf(3 * sc));
            const uint16_t col = sc > 0.25f ? mix565(c.greenDim, c.green, (uint8_t)((sc - 0.25f) / 0.2f * 255))
                                            : mix565(c.red, c.greenDim, (uint8_t)(sc / 0.25f * 255));
            for (int j = 0; j < 20; j++) fillSeg(s.out, y, (int)(CX + (12 + 24 * j - CX) * sc), rw, sw(col));
          }
        }
      });
      lastA = y0; lastB = y1;
    } else {
      // The last sliver pinches to a point, which flares into a corona.
      const int bh = 112;
      const float f = seg(tm, 330, 400);
      const float ext = f < 0.5f ? easeOut(f * 2) : 1 - easeIn((f - 0.5f) * 2);
      frameStrips(0, max(bh, lastB), [&](Strip& s) {
        for (int y = s.y0; y < s.y1; y++) copyRow(s.out, y, B, y);
        if (tm < 330) {
          const float cf = seg(tm, 300, 330);
          const int hw = (int)(60 * (1 - easeIn(cf)));
          s.g.drawFastHLine(CX - hw, VY, 2 * hw + 1, c.green);
          s.g.drawFastHLine(CX - hw / 2, VY, hw + 1, TFT_WHITE);
          s.g.fillCircle(CX, VY, 1 + (int)(2 * cf), TFT_WHITE);
          return;
        }
        for (int i = 0; i < 14; i++) {
          const float ang = i * (2 * PI / 14) + 0.2f * sinf(i * 1.7f);
          const float len = (30 + 60 * hashf(i * 5 + 1)) * ext, ca = cosf(ang), sa = sinf(ang);
          const int x1 = CX + (int)(ca * len * 0.45f), y1 = VY + (int)(sa * len * 0.45f);
          const int x2 = CX + (int)(ca * len * 0.8f), y2 = VY + (int)(sa * len * 0.8f);
          const int x3 = CX + (int)(ca * len), y3 = VY + (int)(sa * len);
          s.g.drawLine(CX, VY, x1, y1, c.green);    s.g.drawLine(CX + 1, VY, x1 + 1, y1, c.green);
          s.g.drawLine(x1, y1, x2, y2, c.greenDim); s.g.drawLine(x1 + 1, y1, x2 + 1, y2, c.greenDim);
          s.g.drawLine(x2, y2, x3, y3, c.red);
        }
        const int core = (int)(3 * (1 - seg(f, 0.4f, 1)));
        if (core > 0) s.g.fillCircle(CX, VY, core, TFT_WHITE);
        star(s.g, CX, VY, max(1, (int)(4 - 3 * f)), TFT_WHITE);
      });
      lastB = 0;
    }
    if (p >= 1 || testStop(t0)) break;
  }
  if (!stopped()) pushFull(to);
}

// ---- unlock: substorm liftoff ---------------------------------------------------------
// The lock scene is redrawn live, carrying on from exactly what was on screen.
void liftoff(Canvas& from, Canvas& to, uint16_t ms) {
  const Theme& c = T();
  const uint16_t* A = bufOf(from);
  const uint16_t* B = bufOf(to);
  const scenes::AuroraState st = scenes::auroraLast();
  float walk = st.phase, rib = st.phase, scroll = st.scroll;
  bool pierced = false;
  uint32_t last = tnow(), frame = 0;
  for (uint32_t t0 = tnow();; frame++) {
    const uint32_t now = tnow();
    const float dt = (now - last) / 1000.0f;
    last = now;
    const float p = clamp01((now - t0) / (float)ms), tm = p * 720;
    if (tm < 420) {
      // ONSET (0-160): the two quiet ribbons erupt and the clock glitches.
      // LIFTOFF (160-420): we rise - the clock band falls away fastest, the ridge
      // slower, the sky barely - into the lights, which spread to fill the screen.
      const float gain = easeOut(seg(tm, 0, 160)), lu = easeIn(seg(tm, 160, 420));
      const int dSky = (int)(16 * lu), dGround = (int)(125 * lu), dClock = (int)(150 * lu);
      rib += dt * PHASE_RATE * (1 + 3 * gain);
      walk += dt * PHASE_RATE * 1.5f;
      scroll += dt * SCROLL_RATE * 1.5f;
      buildRays(c, rib * 0.05f, {gain, 1 + 2 * lu, 0.34f + 0.66f * gain, lu}, frame);
      const int top = (int)(SB * (1 - lu));
      // The storm's current glitches the clock: slices of it jump sideways, split.
      int tearY[2], tearH[2], tearOff[2];
      for (int i = 0; i < 2; i++) {
        const uint32_t h = hash32(frame * 17 + i * 5 + 3);
        tearY[i] = tm > 40 ? CLK + dClock + (int)(h % 42) : H;
        tearH[i] = 4 + (int)((h >> 8) % 9);
        tearOff[i] = (int)(4 + (h >> 16) % 11) * ((h >> 24) & 1 ? 1 : -1);
      }
      frameStrips(0, H, [&](Strip& s) {
        for (int y = s.y0; y < s.y1; y++) {
          if (y < SB) copyRow(s.out, y, A, y);           // the status bar stays put
          else fillSeg(s.out, y, 0, W, sw(c.bg));
        }
        starField(s, c, st.phase, dSky, (int)(28 * lu));
        if (lu > 0.25f) moon(s, c, dSky);                 // once we're rising, the lights are in front
        if (top < s.y1) {                                 // over the status bar only once we're rising
          s.g.setClipRect(0, max(top, s.y0), W, s.y1 - max(top, s.y0));
          drawRays(s, c);
          s.g.setClipRect(0, s.y0, W, s.y1 - s.y0);
        }
        if (lu <= 0.25f) moon(s, c, dSky);
        ground(s, c, walk, scroll, st.unread, dGround);
        for (int y = s.y0; y < s.y1; y++)                 // the clock band, in front of it all
          if (y >= CLK + dClock) copyRow(s.out, y, A, y - dClock);
        for (int i = 0; i < 2; i++)
          for (int y = max(tearY[i], s.y0); y < min(min(H, tearY[i] + tearH[i]), s.y1); y++) {
            copyRowWrap(s.out, y, A, y - dClock, tearOff[i]);
            chromaRow(s.out, y, 3, 3);
          }
      });
    } else if (tm < 660) {
      // PIERCE, then THROUGH: we fly through the curtain; home shows between the
      // rays as they thin out and race past the edges.
      if (!pierced) { pierced = true; buzzOnce(); }
      const float u = seg(tm, 420, 660);
      rib += dt * PHASE_RATE * 4;
      buildRays(c, rib * 0.05f, {1, lerpf(3, 14, easeIn(u)), lerpf(1, 0.15f, easeOut(u)), 1}, frame);
      frameStrips(0, H, [&](Strip& s) {
        for (int y = s.y0; y < s.y1; y++) copyRow(s.out, y, B, y);
        drawRays(s, c);
      });
    } else {
      // SIGNAL LOCK: the last of the light's colours pulled apart, snapping together.
      const int cr = tm < 690 ? 2 : tm < 710 ? 1 : 0;
      frameStrips(0, H, [&](Strip& s) { emission(s, B, cr); });
    }
    if (p >= 1 || testStop(t0)) break;
  }
  if (!stopped()) pushFull(to);
}

// ---- lock: touchdown --------------------------------------------------------------------
void touchdown(Canvas& from, Canvas& to, uint16_t ms) {
  const Theme& c = T();
  const uint16_t* A = bufOf(from);
  const uint16_t* B = bufOf(to);
  // `to` was just drawn: the scene's clocks run so they arrive at exactly that.
  const scenes::AuroraState st = scenes::auroraLast();
  Mote motes[8] = {};
  bool landed = false;
  uint32_t last = tnow(), frame = 0;
  for (uint32_t t0 = tnow();; frame++) {
    const uint32_t now = tnow();
    const float dt = (now - last) / 1000.0f;
    last = now;
    const float p = clamp01((now - t0) / (float)ms), tm = p * 460;
    const float before = (tm - 460) / 1000.0f;           // seconds until the end (negative)
    if (tm < 160) {
      // The storm rolls in: rays sweep in from past both edges and close on the
      // centre, and the screen under them tears.
      const float u = seg(tm, 0, 140);
      buildRays(c, (st.phase + before * PHASE_RATE * 4) * 0.05f, {1, lerpf(14, 3, easeOut(u)), lerpf(0.15f, 1, easeIn(u)), 1}, frame);
      int tearY[3], tearH[3], tearOff[3];
      for (int i = 0; i < 3; i++) {
        const uint32_t h = hash32(frame * 29 + i * 3 + 1);
        tearY[i] = (int)(h % H); tearH[i] = 6 + (int)((h >> 8) % 13);
        tearOff[i] = (int)(2 + (h >> 16) % 7) * ((h >> 24) & 1 ? 1 : -1);
      }
      frameStrips(0, H, [&](Strip& s) {
        for (int y = s.y0; y < s.y1; y++) copyRow(s.out, y, A, y);
        for (int i = 0; i < 3; i++)
          for (int y = max(tearY[i], s.y0); y < min(min(H, tearY[i] + tearH[i]), s.y1); y++) {
            copyRowWrap(s.out, y, A, y, tearOff[i]);
            chromaRow(s.out, y, 3, 3);
          }
        drawRays(s, c);
      });
    } else {
      // Descent and touchdown: the rays pull up and together into the scene's two
      // ribbons, the stars stop streaking, and the ridge rises from below and lands
      // with a bounce, the clock a beat behind.
      const float u = seg(tm, 160, 400), g = easeOut(u);
      const float gain = tm < 400 ? lerpf(1, 0.4f, g) : 0.4f * (1 - seg(tm, 400, 450));
      const float reach = 1 - g;
      const int dGround = (int)(150 * (1 - easeOutBack(u, 1.0f)));
      const int dClock = (int)(150 * (1 - easeOutBack(seg(tm, 200, 420), 1.0f)));
      const int dSky = (int)(16 * (1 - g));
      buildRays(c, (st.phase + before * PHASE_RATE * (1 + 3 * gain)) * 0.05f, {gain, lerpf(3, 1, g), lerpf(1, 0.34f, g), reach}, frame);
      const int top = (int)(SB * (1 - reach));
      if (!landed && tm >= 340) {                        // light kicked up off the ridge
        landed = true;
        for (int i = 0; i < 6; i++)
          spawnMote(motes, 60 + hashf(i * 7) * 360, 150 + hashf(i * 3) * 16, (hashf(i * 5) - 0.5f) * 30, -40 - hashf(i) * 30,
                    0.5f, i % 2 ? c.green : c.greenDim, 3 + (uint8_t)(hashf(i * 9) * 2));
      }
      stepMotes(motes, dt, -20);
      frameStrips(0, H, [&](Strip& s) {
        for (int y = s.y0; y < s.y1; y++) {
          if (y < SB) copyRow(s.out, y, B, y);
          else fillSeg(s.out, y, 0, W, sw(c.bg));
        }
        starField(s, c, st.phase, dSky, (int)(20 * (1 - g)));
        if (g < 0.75f) moon(s, c, dSky);
        if (top < s.y1) {
          s.g.setClipRect(0, max(top, s.y0), W, s.y1 - max(top, s.y0));
          drawRays(s, c);
          s.g.setClipRect(0, s.y0, W, s.y1 - s.y0);
        }
        if (g >= 0.75f) moon(s, c, dSky);
        ground(s, c, st.phase + before * PHASE_RATE, st.scroll + before * SCROLL_RATE, st.unread, dGround);
        for (int y = s.y0; y < s.y1; y++)
          if (y >= CLK + dClock && y - dClock < H) copyRow(s.out, y, B, y - dClock);
        drawMotes(s.g, motes, false);
      });
    }
    if (p >= 1 || testStop(t0)) break;
  }
  if (!stopped()) pushFull(to);
}

}  // namespace

bool auroraTransition(Trans kind, Canvas& from, Canvas& to) {
  switch (kind) {
  case Trans::Forward: curtain(from, to, 420); return true;
  case Trans::Back:    corona(from, to, 400); return true;
  case Trans::Unlock:  liftoff(from, to, 720); return true;
  case Trans::Lock:    touchdown(from, to, 460); return true;
  default:             return false;
  }
}

}  // namespace fx
