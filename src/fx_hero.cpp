// Hero: a hooded adventurer's night - the sword, gold, hearts.
//   forward  A sword stroke: a blazing arc sweeps up across the screen, the cut
//            gleams, and a beat later the two halves slide apart along it onto the
//            new screen, sparks spilling off the edges.
//   back     The backhand: the same stroke from the other side.
//   unlock   X-slash: two strokes cross, and the lock screen falls apart in four
//            pieces that fly off, home underneath.
//   lock     Mend: the lock screen's four pieces fly in and meet, the X where they
//            join flashes gold, and a heart pops where they meet.
// Wake and sleep stay the iris (fx.cpp).

#include "fx_internal.h"

namespace fx {
using namespace k;
namespace {

struct Pt { float x, y; };
inline Pt along(Pt a, Pt b, float s) { return {a.x + (b.x - a.x) * s, a.y + (b.y - a.y) * s}; }

// The sword's arc along the line a-b, between tail and tip (0..1 along it, and a little
// past either end): a crescent blade of light hugging the cut, thin at the tail and
// full at the front - orange outside, gold, a white-hot core - with a glint at the tip.
struct Swoosh {
  static constexpr int N = 22;
  int16_t ix[N + 1], iy[N + 1];          // inner edge, on the cut
  float nx, ny, th[N + 1];               // normal, and the blade's width along it
  int top = 0, bottom = -1;              // rows it covers this frame
  bool on = false, glint = false;
  Pt tipAt;
  void set(Pt a, Pt b, float tail, float tip) {
    on = tip > tail;
    if (!on) return;
    const float dx = b.x - a.x, dy = b.y - a.y, len = sqrtf(dx * dx + dy * dy);
    nx = dy / len; ny = -dx / len;
    top = 9999; bottom = -9999;
    for (int i = 0; i <= N; i++) {
      const float q = i / (float)N;                          // 0 tail .. 1 tip
      const Pt c = along(a, b, lerpf(tail, tip, q));
      ix[i] = (int16_t)c.x; iy[i] = (int16_t)c.y;
      th[i] = 15 * sinf(3.1416f * 0.9f * powf(q, 0.7f)) + 1.5f;
      top = min(top, (int)(c.y - 18)); bottom = max(bottom, (int)(c.y + 18));
    }
    glint = tip < 1.05f;
    tipAt = along(a, b, tip);
  }
  void draw(lgfx::LovyanGFX& g, const Theme& c, const Strip& s) const {
    if (!on || !touches(s, top, bottom)) return;
    const uint16_t layer[3] = {c.amber, c.green, TFT_WHITE};
    const float scale[3] = {1.0f, 0.6f, 0.28f};
    for (int l = 0; l < 3; l++)
      for (int i = 0; i < N; i++) {
        // The strip of blade between two samples: from just across the cut out to its width.
        const float w0 = th[i] * scale[l], w1 = th[i + 1] * scale[l];
        const int ax = ix[i] - (int)(nx * 1.5f), ay = iy[i] - (int)(ny * 1.5f);
        const int bx = ix[i + 1] - (int)(nx * 1.5f), by = iy[i + 1] - (int)(ny * 1.5f);
        const int cx = ix[i + 1] + (int)(nx * w1), cy = iy[i + 1] + (int)(ny * w1);
        const int dx = ix[i] + (int)(nx * w0), dy = iy[i] + (int)(ny * w0);
        g.fillTriangle(ax, ay, bx, by, cx, cy, layer[l]);
        g.fillTriangle(ax, ay, cx, cy, dx, dy, layer[l]);
      }
    if (glint) { star(g, (int)tipAt.x, (int)tipAt.y, 9, TFT_WHITE); star(g, (int)tipAt.x, (int)tipAt.y, 4, c.green); }
  }
};

// A cut edge from a to b moved by (ox, oy).
void edge(lgfx::LovyanGFX& g, Pt a, Pt b, int ox, int oy, uint16_t col) {
  thickLine(g, (int)a.x + ox, (int)a.y + oy, (int)b.x + ox, (int)b.y + oy, 2, col);
}

uint16_t heat(const Theme& c, float f) {                    // 1 fresh .. 0 cooled
  return f > 0.66f ? TFT_WHITE : f > 0.33f ? c.green : c.amber;
}

// Sparks thrown off a cut from a to b, carried on in the direction of the stroke.
template <size_t N> void spray(Mote (&m)[N], const Theme& c, Pt a, Pt b, int n, uint32_t seed) {
  const float dx = b.x - a.x, dy = b.y - a.y, len = sqrtf(dx * dx + dy * dy);
  for (int i = 0; i < n; i++) {
    const float s = hashf(seed + i * 7), side = hashf(seed + i * 11) < 0.5f ? -1 : 1;
    const float sp = 60 + hashf(seed + i * 13) * 220, carry = 80 + hashf(seed + i * 17) * 260;
    const Pt q = along(a, b, s);
    spawnMote(m, q.x, q.y, dx / len * carry + side * dy / len * sp, dy / len * carry - side * dx / len * sp,
              0.16f + hashf(seed + i * 19) * 0.18f, i % 3 == 0 ? TFT_WHITE : i % 3 == 1 ? c.green : c.amber,
              2 + (uint8_t)(hashf(seed + i) * 2));
  }
}

// ---- forward / back: one stroke, two halves ---------------------------------------------
void swordSlash(Canvas& from, Canvas& to, bool back, uint16_t ms) {
  const Theme& c = T();
  const uint16_t* A = bufOf(from);
  const uint16_t* B = bufOf(to);
  // The cut runs through the centre, rising the way the sword swings: forward from
  // lower left to upper right, back from lower right to upper left.
  const float m = back ? 0.3f : -0.3f;
  auto yAt = [&](float x) { return CY + m * (x - CX); };
  const Pt a = {back ? (float)W : 0.0f, yAt(back ? W : 0)};
  const Pt b = {back ? 0.0f : (float)W, yAt(back ? 0 : W)};
  const float SWING = 0.26f, SPLIT = 0.38f;
  Mote sparks[48] = {};
  Swoosh sw1;
  bool cut = false;
  uint32_t last = tnow(), frame = 0;
  for (uint32_t t0 = tnow();; frame++) {
    const uint32_t now = tnow();
    const float dt = (now - last) / 1000.0f;
    last = now;
    const float p = clamp01((now - t0) / (float)ms);
    // The halves: the upper one slides up and back along the cut, the lower one down
    // and on, so they part like something really cut through.
    const float e = easeIn(seg(p, SPLIT, 1));
    const int ux = (int)(e * (back ? 80 : -80)), uy = (int)(e * -212), lx = -ux, ly = -uy;
    const float tip = lerpf(-0.08f, 1.12f, easeInOut(seg(p, 0, SWING)));
    sw1.set(a, b, lerpf(-0.08f, 1.12f, easeInOut(seg(p, 0.07f, SWING + 0.1f))), tip);
    if (!cut && p >= SWING) {
      cut = true;
      buzzOnce();
      spray(sparks, c, a, b, 40, 17);
    }
    stepMotes(sparks, dt, 900);
    // A beat of hit-stop: the screen trembles between the stroke and the split.
    int shx = 0, shy = 0;
    if (p >= SWING && p < SPLIT + 0.06f) {
      const float sk = 4 * (1 - seg(p, SWING, SPLIT + 0.06f));
      shx = (int)(sk * (hashf(frame * 3) - 0.5f) * 2);
      shy = (int)(sk * (hashf(frame * 5 + 1) - 0.5f) * 2);
    }
    const uint16_t hot = heat(c, 1 - seg(p, SPLIT, 0.9f));
    const int gw = (int)(7 * (1 - seg(p, SWING, SPLIT)));
    frameStrips(0, H, [&](Strip& s) {
      for (int y = s.y0; y < s.y1; y++) {
        if (e <= 0) { copyRow(s.out, y, A, y); continue; }
        copyRow(s.out, y, B, y);
        int sy = y - uy;
        if (sy >= 0 && sy < H) {
          const int xc = constrain((int)lroundf(CX + (sy - CY) / m), 0, W);
          if (m < 0) copySeg(s.out, y, ux, A, sy, 0, xc);                  // above the cut
          else       copySeg(s.out, y, xc + ux, A, sy, xc, W - xc);
        }
        sy = y - ly;
        if (sy >= 0 && sy < H) {
          const int xc = constrain((int)lroundf(CX + (sy - CY) / m), 0, W);
          if (m < 0) copySeg(s.out, y, xc + lx, A, sy, xc, W - xc);        // below it
          else       copySeg(s.out, y, lx, A, sy, 0, xc);
        }
      }
      if (e <= 0) {                                        // the cut so far, and its gleam
        const Pt q = along(a, b, min(tip, 1.0f));
        if (tip > 0) { thickLine(s.g, (int)a.x, (int)a.y, (int)q.x, (int)q.y, 3, c.green); s.g.drawLine((int)a.x, (int)a.y, (int)q.x, (int)q.y, TFT_WHITE); }
        if (p >= SWING && gw > 1) thickLine(s.g, (int)a.x, (int)a.y, (int)b.x, (int)b.y, gw, TFT_WHITE);
      } else {
        edge(s.g, a, b, ux, uy, hot);
        edge(s.g, a, b, lx, ly, hot);
      }
      sw1.draw(s.g, c, s);
      drawMotes(s.g, sparks, true);
    }, shx, shy);
    if (p >= 1 || testStop(t0)) break;
  }
  if (!stopped()) pushFull(to);
}

// ---- the X: four pieces ------------------------------------------------------------------
// Corner to corner: line 1 lower left to upper right, line 2 upper left to lower right.
const Pt L1a = {0, (float)H}, L1b = {(float)W, 0}, L2a = {0, 0}, L2b = {(float)W, (float)H};
const Pt MID = {(float)CX, (float)CY};
enum { TOP, LEFT, RIGHT, BOTTOM };

// Columns [s0, s1) of source row sy that belong to piece i; false if none do.
bool pieceSpan(int i, int sy, int& s0, int& s1) {
  const int d = (int)lroundf((sy - CY) * (float)W / H);
  const int x1 = constrain(CX - d, 0, W), x2 = constrain(CX + d, 0, W);
  if (sy < CY) {
    if (i == TOP)   { s0 = x2; s1 = x1; return true; }
    if (i == LEFT)  { s0 = 0; s1 = x2; return true; }
    if (i == RIGHT) { s0 = x1; s1 = W; return true; }
    return false;
  }
  if (i == BOTTOM) { s0 = x1; s1 = x2; return true; }
  if (i == LEFT)   { s0 = 0; s1 = x1; return true; }
  if (i == RIGHT)  { s0 = x2; s1 = W; return true; }
  return false;
}

// The strip's rows: the backdrop, then each piece of src moved by (ox[i], oy[i]).
void composePieces(const Strip& s, const uint16_t* back, const uint16_t* src, const int* ox, const int* oy) {
  for (int y = s.y0; y < s.y1; y++) {
    copyRow(s.out, y, back, y);
    for (int i = 0; i < 4; i++) {
      const int sy = y - oy[i];
      int s0, s1;
      if (sy < 0 || sy >= H || !pieceSpan(i, sy, s0, s1)) continue;
      copySeg(s.out, y, s0 + ox[i], src, sy, s0, s1 - s0);
    }
  }
}

// The two cut edges of piece i, moved by (ox, oy).
void pieceEdges(lgfx::LovyanGFX& g, int i, int ox, int oy, uint16_t col) {
  const Pt ends[4][2] = {{L2a, L1b}, {L2a, L1a}, {L1b, L2b}, {L1a, L2b}};
  edge(g, MID, ends[i][0], ox, oy, col);
  edge(g, MID, ends[i][1], ox, oy, col);
}

// ---- unlock: X-slash ---------------------------------------------------------------------
void xSlash(Canvas& from, Canvas& to, uint16_t ms) {
  const Theme& c = T();
  const uint16_t* A = bufOf(from);
  const uint16_t* B = bufOf(to);
  const float CROSS = 0.22f, SPLIT = 0.30f;
  // Where each piece flies (px/s), under gravity: all four are off screen by the end.
  const float vx[4] = {0, -560, 560, 0}, vy[4] = {-640, -230, -230, 170}, G = 900;
  Mote sparks[64] = {};
  Swoosh s1, s2;
  bool crossed = false;
  uint32_t last = tnow(), frame = 0;
  for (uint32_t t0 = tnow();; frame++) {
    const uint32_t now = tnow();
    const float dt = (now - last) / 1000.0f;
    last = now;
    const float p = clamp01((now - t0) / (float)ms);
    const float ts = p > SPLIT ? (p - SPLIT) * ms / 1000.0f : 0;
    int ox[4], oy[4];
    for (int i = 0; i < 4; i++) { ox[i] = (int)(vx[i] * ts); oy[i] = (int)(vy[i] * ts + 0.5f * G * ts * ts); }
    const float c1 = lerpf(-0.08f, 1.12f, easeInOut(seg(p, 0, 0.12f)));
    const float c2 = lerpf(-0.08f, 1.12f, easeInOut(seg(p, 0.10f, CROSS)));
    s1.set(L1a, L1b, lerpf(-0.08f, 1.12f, easeInOut(seg(p, 0.03f, 0.17f))), c1);
    s2.set(L2a, L2b, lerpf(-0.08f, 1.12f, easeInOut(seg(p, 0.13f, 0.27f))), c2);
    if (!crossed && p >= CROSS) {
      crossed = true;
      buzzOnce();
      spray(sparks, c, L1a, L1b, 24, 5);
      spray(sparks, c, L2a, L2b, 24, 91);
      for (int i = 0; i < 14; i++) {                       // and a burst from the crossing
        const float ang = i * 0.4488f, sp = 200 + hashf(i * 3) * 260;
        spawnMote(sparks, CX, CY, cosf(ang) * sp, sinf(ang) * sp, 0.3f, i % 2 ? c.green : TFT_WHITE, 3);
      }
    }
    stepMotes(sparks, dt, 900);
    int shx = 0, shy = 0;
    if (p >= CROSS && p < SPLIT + 0.1f) {
      const float sk = 7 * (1 - seg(p, CROSS, SPLIT + 0.1f));
      shx = (int)(sk * (hashf(frame * 3) - 0.5f) * 2);
      shy = (int)(sk * (hashf(frame * 5 + 1) - 0.5f) * 2);
    }
    const uint16_t hot = heat(c, 1 - seg(p, SPLIT, 0.85f));
    const float f = 1 - seg(p, CROSS, SPLIT);
    frameStrips(0, H, [&](Strip& s) {
      if (ts <= 0) {
        for (int y = s.y0; y < s.y1; y++) copyRow(s.out, y, A, y);
        // The wounds so far.
        if (c1 > 0) { const Pt q = along(L1a, L1b, min(c1, 1.0f)); thickLine(s.g, 0, H, (int)q.x, (int)q.y, 3, c.green); }
        if (c2 > 0) { const Pt q = along(L2a, L2b, min(c2, 1.0f)); thickLine(s.g, 0, 0, (int)q.x, (int)q.y, 3, c.green); }
        if (p >= CROSS) {                                  // the gleam, and a flash where they cross
          const int gw = (int)(8 * f);
          if (gw > 1) { thickLine(s.g, 0, H, W, 0, gw, TFT_WHITE); thickLine(s.g, 0, 0, W, H, gw, TFT_WHITE); }
          s.g.fillCircle(CX, CY, (int)(6 + 30 * f), TFT_WHITE);
          star(s.g, CX, CY, (int)(10 + 30 * f), c.green);
        }
      } else {
        composePieces(s, B, A, ox, oy);
        for (int i = 0; i < 4; i++) pieceEdges(s.g, i, ox[i], oy[i], hot);
      }
      s1.draw(s.g, c, s);
      s2.draw(s.g, c, s);
      drawMotes(s.g, sparks, true);
    }, shx, shy);
    if (p >= 1 || testStop(t0)) break;
  }
  if (!stopped()) pushFull(to);
}

// ---- lock: mend --------------------------------------------------------------------------
void mend(Canvas& from, Canvas& to, uint16_t ms) {
  const Theme& c = T();
  const uint16_t* A = bufOf(from);
  const uint16_t* B = bufOf(to);
  const float JOIN = 0.5f;
  // Each piece starts off screen on its own side; the sides come in first.
  const float sx0[4] = {0, -300, 300, 0}, sy0[4] = {-200, -30, -30, 200}, start[4] = {0.1f, 0, 0, 0.1f};
  Mote sparks[56] = {};
  bool joined = false;
  uint32_t last = tnow(), frame = 0;
  for (uint32_t t0 = tnow();; frame++) {
    const uint32_t now = tnow();
    const float dt = (now - last) / 1000.0f;
    last = now;
    const float p = clamp01((now - t0) / (float)ms);
    int ox[4], oy[4];
    for (int i = 0; i < 4; i++) {
      const float f = 1 - easeOutBack(seg(p, start[i], JOIN), 1.3f);
      ox[i] = (int)(sx0[i] * f); oy[i] = (int)(sy0[i] * f);
    }
    if (!joined && p >= JOIN) {
      joined = true;
      buzzOnce();
      spray(sparks, c, L1a, L1b, 20, 33);
      spray(sparks, c, L2a, L2b, 20, 77);
    }
    stepMotes(sparks, dt, 700);
    // The seam flares white, cools to gold and fades out; a heart pops where they met.
    const float f = 1 - seg(p, JOIN, 0.9f);
    const int seamW = (int)(1 + 6 * f);
    const uint16_t seamC = f > 0.6f ? TFT_WHITE : f > 0.25f ? c.green : c.amber;
    const float hp = seg(p, JOIN, 0.62f), hf = seg(p, 0.75f, 0.95f);
    const int hs = (int)(16 * easeOutBack(hp, 2.2f) * (1 - hf));
    const int shy = (p >= JOIN && p < JOIN + 0.14f) ? (int)(5 * (1 - seg(p, JOIN, JOIN + 0.14f)) * (frame % 2 ? 1 : -1)) : 0;
    frameStrips(0, H, [&](Strip& s) {
      if (p < JOIN) {
        composePieces(s, A, B, ox, oy);
        for (int i = 0; i < 4; i++) pieceEdges(s.g, i, ox[i], oy[i], c.green);
      } else {
        for (int y = s.y0; y < s.y1; y++) copyRow(s.out, y, B, y);
        if (f > 0) { thickLine(s.g, 0, H, W, 0, seamW, seamC); thickLine(s.g, 0, 0, W, H, seamW, seamC); }
        if (hs > 1 && touches(s, CY - hs - 12, CY + hs + 12)) {
          heart(s.g, CX, CY - hs / 2, hs + 3, c.green);
          heart(s.g, CX, CY - hs / 2 + 1, hs, c.red);
        }
      }
      drawMotes(s.g, sparks, true);
    }, 0, shy);
    if (p >= 1 || testStop(t0)) break;
  }
  if (!stopped()) pushFull(to);
}

}  // namespace

bool heroTransition(Trans kind, Canvas& from, Canvas& to) {
  switch (kind) {
  case Trans::Forward: swordSlash(from, to, false, 480); return true;
  case Trans::Back:    swordSlash(from, to, true, 480); return true;
  case Trans::Unlock:  xSlash(from, to, 780); return true;
  case Trans::Lock:    mend(from, to, 640); return true;
  default:             return false;
  }
}

}  // namespace fx
