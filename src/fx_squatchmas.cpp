// Squatchmas: its own screen changes (the theme has the Squatch look everywhere else).
//   forward  Wrapping paper sweeps over the old screen, a ribbon goes round it, and it
//            tears open down the middle: the new screen is inside.
//   back     The sleigh flies across right to left behind its reindeer, and the screen
//            before is left behind it in a trail of sparkle.
//   unlock   A gold ribbon and bow tie up the lock screen, come undone, and the box's
//            four flaps fold open onto home.
//   lock     The screen closes into a snow globe, it's shaken, the snow settles over the
//            lock screen, and the globe opens back out.
//   wake     A string of lights comes on bulb by bulb, flickers, and pulls away off the
//            picture.
//   sleep    The picture spins down into a star that twinkles out. Powering down, a
//            Christmas tree's lights go out one by one, the star on top last.
// Everything is measured from the screen's own width and height (k::W, k::H), so the
// pager's wide screen and the T-Deck's taller one each get it at their own size.

#include "fx_internal.h"

namespace fx {
using namespace k;
namespace {

constexpr float U = H / 111.0f;                            // a unit: 2 px on both screens
inline int u(float v) { return (int)(v * U + 0.5f); }

uint16_t bulb(int i) {
  static const uint16_t C[4] = {rgb(255, 74, 74), rgb(92, 255, 122), rgb(255, 211, 77), rgb(92, 200, 255)};
  return C[i & 3];
}

// Wrapping paper: red stripes on the slant with a gold pinstripe (swapped pixels).
void paperRow(uint16_t* out, int y, int x0, int x1) {
  if (!rowOk(y)) return;
  static const uint16_t a = sw(rgb(200, 30, 40)), b = sw(rgb(150, 18, 30)), gold = sw(rgb(255, 205, 80));
  uint16_t* p = out + y * W;
  for (int x = max(0, x0), e = min(W, x1); x < e; x++) {
    const int d = (x + y) & 31;
    p[x] = d < 2 ? gold : d < 16 ? a : b;
  }
}

// A bow at (x, y), s units across, in plain colours on a strip's canvas.
void bow(lgfx::LovyanGFX& g, int x, int y, float s, uint16_t c, uint16_t dark) {
  if (s <= 0.05f) return;
  const int l = u(9 * s), h = u(6 * s);
  g.fillTriangle(x, y, x - l, y - h, x - l, y + h, c);
  g.fillTriangle(x, y, x + l, y - h, x + l, y + h, c);
  g.fillTriangle(x, y, x - l / 2, y + h + u(5 * s), x - l / 4, y + h + u(6 * s), dark);   // the tails
  g.fillTriangle(x, y, x + l / 2, y + h + u(5 * s), x + l / 4, y + h + u(6 * s), dark);
  g.fillCircle(x, y, max(1, u(2.4f * s)), dark);
}

// ---- forward: wrapping paper ----------------------------------------------------------------
void wrapTear(Canvas& from, Canvas& to, uint16_t ms) {
  const uint16_t* A = bufOf(from);
  const uint16_t* B = bufOf(to);
  const uint16_t back = sw(rgb(250, 244, 232)), fold = sw(rgb(255, 130, 130));
  const uint16_t gold = rgb(255, 205, 80), goldDk = rgb(190, 140, 40);
  for (uint32_t t0 = tnow();;) {
    const float p = clamp01((tnow() - t0) / (float)ms);
    const int edge = W - (int)((W + 6) * easeOut(seg(p, 0, 0.3f)));         // the paper sweeping over
    const float tie = p < 0.42f ? easeOutBack(seg(p, 0.27f, 0.36f)) : 0;      // a ribbon round it
    const int rib = (int)(u(5) * tie);
    const float open = easeInOut(seg(p, 0.42f, 1));                           // the tear
    const int half = (int)((CX + 26) * open);
    frameStrips(0, H, [&](Strip& s) {
      for (int y = s.y0; y < s.y1; y++) {
        if (p < 0.42f) {
          if (edge > 0) copySeg(s.out, y, 0, A, y, 0, edge);
          paperRow(s.out, y, max(0, edge), W);
          if (edge > 0 && edge < W) fillSeg(s.out, y, edge, 3, fold);
          if (rib > 0) {
            if (abs(y - CY) <= rib) fillSeg(s.out, y, 0, W, sw(abs(y - CY) == rib ? goldDk : gold));
            fillSeg(s.out, y, CX - rib, 2 * rib + 1, sw(gold));
            fillSeg(s.out, y, CX - rib, 1, sw(goldDk));
          }
          continue;
        }
        const int jag = (int)(sinf(y * 0.35f) * 3 + sinf(y * 0.11f + 1) * 4) + (int)(hash32(y / 4 * 7 + 1) % 3),
                  jag2 = (int)(sinf(y * 0.29f + 2) * 3 + sinf(y * 0.13f) * 4) + (int)(hash32(y / 4 * 13 + 5) % 3);
        const int a = CX - half + jag, b = CX + half + jag2;
        paperRow(s.out, y, 0, a);
        paperRow(s.out, y, b, W);
        const int a0 = max(0, a), b0 = min(W, b);
        if (b0 > a0) copySeg(s.out, y, a0, B, y, a0, b0 - a0);
        const int curl = 2 + (int)(5 * (1 - open));                          // the torn edges: the paper's white back
        if (a > 0) fillSeg(s.out, y, a - curl, curl, back);
        if (b < W) fillSeg(s.out, y, b, curl, back);
      }
      if (tie > 0 && touches(s, CY - u(14), CY + u(14))) bow(s.g, CX, CY, tie, gold, goldDk);
    });
    if (p >= 1 || testStop(t0)) break;
  }
  if (!stopped()) pushFull(to);
}

// ---- back: the sleigh -------------------------------------------------------------------------
// A reindeer at (x, y) facing left, legs at gallop phase ph.
void reindeer(lgfx::LovyanGFX& g, int x, int y, float ph, bool lead, uint16_t fur, uint16_t antler) {
  g.fillRect(x - u(7), y - u(3), u(14), u(6), fur);
  g.fillCircle(x - u(7), y, u(3), fur);
  g.fillCircle(x + u(7), y, u(3), fur);
  for (int k = 0; k < 4; k++) {                                              // legs, galloping
    const int lx = x + (k < 2 ? -u(6) : u(5)) + (k & 1) * u(2);
    const int sw2 = (int)(sinf(ph + k * 1.6f) * u(3));
    g.drawLine(lx, y + u(2), lx + sw2, y + u(8), fur);
    g.drawLine(lx + 1, y + u(2), lx + sw2 + 1, y + u(8), fur);
  }
  for (int k = 0; k < 2; k++) g.drawLine(x - u(8) - k, y - u(2), x - u(12) - k, y - u(8), fur);   // neck
  g.fillCircle(x - u(13), y - u(9), u(2.6f), fur);
  g.fillRect(x - u(17), y - u(9), u(4), u(2.4f), fur);                       // muzzle
  g.drawLine(x - u(12), y - u(11), x - u(10), y - u(17), antler);            // antlers
  g.drawLine(x - u(11), y - u(14), x - u(8), y - u(16), antler);
  g.drawLine(x - u(14), y - u(11), x - u(16), y - u(16), antler);
  if (lead) { g.fillCircle(x - u(17), y - u(8), u(1.6f), rgb(255, 40, 40)); g.drawPixel(x - u(17), y - u(9), 0xFFFF); }
}

void sleigh(Canvas& from, Canvas& to, uint16_t ms) {
  const Theme& c = T();
  const uint16_t* A = bufOf(from);
  const uint16_t* B = bufOf(to);
  const uint16_t red = rgb(210, 36, 40), gold = rgb(255, 205, 80), fur = rgb(150, 98, 58), antler = rgb(210, 175, 120), sack = rgb(138, 90, 43);
  for (uint32_t t0 = tnow();;) {
    const uint32_t el = tnow() - t0;
    const float p = clamp01(el / (float)ms), t = el / 1000.0f;
    const float front = lerpf(W + u(70), -u(70), easeInOut(p));              // the sleigh's back; behind it, the screen before
    const int sy = CY - H / 10 + (int)(sinf(p * 6.2832f) * H * 0.07f);
    frameStrips(0, H, [&](Strip& s) {
      for (int y = s.y0; y < s.y1; y++) {
        const int e = constrain((int)front + (int)(5 * sinf(y * 0.11f + t * 14)), 0, W);
        if (e > 0) copySeg(s.out, y, 0, A, y, 0, e);
        if (e < W) copySeg(s.out, y, e, B, y, e, W - e);
        if (!(y & 1) && e > 0 && e < W - 2) fillSeg(s.out, y, e, 2, sw(gold));
      }
      for (int i = 0; i < 26; i++) {                                         // the trail of sparkle behind it
        const int bx = (int)(front + hashf(i * 5 + 1) * u(60)) + (int)(sinf(t * 6 + i) * 3);
        const int by = (int)(hashf(i * 3 + 11) * H);
        if (!touches(s, by - 3, by + 3) || ((int)(t * 20) + i) % 4 == 0) continue;
        star(s.g, bx, by, i % 3 ? 1 : 2, i % 2 ? gold : c.white);
      }
      if (!touches(s, sy - u(24), sy + u(14))) return;
      const int bx = (int)front - u(44), by = sy;
      // reindeer out front, on their reins
      const int r1 = bx - u(26), r2 = bx - u(52), ry1 = by - u(4) + (int)(sinf(t * 18) * u(1)), ry2 = by - u(7) + (int)(sinf(t * 18 + 1) * u(1));
      s.g.drawLine(bx - u(2), by - u(2), r1 + u(7), ry1, gold);
      s.g.drawLine(r1 - u(6), ry1, r2 + u(7), ry2, gold);
      reindeer(s.g, r1, ry1, t * 22, false, fur, antler);
      reindeer(s.g, r2, ry2, t * 22 + 1.3f, true, fur, antler);
      // the sleigh: runner, body, a high back, and the sack
      s.g.drawFastHLine(bx - u(5), by + u(9), u(48), gold);
      s.g.drawFastHLine(bx - u(5), by + u(9) + 1, u(48), gold);
      s.g.drawCircle(bx - u(5), by + u(6), u(3), gold);
      s.g.drawFastVLine(bx + u(8), by + u(5), u(4), gold);
      s.g.drawFastVLine(bx + u(32), by + u(5), u(4), gold);
      s.g.fillCircle(bx + u(16), by - u(8), u(7), sack);
      s.g.fillRect(bx, by - u(4), u(40), u(9), red);
      s.g.fillRect(bx + u(28), by - u(14), u(12), u(19), red);
      s.g.fillCircle(bx + u(34), by - u(14), u(6), red);
      s.g.fillCircle(bx, by, u(4.5f), red);
      s.g.drawFastHLine(bx, by - u(4), u(28), gold);
      s.g.drawFastVLine(bx + u(28), by - u(12), u(8), gold);
    });
    if (p >= 1 || testStop(t0)) break;
  }
  if (!stopped()) pushFull(to);
}

// ---- unlock: the present ----------------------------------------------------------------------
void present(Canvas& from, Canvas& to, uint16_t ms) {
  const uint16_t* A = bufOf(from);
  const uint16_t* B = bufOf(to);
  const uint16_t gold = rgb(255, 205, 80), goldDk = rgb(190, 140, 40), white = rgb(255, 250, 240);
  const float slope = (float)CX / CY;
  for (uint32_t t0 = tnow();;) {
    const uint32_t el = tnow() - t0;
    const float p = clamp01(el / (float)ms), t = el / 1000.0f;
    const float tie = easeOutBack(seg(p, 0, 0.2f)), undo = easeIn(seg(p, 0.22f, 0.38f));
    const int rib = (int)(u(5) * clamp01(tie) * (1 - undo));
    const int reachV = (int)((CY + 2) * clamp01(tie)), reachH = (int)((CX + 2) * clamp01(tie));
    const float q = easeInCubic(seg(p, 0.36f, 1));
    const int dT = (int)((CY + 12) * 1.2f * q), dS = (int)((CX + 12) * 1.2f * q);
    frameStrips(0, H, [&](Strip& s) {
      for (int y = s.y0; y < s.y1; y++) {
        if (q <= 0) {
          copyRow(s.out, y, A, y);
          if (rib > 0) {
            if (abs(y - CY) <= rib) fillSeg(s.out, y, CX - reachH, 2 * reachH, sw(abs(y - CY) == rib ? goldDk : gold));
            if (abs(y - CY) <= reachV) { fillSeg(s.out, y, CX - rib, 2 * rib + 1, sw(gold)); fillSeg(s.out, y, CX + rib, 1, sw(goldDk)); }
          }
          continue;
        }
        copyRow(s.out, y, B, y);
        const int yt = y + dT;                                               // the top flap, gone up
        if (yt >= 0 && yt < CY) { const int hw = (int)((CY - yt) * slope); copySeg(s.out, y, CX - hw, A, yt, CX - hw, 2 * hw); }
        const int yb = y - dT;                                               // the bottom one, down
        if (yb > CY && yb < H) { const int hw = (int)((yb - CY) * slope); copySeg(s.out, y, CX - hw, A, yb, CX - hw, 2 * hw); }
        const int side = (int)(abs(y - CY) * slope);                         // the side ones, out
        if (CX - side > 0) copySeg(s.out, y, -dS, A, y, 0, CX - side);
        if (CX + side < W) copySeg(s.out, y, CX + side + dS, A, y, CX + side, W - CX - side);
      }
      if (q > 0) {                                                           // gold along the flaps' edges
        s.g.drawLine(0, -dT, CX, CY - dT, gold); s.g.drawLine(CX, CY - dT, W, -dT, gold);
        s.g.drawLine(0, H + dT, CX, CY + dT, gold); s.g.drawLine(CX, CY + dT, W, H + dT, gold);
        s.g.drawLine(-dS, 0, CX - dS, CY, gold); s.g.drawLine(CX - dS, CY, -dS, H, gold);
        s.g.drawLine(W + dS, 0, CX + dS, CY, gold); s.g.drawLine(CX + dS, CY, W + dS, H, gold);
        const float b = seg(p, 0.42f, 1);                                    // sparkle bursting out of the box
        for (int i = 0; i < 18 && b > 0 && b < 1; i++) {
          const float ang = hashf(i * 7 + 2) * 6.2832f, dist = (0.15f + hashf(i * 3 + 9) * 0.9f) * easeOut(b) * W * 0.6f;
          const int sx = CX + (int)(cosf(ang) * dist), sy2 = CY + (int)(sinf(ang) * dist * 0.7f) + (int)(b * b * H * 0.2f);
          if (touches(s, sy2 - 3, sy2 + 3)) star(s.g, sx, sy2, i % 3 == 0 ? 2 : 1, i % 2 ? gold : white);
        }
      }
      if (rib > 0 && q <= 0 && touches(s, CY - u(16), CY + u(16))) bow(s.g, CX, CY, clamp01(tie) * (1 - undo * 0.6f), gold, goldDk);
    });
    (void)t;
    if (p >= 1 || testStop(t0)) break;
  }
  if (!stopped()) pushFull(to);
}

// ---- lock: the snow globe -----------------------------------------------------------------------
void globe(Canvas& from, Canvas& to, uint16_t ms) {
  const Theme& c = T();
  const uint16_t* A = bufOf(from);
  const uint16_t* B = bufOf(to);
  const float R0 = sqrtf((float)(CX * CX + CY * CY)) + 4, Rg = H * 0.36f;
  const uint16_t dark = sw(mix565(c.bg, 0, 110)), glass = mix565(c.bg, 0xFFFF, 150), glassDim = mix565(c.bg, 0xFFFF, 70);
  const uint16_t wood = rgb(110, 68, 38), woodDk = rgb(74, 46, 25), gold = rgb(255, 205, 80), snow = rgb(240, 246, 250);
  for (uint32_t t0 = tnow();;) {
    const uint32_t el = tnow() - t0;
    const float p = clamp01(el / (float)ms), t = el / 1000.0f;
    const float close = easeInOut(seg(p, 0, 0.26f)), reopen = easeIn(seg(p, 0.84f, 1));
    const float r = p < 0.26f ? lerpf(R0, Rg, close) : p < 0.84f ? Rg : lerpf(Rg, R0, reopen);
    const int lift = (int)(H / 14 * (p < 0.26f ? close : p < 0.84f ? 1 : 1 - reopen));
    const float shakeAmt = seg(p, 0.26f, 0.32f) * (1 - seg(p, 0.32f, 0.6f));
    const int shake = (int)(sinf(t * 46) * H * 0.04f * shakeAmt);
    const int gx = CX + shake, gy = CY - lift;
    const uint16_t* src = p < 0.26f ? A : B;
    const float baseIn = easeOut(seg(p, 0.12f, 0.3f)) * (1 - seg(p, 0.84f, 0.95f));
    const int baseTop = gy + (int)(r * 0.82f) + (int)((1 - baseIn) * H * 0.3f);
    frameStrips(0, H, [&](Strip& s) {
      for (int y = s.y0; y < s.y1; y++) {
        const float dy = (float)(y - gy), span = r * r - dy * dy;
        const int half = span > 0 ? (int)sqrtf(span) : 0;
        const int a = max(0, gx - half), b = min(W, gx + half);
        fillSeg(s.out, y, 0, a, dark);
        if (b > a) copySeg(s.out, y, a, src, y, a - shake, b - a);
        fillSeg(s.out, y, b, W - b, dark);
      }
      if (p < 0.95f && r < R0 - 2) {
        if (baseIn > 0 && touches(s, baseTop - 2, baseTop + H / 6)) {         // the base it stands on
          const int bw = (int)(r * 0.9f), bh = H / 8;
          s.g.fillTriangle(gx - bw, baseTop + bh, gx + bw, baseTop + bh, gx - bw + bh / 2, baseTop, wood);
          s.g.fillTriangle(gx + bw, baseTop + bh, gx - bw + bh / 2, baseTop, gx + bw - bh / 2, baseTop, wood);
          s.g.fillRect(gx - bw + bh / 3, baseTop + bh / 3, 2 * bw - 2 * bh / 3, max(2, bh / 4), gold);
          s.g.drawFastHLine(gx - bw, baseTop + bh, 2 * bw, woodDk);
        }
        s.g.drawCircle(gx, gy, (int)r, glass);                                // the glass
        s.g.drawCircle(gx, gy, (int)r - 1, glassDim);
        for (int k = 0; k < 14; k++) {                                        // its shine
          const float ang = 3.6f + k * 0.045f;
          const int hx = gx + (int)(cosf(ang) * (r - 6)), hy = gy + (int)(sinf(ang) * (r - 6));
          s.g.drawPixel(hx, hy, glass); s.g.drawPixel(hx + 1, hy, glassDim);
        }
      }
      if (p > 0.26f && p < 0.88f) {                                           // the snow inside
        const float settle = easeIn(seg(p, 0.5f, 0.82f)), fade = 1 - seg(p, 0.8f, 0.88f);
        for (int i = 0; i < 46; i++) {
          const float ang = hashf(i * 7 + 1) * 6.2832f + t * (2.2f + hashf(i) * 2) * (1 - settle);
          const float rad = (0.15f + 0.75f * hashf(i * 11 + 3)) * r;
          const float fx0 = gx + cosf(ang) * rad, fy0 = gy + sinf(ang) * rad * 0.8f;
          const float restX = gx + (hashf(i * 13 + 5) - 0.5f) * r * 1.3f;
          const float restY = gy + sqrtf(fmaxf(0, r * r - (restX - gx) * (restX - gx))) * 0.88f - hashf(i * 17) * 5;
          const int fx = (int)lerpf(fx0, restX, settle), fy = (int)lerpf(fy0, restY, settle);
          if (fade <= 0 || !touches(s, fy - 1, fy + 2)) continue;
          s.g.fillRect(fx, fy, i % 3 ? 2 : 1, i % 3 ? 2 : 1, fade > 0.5f ? snow : glassDim);
        }
      }
    });
    if (p >= 1 || testStop(t0)) break;
  }
  if (!stopped()) pushFull(to);
}

// ---- wake and sleep (whole frames into a sprite, fx.cpp's render) -------------------------------
// The string of lights: two strands, one along the top sagging down, one along the bottom.
void strandAt(int k, int i, int n, float pull, int& x, int& y) {
  const float v = (i + 0.5f) / n;
  x = (int)(v * W);
  y = k ? (int)(H * 0.8f - sinf(v * 3.1416f) * H * 0.09f + pull * H * 0.35f)
        : (int)(H * 0.16f + sinf(v * 3.1416f) * H * 0.11f - pull * H * 0.35f);
}

void lightsOn(Canvas& f, lgfx::LovyanGFX& d, float p) {
  constexpr int N = 11;
  const bool picture = p >= 0.62f;
  if (picture) f.pushSprite(&d, 0, 0); else d.fillScreen(TFT_BLACK);
  const float pull = easeIn(seg(p, 0.66f, 0.92f));
  if (pull >= 1) return;
  const bool flick = (p > 0.5f && p < 0.535f) || (p > 0.57f && p < 0.6f);
  const uint16_t wire = rgb(36, 70, 46), cap = rgb(110, 110, 110);
  for (int k = 0; k < 2; k++) {
    int px, py; strandAt(k, -1, N, pull, px, py); px = -4;
    for (int i = 0; i <= N; i++) {
      int x, y; strandAt(k, i, N, pull, x, y);
      if (i == N) x = W + 4;
      d.drawLine(px, py, x, y, wire); d.drawLine(px, py + 1, x, y + 1, wire);
      px = x; py = y;
    }
    for (int i = 0; i < N; i++) {
      int x, y; strandAt(k, i, N, pull, x, y);
      const bool lit = !flick && p > 0.04f + (k * N + i) / (2.0f * N) * 0.44f;
      const uint16_t col = bulb(i + k);
      if (lit && !picture) { d.fillCircle(x, y + u(4), u(7), mix565(0, col, 60)); d.fillCircle(x, y + u(4), u(4.5f), mix565(0, col, 120)); }
      d.fillRect(x - u(1.5f), y + u(1), u(3), u(5), lit ? col : rgb(46, 46, 52));
      d.fillRect(x - u(1), y - u(0.5f), u(2), u(1.6f), cap);
      if (lit) d.drawPixel(x, y + u(2), mix565(col, 0xFFFF, 150));
    }
  }
}

void twinkle(lgfx::LovyanGFX& d, int x, int y, float s, uint16_t c, uint16_t glow) {
  if (s < 0.5f) return;
  d.fillCircle(x, y, (int)(s * 1.4f), glow);
  d.drawLine(x - (int)(s * 2.2f), y, x + (int)(s * 2.2f), y, c);
  d.drawLine(x, y - (int)(s * 2.2f), x, y + (int)(s * 2.2f), c);
  d.drawLine(x - (int)(s * 0.9f), y - (int)(s * 0.9f), x + (int)(s * 0.9f), y + (int)(s * 0.9f), c);
  d.drawLine(x - (int)(s * 0.9f), y + (int)(s * 0.9f), x + (int)(s * 0.9f), y - (int)(s * 0.9f), c);
  d.fillCircle(x, y, max(1, (int)(s * 0.5f)), 0xFFFF);
}

void starOut(Canvas& f, lgfx::LovyanGFX& d, float p, bool final) {
  const uint16_t gold = rgb(255, 214, 90), glow = rgb(90, 70, 20);
  d.fillScreen(TFT_BLACK);
  if (!final) {
    const float a = 0.66f;
    if (p < a) {                                                              // spinning down into the middle
      const float t = p / a, z = max(0.012f, 1 - easeIn(t));
      f.pushRotateZoom(&d, CX, CY, t * 50.0f, z, z);
      if (t > 0.5f) twinkle(d, CX, CY, u(5) * seg(t, 0.5f, 1), gold, glow);
    } else {                                                                  // the star twinkles out
      const float t = (p - a) / (1 - a);
      twinkle(d, CX, CY, u(5) * (1 - easeIn(t)) * (1 + 0.25f * sinf(t * 30)), gold, glow);
    }
    return;
  }
  // Powering down: the picture spins down into a little tree, whose lights go out one by
  // one, then the star on top.
  if (p < 0.3f) {
    const float t = p / 0.3f, z = max(0.012f, 1 - easeIn(t));
    f.pushRotateZoom(&d, CX, CY, t * 50.0f, z, z);
  }
  const float grow = easeOutBack(seg(p, 0.22f, 0.4f));
  if (grow <= 0) return;
  const int th = (int)(H * 0.62f * grow), ty = CY + H * 0.3f;
  const uint8_t dim = (uint8_t)(255 * (1 - 0.75f * seg(p, 0.45f, 0.9f)) * (1 - seg(p, 0.92f, 1)));   // the tree fades as its lights go
  const uint16_t pine = mix565(0, rgb(26, 92, 50), dim), pineDk = mix565(0, rgb(16, 60, 33), dim);
  for (int k = 0; k < 3; k++) {
    const int ay = ty - th + k * th * 23 / 100, by = ay + th * 44 / 100, hw = th * (17 + 9 * k) / 100;
    d.fillTriangle(CX, ay, CX - hw, by, CX + hw, by, k & 1 ? pineDk : pine);
  }
  d.fillRect(CX - u(2), ty - th / 7 + 1, u(4), th / 7, mix565(0, rgb(90, 58, 30), dim));
  for (int i = 0; i < 16; i++) {                                               // its lights, going out
    const float v = 0.22f + 0.68f * hashf(i * 5 + 3), w = (hashf(i * 9 + 1) - 0.5f) * 0.8f * v;
    const int bx = CX + (int)(w * th * 0.6f), by = ty - th + (int)(v * th * 0.86f);
    const bool on = p < 0.42f + hashf(i * 13 + 7) * 0.38f;
    if (on) { d.fillCircle(bx, by, u(2.5f), mix565(0, bulb(i), 90)); d.fillCircle(bx, by, u(1.2f), bulb(i)); }
  }
  const float st = seg(p, 0.82f, 0.98f);
  const bool flick = st > 0.4f && ((int)(st * 30) % 3 == 0);
  if (st < 1 && !flick) twinkle(d, CX, ty - th - u(1), u(3.5f) * grow * (1 - st * st), gold, glow);
}

}  // namespace

bool squatchmasTransition(Trans kind, Canvas& from, Canvas& to) {
  switch (kind) {
  case Trans::Forward: wrapTear(from, to, 600); return true;
  case Trans::Back:    sleigh(from, to, 620); return true;
  case Trans::Unlock:  present(from, to, 950); return true;
  case Trans::Lock:    globe(from, to, 1250); return true;
  default:             return false;
  }
}

void squatchmasRender(uint8_t kind, Canvas& f, lgfx::LovyanGFX& d, float p) {
  if (kind == 0) lightsOn(f, d, p);
  else starOut(f, d, p, kind == 2);
}

}  // namespace fx
