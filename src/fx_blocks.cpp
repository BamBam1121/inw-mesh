// Blocks: a world of 16 px blocks, and gravity.
//   forward  The old screen crumbles block by block and falls away; the new one's
//            blocks drop from the sky and stack up from the ground, each landing
//            with a bounce and a puff of grass and dirt.
//   back     The same with gravity reversed: old blocks fall up and away, the
//            previous screen's blocks rise from below into place.
//   unlock   Blast: the middle block's fuse blinks, then it goes off - a blocky
//            fireball cooling to smoke, a ring of pixels racing out, every block of
//            the lock screen thrown clear, the screen shaking - and home is left.
//   lock     The lock screen's blocks rain down column by column and stack, landing
//            with a heavy thud.
// Wake and sleep stay the tile build / break (fx.cpp).

#include "fx_internal.h"

namespace fx {
using namespace k;
namespace {

constexpr int NT = TCOLS * TROWS;
constexpr int16_t GONE = -32768;

// Where every block of the old and new screens is this frame (GONE: not drawn).
int16_t s_ax[NT], s_ay[NT], s_bx[NT], s_by[NT];

void drawTiles(const Strip& s, const uint16_t* src, const int16_t* xs, const int16_t* ys) {
  for (int i = 0; i < NT; i++) {
    const int y = ys[i];
    if (y == GONE || y >= s.y1 || y + TILE <= s.y0) continue;
    blitTile(s.out, src, i % TCOLS, i / TCOLS, xs[i], y);
  }
}

// ---- forward / back / lock: blocks fall and stack --------------------------------------
// up: gravity reversed (back). heavy: the lock screen building (bigger landings,
// columns in a sweep, a thud at the end).
void blockDrop(Canvas& from, Canvas& to, bool up, bool heavy, uint16_t ms) {
  const Theme& c = T();
  const uint16_t* A = bufOf(from);
  const uint16_t* B = bufOf(to);
  static bool landed[NT];
  memset(landed, 0, sizeof(landed));
  Mote bits[60] = {};
  const float g = up ? -1700.0f : 1700.0f;             // px/s^2, sign is the direction of fall
  const float fall = 0.17f;                             // share of the run a block spends falling in
  bool thud = false;
  uint32_t last = tnow(), frame = 0;
  for (uint32_t t0 = tnow();; frame++) {
    const uint32_t now = tnow();
    const float dt = (now - last) / 1000.0f;
    last = now;
    const float p = clamp01((now - t0) / (float)ms), ts = (now - t0) / 1000.0f;
    // The old blocks: each lets go at its own moment and falls away, drifting.
    for (int i = 0; i < NT; i++) {
      const float let = (heavy ? 0.0f : 0.02f) + hashf(i * 3 + 1) * (heavy ? 0.12f : 0.26f);
      const float lt = let * ms / 1000.0f;
      int x = (i % TCOLS) * TILE, y = (i / TCOLS) * TILE;
      if (ts > lt) {
        const float d = ts - lt;
        y += (int)(0.5f * g * d * d);
        x += (int)((hashf(i * 5 + 2) - 0.5f) * 140 * d);
      }
      s_ax[i] = (int16_t)x;
      s_ay[i] = (y > H || y < -TILE) ? GONE : (int16_t)y;
    }
    // The new blocks: fall in and stack, ground row first (or top row, going up).
    for (int i = 0; i < NT; i++) {
      const int tx = i % TCOLS, ty = i / TCOLS;
      const float row = up ? ty / (float)(TROWS - 1) : (TROWS - 1 - ty) / (float)(TROWS - 1);
      const float land = heavy ? 0.22f + tx / (float)(TCOLS - 1) * 0.38f + row * 0.22f + hashf(i * 7 + 3) * 0.06f
                               : 0.28f + row * 0.46f + hashf(i * 7 + 3) * 0.12f;
      s_bx[i] = (int16_t)(tx * TILE);
      if (p < land - fall) { s_by[i] = GONE; continue; }
      const int sx = tx * TILE, sy = ty * TILE;
      int y = sy;
      if (p < land) {
        const float u = (p - (land - fall)) / fall;
        const float drop = up ? (H - sy + 24) : (sy + 32);
        y = sy + (int)((up ? 1 : -1) * drop * (1 - u) * (1 - u));   // accelerating in
      } else {
        const float since = p - land;
        const int bump = heavy ? 5 : 3;
        const int b = since < 0.025f ? -bump : since < 0.05f ? bump / 2 : 0;   // squash, rebound
        y = sy + (up ? -b : b);
        if (!landed[i]) {
          landed[i] = true;
          if (p < 0.86f)                                   // late ones land clean: nothing left at the end
            for (int k2 = 0; k2 < (heavy ? 3 : 2); k2++) {
              const float r1 = hashf(i * 11 + k2), r2 = hashf(i * 13 + k2 + 7);
              const uint16_t col = k2 == 0 ? c.green : (r1 < 0.5f ? c.panel : c.line);   // grass and dirt
              spawnMote(bits, sx + 8 + (r1 - 0.5f) * 12, up ? sy + TILE : sy, (r1 - 0.5f) * 160,
                        (up ? 1 : -1) * (60 + r2 * 140), 0.16f + r2 * 0.14f, col, 3);
            }
        }
      }
      s_by[i] = (int16_t)y;
    }
    stepMotes(bits, dt, up ? -500 : 500);
    int shy = 0;
    if (heavy && p > 0.84f) {                              // the last column lands: thud
      if (!thud) { thud = true; buzzOnce(); }
      shy = (int)(5 * (1 - seg(p, 0.84f, 1)) * (frame % 2 ? 1 : -1));
    }
    frameStrips(0, H, [&](Strip& s) {
      s.g.fillRect(0, s.y0, W, s.y1 - s.y0, c.bg);
      drawTiles(s, A, s_ax, s_ay);
      drawTiles(s, B, s_bx, s_by);
      drawMotes(s.g, bits, true);
    }, 0, shy);
    if (p >= 1 || testStop(t0)) break;
  }
  if (!stopped()) pushFull(to);
}

// ---- unlock: blast ----------------------------------------------------------------------
// A puff of the fireball: a square that flashes white, burns amber and red, cools
// to smoke and shrinks away, drifting up as it slows.
struct Puff { float x, y, vx, vy, s0, life; };

void blast(Canvas& from, Canvas& to, uint16_t ms) {
  const Theme& c = T();
  const uint16_t* A = bufOf(from);
  const uint16_t* B = bufOf(to);
  const float fuse = 0.16f;
  const int ctx = TCOLS / 2, cty = TROWS / 2;               // the block that goes off
  const float bx = ctx * TILE + 8, by = cty * TILE + 8;
  Mote sparks[70] = {};
  Puff puffs[26];
  for (int i = 0; i < 26; i++) {
    const float ang = hashf(i * 17 + 3) * 6.2832f, sp = 30 + hashf(i * 19) * 150;
    puffs[i] = {bx + (hashf(i * 23) - 0.5f) * 16, by + (hashf(i * 29) - 0.5f) * 16, cosf(ang) * sp, sinf(ang) * sp * 0.7f - 20,
                10 + hashf(i * 31) * 16, 0.32f + hashf(i * 37) * 0.3f};
  }
  const uint16_t smoke = c.dim, soot = c.line;
  bool boom = false;
  uint32_t last = tnow(), frame = 0;
  for (uint32_t t0 = tnow();; frame++) {
    const uint32_t now = tnow();
    const float dt = (now - last) / 1000.0f;
    last = now;
    const float p = clamp01((now - t0) / (float)ms);
    const float ts = p > fuse ? (p - fuse) * ms / 1000.0f : 0;   // seconds since it went off
    int shx = 0, shy = 0;
    bool lit = false;
    if (p < fuse) {
      // The fuse: the middle block blinks white faster and faster, a tremble building.
      const float u = p / fuse;
      lit = fmodf(u * u * 9, 1.0f) < 0.5f;
      shx = (int)((hashf(frame) - 0.5f) * 4 * u);
    } else {
      if (!boom) {
        boom = true;
        buzzOnce();
        for (int i = 0; i < 60; i++) {
          const float ang = hashf(i * 3) * 6.2832f, sp = 180 + hashf(i * 5) * 520;
          spawnMote(sparks, bx, by, cosf(ang) * sp, sinf(ang) * sp - 120, 0.3f + hashf(i * 7) * 0.3f,
                    i % 3 == 0 ? TFT_WHITE : i % 3 == 1 ? c.amber : c.red, 2 + (uint8_t)(hashf(i) * 3));
        }
      }
      // Every block is thrown out from the blast, faster the closer it was.
      for (int i = 0; i < NT; i++) {
        const int tx = i % TCOLS, ty = i / TCOLS;
        const float ox = tx * TILE + 8 - bx, oy = ty * TILE + 8 - by;
        const float dist = sqrtf(ox * ox + oy * oy) + 1;
        const float kick = (620 + hashf(i * 9) * 380) * (1.2f - min(dist / 300.0f, 1.0f) * 0.6f);
        const float vx = ox / dist * kick, vy = oy / dist * kick - 160;
        const int x = tx * TILE + (int)(vx * ts), y = ty * TILE + (int)(vy * ts + 0.5f * 1300 * ts * ts);
        s_ax[i] = (int16_t)x;
        s_ay[i] = (x < -TILE || x > W || y < -TILE || y > H || (tx == ctx && ty == cty)) ? GONE : (int16_t)y;
      }
      stepMotes(sparks, dt, 700);
      const float sk = 10 * (1 - seg(p, fuse, fuse + 0.35f));
      shx = (int)(sk * (hashf(frame * 3) - 0.5f) * 2);
      shy = (int)(sk * (hashf(frame * 5 + 1) - 0.5f) * 2);
    }
    // The ring of pixels racing out, cooling from white to amber to red.
    const float rr = 24 + 420 * easeOutCubic(seg(p, fuse, 0.7f));
    const uint16_t ringC = rr < 150 ? TFT_WHITE : rr < 290 ? c.amber : c.red;
    frameStrips(0, H, [&](Strip& s) {
      if (p < fuse) {
        for (int y = s.y0; y < s.y1; y++) copyRow(s.out, y, A, y);
        if (lit) { s.g.fillRect(ctx * TILE, cty * TILE, TILE, TILE, TFT_WHITE); s.g.drawRect(ctx * TILE - 1, cty * TILE - 1, TILE + 2, TILE + 2, c.red); }
        return;
      }
      for (int y = s.y0; y < s.y1; y++) copyRow(s.out, y, B, y);   // what's left behind: home
      drawTiles(s, A, s_ax, s_ay);
      if (p < 0.72f && touches(s, (int)(by - rr - 4), (int)(by + rr + 4)))
        for (int i = 0; i < 48; i++) {
          const float a = i * 0.1309f;
          const int px = (int)(bx + cosf(a) * rr) & ~3, py = (int)(by + sinf(a) * rr * 0.9f) & ~3;
          s.g.fillRect(px, py, 4, 4, ringC);
        }
      // The fireball.
      for (auto& f : puffs) {
        const float a = ts / f.life;
        if (a >= 1) continue;
        const float drag = 1 - expf(-3 * ts);
        const float x = f.x + f.vx * drag / 3, y = f.y + f.vy * drag / 3 - 40 * ts * ts;
        const int sz = ((int)(f.s0 * (a < 0.3f ? 1 + a : 1.3f * (1 - (a - 0.3f) / 0.7f))) + 1) & ~1;
        if (sz < 2 || !touches(s, (int)y - sz / 2, (int)y + sz / 2)) continue;
        const uint16_t col = a < 0.14f ? TFT_WHITE : a < 0.3f ? c.amber : a < 0.42f ? c.red : a < 0.72f ? smoke : soot;
        s.g.fillRect(((int)x - sz / 2) & ~1, ((int)y - sz / 2) & ~1, sz, sz, col);
      }
      drawMotes(s.g, sparks, true);
    }, shx, shy);
    if (p >= 1 || testStop(t0)) break;
  }
  if (!stopped()) pushFull(to);
}

}  // namespace

bool blocksTransition(Trans kind, Canvas& from, Canvas& to) {
  switch (kind) {
  case Trans::Forward: blockDrop(from, to, false, false, 460); return true;
  case Trans::Back:    blockDrop(from, to, true, false, 460); return true;
  case Trans::Unlock:  blast(from, to, 820); return true;
  case Trans::Lock:    blockDrop(from, to, false, true, 620); return true;
  default:             return false;
  }
}

}  // namespace fx
