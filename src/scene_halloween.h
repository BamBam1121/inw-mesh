// The Halloween theme's lock scene: the sasquatch out trick-or-treating, in costume
// (mascot.h, spooky::costume) and somewhere different after every restart
// (spooky::place): down a street of lit houses, through a graveyard, across a pumpkin
// patch, or in the woods below a haunted house.
//
// Included by scenes.h inside namespace scenes, after its helpers (rgb, mix, hash,
// mascotLift, mascotPose). The same code draws the pager's 480 px band and the
// T-Deck's 320 px one: SpookyFrame says how wide, where the ground is and where he stands.

struct SpookyFrame {
  int W, ground, mascotX;
  bool narrow;                       // the T-Deck: the moon goes behind him, what he says to the right
  int skyX, skyY, farX, farY;        // how far the sky and the far layer slide (the pager's lean), 0 otherwise
};

inline int spookyWrap(float v, int span) {
  int x = (int)v % span;
  return x < 0 ? x + span : x;
}

// A pumpkin sitting on the ground at x, r px across each way; lit ones are carved and flicker.
inline void spookyPumpkin(lgfx::LovyanGFX& d, int x, int groundY, int r, bool lit, float phase) {
  const uint16_t pk = rgb(0xff8a1f), dk = rgb(0xb4560c);
  const int ry = max(3, r * 3 / 4), cy = groundY - ry;
  d.fillEllipse(x, cy, r, ry, pk);
  d.drawEllipse(x, cy, max(1, r / 2), ry, dk);
  d.fillRect(x - 1, cy - ry - 3, 2, 3, rgb(0x4e7a2a));
  if (!lit || r < 6) return;
  const uint16_t glow = mix(rgb(0xffe27a), pk, 0.2f + 0.2f * sinf(phase * 3.0f + x));
  d.fillTriangle(x - r / 2 - 1, cy - 1, x - r / 2 + 1, cy - 4, x - r / 2 + 3, cy - 1, glow);
  d.fillTriangle(x + r / 2 - 3, cy - 1, x + r / 2 - 1, cy - 4, x + r / 2 + 1, cy - 1, glow);
  d.drawFastHLine(x - r / 2, cy + 2, r, glow);
  d.drawFastHLine(x - r / 2 + 1, cy + 3, r - 2, glow);
}

inline void spookyBat(lgfx::LovyanGFX& d, int x, int y, float flap, uint16_t c) {
  const int tip = (int)(flap * 4);
  d.fillTriangle(x - 1, y, x - 8, y - 2 + tip, x - 4, y + 2, c);
  d.fillTriangle(x + 1, y, x + 8, y - 2 + tip, x + 4, y + 2, c);
  d.fillRect(x - 1, y - 1, 3, 3, c);
  d.drawPixel(x - 1, y - 2, c); d.drawPixel(x + 1, y - 2, c);      // ears
}

// ---- a street of houses with their lights on -------------------------------------------
inline void spookyStreet(lgfx::LovyanGFX& d, const Theme& t, const SpookyFrame& f, float phase, float scroll) {
  const int pitch = 132, n = f.W / pitch + 3, span = n * pitch, g = f.ground;
  const uint16_t wall = mix(t.bg, t.line, 0.8f), roof = mix(t.line, t.dim, 0.3f), lit = rgb(0xffb648), door = mix(t.bg, t.line, 0.35f);
  for (int i = 0; i < n; i++) {
    const int x = spookyWrap(i * pitch - scroll, span) - pitch;
    const uint32_t h = hash(i * 13 + 1);
    const int w = 76 + h % 22, hh = 42 + (h >> 5) % 20, top = g - hh;
    d.fillRect(x + w - 22, top - 20, 8, 20, wall);                              // chimney
    d.fillTriangle(x - 6, top, x + w / 2, top - 22 - (int)((h >> 9) % 10), x + w + 6, top, roof);
    d.fillRect(x, top, w, hh, wall);
    for (int k = 0; k < 2; k++) {                                               // windows: most are lit
      const int wx = k ? x + w - 24 : x + 11, wy = top + 9;
      const bool on = (h >> (12 + k)) & 3;
      d.fillRect(wx, wy, 13, 14, on ? lit : door);
      d.drawFastVLine(wx + 6, wy, 14, wall);
      d.drawFastHLine(wx, wy + 7, 13, wall);
    }
    d.fillRect(x + w / 2 - 6, g - 22, 12, 22, door);
    d.drawPixel(x + w / 2 + 3, g - 11, lit);
    spookyPumpkin(d, x + w / 2 + 15, g, 6, true, phase);                        // one on every step
    if ((h >> 16) & 1) spookyPumpkin(d, x + w / 2 - 16, g, 5, false, phase);
    // A street lamp between this house and the next.
    const int lx = x + w + (pitch - w) / 2;
    d.drawFastVLine(lx, g - 50, 50, t.line);
    d.drawFastHLine(lx, g - 50, 7, t.line);
    d.fillCircle(lx + 7, g - 47, 5, mix(t.bg, rgb(0xffd98a), 0.25f));
    d.fillCircle(lx + 7, g - 47, 2, rgb(0xffd98a));
  }
}

// ---- a graveyard ------------------------------------------------------------------------
inline void spookyGraveyard(lgfx::LovyanGFX& d, const Theme& t, const SpookyFrame& f, float phase, float scroll) {
  const int g = f.ground, W = f.W;
  // A far hill with a bare tree on it.
  const uint16_t hill = mix(t.bg, t.line, 0.75f);
  for (int x = 0; x < W; x += 2) {
    const int y = g - 34 + f.farY + (int)(sinf((x - f.farX + scroll * 0.2f) * 0.013f) * 10);
    d.fillRect(x, y, 2, g - y, hill);
  }
  const int tx = spookyWrap(W * 0.72f - scroll * 0.2f, W + 160) - 80 + f.farX, ty = g - 38 + f.farY;
  d.drawWideLine(tx, ty + 6, tx + 2, ty - 26, 2.2f, hill);
  d.drawLine(tx + 1, ty - 12, tx - 12, ty - 28, hill); d.drawLine(tx + 2, ty - 18, tx + 13, ty - 32, hill);
  d.drawLine(tx - 6, ty - 20, tx - 9, ty - 34, hill);  d.drawLine(tx + 8, ty - 25, tx + 7, ty - 38, hill);
  // A little ghost drifting among the stones.
  const int gx = W + 40 - spookyWrap(scroll * 0.45f, W + 80), gy = g - 62 + (int)(sinf(phase * 0.3f) * 6);
  const uint16_t pale = mix(t.bg, 0xFFFF, 0.5f);
  d.fillCircle(gx, gy, 7, pale);
  d.fillRect(gx - 7, gy, 15, 10, pale);
  for (int k = 0; k < 3; k++) d.fillTriangle(gx - 7 + k * 5, gy + 10, gx - 2 + k * 5, gy + 10, gx - 5 + k * 5, gy + 14, pale);
  d.fillRect(gx - 4, gy - 2, 2, 3, t.bg); d.fillRect(gx + 1, gy - 2, 2, 3, t.bg);
  // The iron fence along the back.
  const int off = spookyWrap(scroll, 10);
  d.drawFastHLine(0, g - 15, W, t.line);
  for (int x = -off; x < W; x += 10) { d.drawFastVLine(x, g - 20, 20, t.line); d.drawPixel(x, g - 21, t.line); }
  // The stones: round-topped, crosses and leaning slabs.
  const int pitch = 58, n = W / pitch + 3, span = n * pitch;
  const uint16_t stone = rgb(0x6c6486), cut = rgb(0x3a3452);
  for (int i = 0; i < n; i++) {
    const int x = spookyWrap(i * pitch - scroll, span) - pitch;
    const uint32_t h = hash(i * 7 + 3);
    const int hh = 22 + h % 10;
    switch ((h >> 6) % 3) {
      case 0:
        d.fillRoundRect(x, g - hh, 16, hh + 6, 7, stone);
        d.fillRect(x, g, 16, 6, t.bg);
        d.drawFastHLine(x + 4, g - hh + 8, 8, cut); d.drawFastHLine(x + 5, g - hh + 11, 6, cut);
        break;
      case 1:
        d.fillRect(x + 6, g - hh - 4, 5, hh + 4, stone);
        d.fillRect(x, g - hh + 3, 17, 5, stone);
        break;
      default:
        d.fillTriangle(x, g, x + 3, g - hh, x + 14, g, stone);
        d.fillTriangle(x + 3, g - hh, x + 17, g - hh + 2, x + 14, g, stone);
        d.drawLine(x + 6, g - hh + 7, x + 12, g - hh + 8, cut);
        break;
    }
  }
  // Ground mist, in drifting strands.
  const uint16_t mist = mix(t.bg, t.dim, 0.4f);
  for (int k = 0; k < 7; k++) {
    const int mx = spookyWrap(k * 83 - scroll * 0.7f + sinf(phase * 0.1f + k) * 12, W + 90) - 60;
    d.drawFastHLine(mx, g - 3 - (k % 3) * 3, 38 + (k * 13) % 30, mist);
  }
}

// ---- a pumpkin patch --------------------------------------------------------------------
inline void spookyPatch(lgfx::LovyanGFX& d, const Theme& t, const SpookyFrame& f, float phase, float scroll) {
  const int g = f.ground, W = f.W;
  // The corn behind, a little further off.
  const uint16_t corn = rgb(0x4b4a1f), leaf = rgb(0x676428);
  const int off = spookyWrap(scroll * 0.6f, 9), first = (int)floorf(scroll * 0.6f / 9);
  for (int k = -1; k <= W / 9 + 1; k++) {
    const int x = k * 9 - off + f.farX;
    const uint32_t h = hash(first + k);
    const int hh = 34 + h % 16;
    d.drawFastVLine(x, g - hh, hh, corn);
    d.drawLine(x, g - hh + 8, x + 4, g - hh + 3, leaf);
    d.drawLine(x, g - hh + 17, x - 4, g - hh + 12, leaf);
    if (h & 16) d.fillRect(x - 1, g - hh + 20, 3, 6, rgb(0x8a7a2a));          // an ear
  }
  // The scarecrow comes round now and then.
  const int sx = spookyWrap(W * 0.8f - scroll, W + 260) - 60;
  const uint16_t pole = rgb(0x6b4a2b), sack = rgb(0xcdb98a), shirt = rgb(0x9a3f1c);
  d.fillRect(sx - 1, g - 62, 3, 62, pole);
  d.fillRect(sx - 19, g - 47, 39, 3, pole);
  d.fillTriangle(sx - 17, g - 48, sx + 17, g - 48, sx, g - 22, shirt);
  d.fillRect(sx - 17, g - 49, 34, 7, shirt);
  for (int k = 0; k < 3; k++) { d.drawLine(sx - 19, g - 46, sx - 23, g - 42 + k * 2, sack); d.drawLine(sx + 19, g - 46, sx + 23, g - 42 + k * 2, sack); }
  d.fillCircle(sx, g - 57, 7, sack);
  d.fillTriangle(sx - 11, g - 62, sx + 11, g - 62, sx, g - 78, pole);            // his hat
  d.drawFastHLine(sx - 13, g - 62, 27, pole);
  d.fillRect(sx - 3, g - 58, 2, 2, t.bg); d.fillRect(sx + 2, g - 58, 2, 2, t.bg);
  d.drawFastHLine(sx - 3, g - 54, 7, t.bg);
  // The pumpkins, all sizes, on their vine; every third is carved and lit.
  const int pitch = 46, n = W / pitch + 3, span = n * pitch;
  const uint16_t vine = rgb(0x4e7a2a);
  for (int i = 0; i < n; i++) {
    const int x = spookyWrap(i * pitch - scroll, span) - pitch;
    const uint32_t h = hash(i * 11 + 5);
    const int r = 6 + h % 7;
    d.drawLine(x + r, g - 2, x + pitch - 8, g - 4 - (int)((h >> 4) % 4), vine);
    d.drawLine(x + pitch - 8, g - 4 - (int)((h >> 4) % 4), x + pitch - 4, g - 9, vine);
    spookyPumpkin(d, x, g, r, i % 3 == 0, phase);
  }
}

// ---- the woods, with a haunted house up the hill ----------------------------------------
inline void spookyWoods(lgfx::LovyanGFX& d, const Theme& t, const SpookyFrame& f, float phase, float scroll) {
  const int g = f.ground, W = f.W;
  const uint16_t hill = mix(t.bg, t.line, 0.5f), lit = rgb(0xffd24a);
  auto hillY = [&](int x) { return g - 30 + f.farY + (int)(sinf((x - f.farX + scroll * 0.2f) * 0.011f) * 12); };
  for (int x = 0; x < W; x += 2) { const int y = hillY(x); d.fillRect(x, y, 2, g - y, hill); }
  // The house: tall, crooked, a light in the tower.
  const int hx = spookyWrap(W * 0.74f - scroll * 0.2f, W + 220) - 110 + f.farX, hy = hillY(hx + 20);
  const uint16_t house = mix(t.bg, t.line, 0.95f);
  d.fillRect(hx, hy - 34, 40, 38, house);
  d.fillTriangle(hx - 5, hy - 34, hx + 20, hy - 52, hx + 45, hy - 34, house);
  d.fillRect(hx + 26, hy - 62, 13, 30, house);                                   // the tower
  d.fillTriangle(hx + 23, hy - 62, hx + 34, hy - 80, hx + 42, hy - 62, house);
  d.fillRect(hx + 30, hy - 56, 5, 7, lit);
  d.fillRect(hx + 6, hy - 26, 6, 8, lit);
  d.fillRect(hx + 17, hy - 12, 7, 12, t.bg);
  if (((int)(phase * 0.15f)) % 5 != 0) d.fillRect(hx + 28, hy - 26, 6, 8, lit);   // one flickers out now and then
  // Eyes in the dark between the trees, blinking.
  for (int i = 0; i < 5; i++) {
    const int ex = spookyWrap(i * 97 + 40 - scroll * 0.8f, W + 100) - 50;
    const int ey = g - 18 - (int)(hash(i * 5 + 2) % 30);
    if (((int)(phase * 0.2f) + i * 3) % 7 == 0) continue;
    const uint16_t c = i % 2 ? t.amber : t.green;
    d.fillRect(ex, ey, 2, 2, c); d.fillRect(ex + 5, ey, 2, 2, c);
  }
  // Bare trees, leaning this way and that. None right where he walks.
  const int pitch = 64, n = W / pitch + 3, span = n * pitch;
  const uint16_t bark = mix(t.line, t.dim, 0.18f);
  for (int i = 0; i < n; i++) {
    const int x = spookyWrap(i * pitch - scroll, span) - pitch;
    if (x > f.mascotX - 46 && x < f.mascotX + 40) continue;
    const uint32_t h = hash(i * 17 + 9);
    const int lean = (int)(h % 13) - 6, hh = 70 + (h >> 4) % 26;
    const int tx = x + lean, ty = g - hh;
    d.drawWideLine(x, g, x + lean / 2, g - hh / 2, 3.2f, bark);
    d.drawWideLine(x + lean / 2, g - hh / 2, tx, ty, 2.0f, bark);
    const int by = g - hh / 2;
    d.drawWideLine(x + lean / 2, by, x + lean / 2 - 16, by - 22, 1.4f, bark);
    d.drawLine(x + lean / 2 - 9, by - 12, x + lean / 2 - 20, by - 12, bark);
    d.drawWideLine(x + lean / 2 + 1, by - 10, x + lean / 2 + 17, by - 30, 1.4f, bark);
    d.drawLine(x + lean / 2 + 10, by - 21, x + lean / 2 + 20, by - 20, bark);
    d.drawLine(tx, ty, tx - 8, ty - 12, bark); d.drawLine(tx, ty, tx + 7, ty - 14, bark);
  }
}

inline void halloween(lgfx::LovyanGFX& d, const Theme& t, const SpookyFrame& f, float phase, float scroll, bool unread) {
  const int g = f.ground, W = f.W;
  // The sky: stars, a big low moon, thin cloud across it, bats.
  const int count = 46 * W / 480;
  for (int i = 0; i < count; i++) {
    const uint32_t h = hash(i + 7);
    const int x = spookyWrap((int)(h % W) + f.skyX, W), y = 20 + (int)((h >> 9) % (g - 80)) + f.skyY;
    if (y < 19) continue;
    if (((int)(phase * 0.2f) + i) % 7 != 0) d.drawPixel(x, y, (i % 5 == 0) ? mix(t.dim, 0xFFFF, 0.6f) : t.dim);
  }
  const int mx = (f.narrow ? 50 : W - 86) + f.skyX * 3 / 4, my = (f.narrow ? 54 : 52) + f.skyY * 3 / 4;
  const uint16_t moon = rgb(0xf3e3a6), crater = rgb(0xd9c582);
  d.fillCircle(mx, my, 21, mix(t.bg, moon, 0.12f));
  d.fillCircle(mx, my, 17, moon);
  d.fillCircle(mx - 6, my - 4, 4, crater); d.fillCircle(mx + 5, my + 6, 3, crater); d.fillCircle(mx + 6, my - 7, 2, crater);
  const uint16_t cloud = mix(t.bg, t.dim, 0.5f);
  const int cx = W + 60 - spookyWrap(scroll * 0.15f, W + 160);
  d.fillRoundRect(cx - 50, my + 5, 70, 3, 1, cloud);
  d.fillRoundRect(cx - 30, my + 10, 80, 2, 1, cloud);
  const uint16_t bat = mix(t.bg, t.blue, 0.55f);
  for (int i = 0; i < 3; i++) {
    const int bx = W + 20 - spookyWrap(i * (W / 3 + 37) + scroll * 1.5f, W + 40);
    const int by = 34 + i * 15 + (int)(sinf(phase * 0.7f + i * 2.1f) * 5) + f.skyY / 2;
    spookyBat(d, bx, by, sinf(phase * 2.5f + i), bat);
  }
  switch (spooky::place()) {
    case spooky::GRAVEYARD: spookyGraveyard(d, t, f, phase, scroll); break;
    case spooky::PATCH:     spookyPatch(d, t, f, phase, scroll); break;
    case spooky::WOODS:     spookyWoods(d, t, f, phase, scroll); break;
    default:                spookyStreet(d, t, f, phase, scroll); break;
  }
  d.drawFastHLine(0, g, W, t.greenDim);
  drawSasquatch(d, t, f.mascotX, g - mascotLift(), 88, phase, unread ? t.amber : t.green, mascotPose());
}
