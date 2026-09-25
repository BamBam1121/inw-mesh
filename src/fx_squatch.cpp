// Squatch: an old terminal on a radio mesh, and the sasquatch.
//   forward  Channel change - the old picture loses sync and tears sideways into
//            static, then the new one rolls in and locks.
//   back     Rewind - the picture rolls with VHS tracking bands racing up it, and
//            the previous screen rolls down into place.
//   unlock   The sasquatch sprints across and drags the home screen in behind him.
//   lock     He drops out of the sky in a ground-pound; the shockwave stamps the
//            lock screen out from under his feet, then he lopes off.
// Wake and sleep stay the CRT warm-up and collapse (fx.cpp).

#include "fx_internal.h"
#include "mascot.h"

namespace fx {
using namespace k;
namespace {

void tvStatic(uint16_t* out, int y, uint32_t seed, const Theme& c) {
  noiseRow(out, y, seed, c.bg, c.greenDim, c.dim, c.green);
}

// ---- channel change / rewind ---------------------------------------------------------
void channel(Canvas& from, Canvas& to, bool rewind, uint16_t ms) {
  const Theme& c = T();
  const uint16_t* A = bufOf(from);
  const uint16_t* B = bufOf(to);
  uint32_t frame = 0;
  for (uint32_t t0 = tnow();; frame++) {
    const uint32_t el = tnow() - t0;
    const float p = clamp01(el / (float)ms), t = el / 1000.0f;
    // Out (the old screen breaking up), a burst of pure static, then in.
    const uint16_t* src;
    float amp, stat, chroma, roll;
    if (p < 0.34f) {
      const float u = seg(p, 0, 0.34f);
      src = A; amp = 38 * easeIn(u); stat = 0.45f * seg(p, 0.1f, 0.34f); chroma = 3 * u;
      roll = rewind ? -0.55f * H * easeIn(u) : 0;
    } else if (p < 0.48f) {
      src = nullptr; amp = 0; stat = 1; chroma = 0; roll = 0;
    } else {
      const float u = seg(p, 0.48f, 1), e = 1 - easeOutCubic(u);
      src = B; amp = 38 * e; stat = 0.45f * (1 - seg(p, 0.48f, 0.78f)); chroma = 3 * (1 - seg(p, 0.48f, 0.9f));
      // Rolling back down: comes in from above and settles with a little bounce.
      roll = rewind ? 0.55f * H * (1 - easeOutBack(u, 1.4f)) : 0;
    }
    const int cr = (int)(chroma + 0.5f), iroll = (int)roll;
    frameStrips(0, H, [&](Strip& s) {
      for (int y = s.y0; y < s.y1; y++) {
        const uint32_t rs = hash32(y * 131 + frame * 7919);
        if (!src || (rs & 0xFFFF) < stat * 65535) { tvStatic(s.out, y, rs, c); continue; }
        int sy = (y + iroll) % H;
        if (sy < 0) sy += H;
        // Sideways tearing: two waves and a little per-row jitter; a rewind shimmers
        // instead, its trouble is vertical.
        const float wob = rewind ? 0.22f * sinf(y * 0.21f + t * 60)
                                 : 0.6f * sinf(y * 0.045f + t * 20) + 0.4f * sinf(y * 0.13f - t * 31);
        const int off = (int)(amp * wob) + (int)(((int)((rs >> 16) % 9) - 4) * amp / 20);
        copyRowWrap(s.out, y, src, sy, off);
        if (cr) chromaRow(s.out, y, cr, cr);
      }
      if (rewind && src)                                   // VHS tracking bands racing upward
        for (int b = 0; b < 3; b++) {
          const int by = H - (int)fmodf(t * 1100 + b * 97, H + 30);
          const int bh = 5 + b * 3;
          for (int yy = max(by, s.y0); yy < min(by + bh, s.y1); yy++) tvStatic(s.out, yy, hash32(yy * 17 + frame * 31 + b), c);
          fillSeg(s.out, by - 1, 0, W, sw(c.white));
        }
      if (!src) {                                          // the burst: a bright bar rolls through
        const int by = (int)fmodf(t * 1700, H);
        fillSeg(s.out, by, 0, W, sw(c.white));
        fillSeg(s.out, (by + 1) % H, 0, W, sw(c.green));
        fillSeg(s.out, (by + 2) % H, 0, W, sw(c.greenDim));
      }
    });
    if (p >= 1 || testStop(t0)) break;
  }
  if (!stopped()) pushFull(to);
}

// ---- unlock: the sasquatch drags the home screen in -------------------------------------
inline int shag(int y) { return (int)(hash32((y >> 2) * 7 + 3) % 13) - 6; }   // his torn, shaggy edge

void sasquatchRun(Canvas& from, Canvas& to, uint16_t ms) {
  const Theme& c = T();
  const uint16_t* A = bufOf(from);
  const uint16_t* B = bufOf(to);
  const int hgt = 150, ground = H - 8;
  Mote dust[40] = {};
  float phase = 0;
  uint32_t last = tnow(), frame = 0;
  for (uint32_t t0 = tnow();; frame++) {
    const uint32_t now = tnow();
    const float dt = (now - last) / 1000.0f;
    last = now;
    const float p = clamp01((now - t0) / (float)ms);
    const float u = seg(p, 0.04f, 0.96f);
    const float x = lerpf(-70, W + 110, easeInOut(u));    // his hip
    const int back = (int)x - 40;                          // behind him: the new screen
    // Dust off his feet (none late on, so it has settled by the end), and the stride.
    if (u > 0 && u < 0.72f && dt > 0)
      for (int k2 = 0; k2 < 3; k2++) {
        const float r1 = hashf(frame * 3 + k2), r2 = hashf(frame * 5 + k2 + 99);
        spawnMote(dust, x - 16 + r1 * 20, ground - 2, -40 - r1 * 90, -60 - r2 * 90, 0.25f + r2 * 0.15f,
                  k2 ? c.dim : c.greenDim, 3 + (uint8_t)(r2 * 3));
      }
    stepMotes(dust, dt, 260);
    phase += dt * 15;                                      // a sprint, not the scene's stroll
    frameStrips(0, H, [&](Strip& s) {
      for (int y = s.y0; y < s.y1; y++) {
        const int e = constrain(back + shag(y), 0, W);
        if (e > 0) copySeg(s.out, y, 0, B, y, 0, e);
        if (e < W) copySeg(s.out, y, e, A, y, e, W - e);
        // The edge glows where he tore through.
        if (!(y & 1) && e > 1 && e < W) s.g.drawFastHLine(e - 2, y, 2, c.green);
      }
      if (back < W)                                        // speed lines streaming off the tear
        for (int i = 0; i < 7; i++) {
          const int sy = 26 + i * 27 + (int)(hashf(i + frame / 2) * 8);
          const int len = 24 + (int)(hashf(i * 13) * 60);
          s.g.drawFastHLine(back - len - 16, sy, len, c.greenDim);
        }
      drawMotes(s.g, dust, false);
      if (x > -60 && x < W + 100 && touches(s, ground - hgt - 12, ground + 4))
        drawSasquatch(s.band, c, (int)x, ground - s.bandY, hgt, phase, c.amber);
    });
    if (p >= 1 || testStop(t0)) break;
  }
  if (!stopped()) pushFull(to);
}

// ---- lock: the ground-pound ------------------------------------------------------------
void groundPound(Canvas& from, Canvas& to, uint16_t ms) {
  const Theme& c = T();
  const uint16_t* A = bufOf(from);
  const uint16_t* B = bufOf(to);
  const int hgt = 120, ground = H - 10, cx = CX;
  const float hit = 0.28f;                                 // when he lands
  // The cracks: jagged, each three segments off in its own direction.
  int16_t crack[9][4][2];
  for (int i = 0; i < 9; i++) {
    const float ang = 3.1416f + i * 3.1416f / 8 + (hashf(i) - 0.5f) * 0.3f;
    const float len = 150 + hashf(i + 20) * 170;
    for (int j = 0; j < 4; j++) {
      const float f = j / 3.0f, jit = j == 0 || j == 3 ? 0 : (hashf(i * 7 + j) - 0.5f) * 30;
      crack[i][j][0] = (int16_t)(cx + cosf(ang) * len * f - sinf(ang) * jit);
      crack[i][j][1] = (int16_t)(ground + sinf(ang) * len * f + cosf(ang) * jit);
    }
  }
  Mote dust[48] = {};
  bool landed = false;
  float phase = 0;
  uint32_t last = tnow(), frame = 0;
  for (uint32_t t0 = tnow();; frame++) {
    const uint32_t now = tnow();
    const float dt = (now - last) / 1000.0f;
    last = now;
    const float p = clamp01((now - t0) / (float)ms);
    const float r = p < hit ? 0 : 330 * easeOutCubic(seg(p, hit, 0.86f));   // the shockwave
    if (!landed && p >= hit) {                             // THUD: dust sprays both ways, the motor kicks
      landed = true;
      buzzOnce();
      for (int i = 0; i < 40; i++) {
        const float r1 = hashf(i * 7 + 1), r2 = hashf(i * 11 + 5);
        const float dir = i % 2 ? 1 : -1;
        spawnMote(dust, cx + dir * 20, ground - 3, dir * (60 + r1 * 260), -(40 + r2 * 200), 0.22f + r1 * 0.2f,
                  i % 3 ? c.dim : c.green, 3 + (uint8_t)(r2 * 3));
      }
    }
    stepMotes(dust, dt, 420);
    // Him: falling feet-first, a crouch on impact, then loping off right.
    int gx = cx, gy = ground;
    if (p < hit) gy = (int)lerpf(-30, ground, easeIn(seg(p, 0, hit)));
    else if (p > 0.5f) { phase += dt * 11; gx = (int)lerpf(cx, W + 100, easeIn(seg(p, 0.5f, 0.95f))); }
    const int squash = (p >= hit && p < hit + 0.08f) ? 14 : 0;
    // Shake: hard at impact, dying away.
    int shx = 0, shy = 0;
    if (p >= hit) {
      const float sk = 9 * (1 - seg(p, hit, hit + 0.3f));
      shx = (int)(sk * (hashf(frame * 3) - 0.5f) * 2);
      shy = (int)(sk * (hashf(frame * 5 + 1) - 0.5f) * 2);
    }
    const float fade = 1 - seg(p, 0.6f, 0.9f);
    frameStrips(0, H, [&](Strip& s) {
      // Inside the ring, the lock screen.
      for (int y = s.y0; y < s.y1; y++) {
        const float dy = y - ground;
        const float span = r * r - dy * dy;
        if (span <= 0) { copyRow(s.out, y, A, y); continue; }
        const int half = (int)sqrtf(span);
        const int a = max(0, cx - half), b = min(W, cx + half);
        if (a > 0) copySeg(s.out, y, 0, A, y, 0, a);
        copySeg(s.out, y, a, B, y, a, b - a);
        if (b < W) copySeg(s.out, y, b, A, y, b, W - b);
      }
      if (r > 2) {                                         // the ring, and the ground cracked open behind it
        for (int k2 = 0; k2 < 3; k2++) s.g.drawCircle(cx, ground, (int)r - k2, k2 == 1 ? TFT_WHITE : c.green);
        if (fade > 0)
          for (int i = 0; i < 9; i++) {
            const float reach = min(1.0f, r / 330 * 1.4f) * fade * 3;   // how many of its 3 segments show
            for (int j = 0; j < 3 && j < reach; j++) {
              const float f = min(1.0f, reach - j);
              const int x0 = crack[i][j][0], y0 = crack[i][j][1];
              const int x1 = x0 + (int)((crack[i][j + 1][0] - x0) * f), y1 = y0 + (int)((crack[i][j + 1][1] - y0) * f);
              s.g.drawLine(x0, y0, x1, y1, c.greenDim);
              if (j == 0) s.g.drawLine(x0 + 1, y0, x1 + 1, y1, c.greenDim);
            }
          }
      }
      drawMotes(s.g, dust, false);
      if (gx < W + 90 && touches(s, gy - hgt - 12, gy + 4)) drawSasquatch(s.band, c, gx, gy - s.bandY, hgt - squash, phase, c.amber);
    }, shx, shy);
    if (p >= 1 || testStop(t0)) break;
  }
  if (!stopped()) pushFull(to);
}

}  // namespace

bool squatchTransition(Trans kind, Canvas& from, Canvas& to) {
  switch (kind) {
  case Trans::Forward: channel(from, to, false, 380); return true;
  case Trans::Back:    channel(from, to, true, 400); return true;
  case Trans::Unlock:  sasquatchRun(from, to, 720); return true;
  case Trans::Lock:    groundPound(from, to, 760); return true;
  default:             return false;
  }
}

}  // namespace fx
