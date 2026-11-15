// The Squatchmas theme's lock scene: the sasquatch out in the snow in one of his holiday
// outfits (mascot.h, jolly::outfit) and somewhere different every time the screen comes
// on (jolly::place): past a log cabin with its lights on, through a Christmas tree farm,
// along a frozen lake, or under a repeater tower strung with lights like a tree.
//
// Included by scenes.h inside namespace scenes, after scene_halloween.h (it borrows its
// SpookyFrame and spookyWrap). The same code draws the pager's 480 px band and the
// T-Deck's 320 px one.

namespace jollyc {
constexpr uint32_t SNOW = 0xe8f0f4, PINE = 0x16482c, WARM = 0xffc56a, WOOD = 0x6b4426, WOOD_DARK = 0x4a2e19;
constexpr uint32_t BULB[4] = {0xff4a4a, 0x5cff7a, 0xffd34d, 0x5cc8ff};
}

// A snowy pine, h px tall, standing on the ground at x.
inline void jollyPine(lgfx::LovyanGFX& d, int x, int g, int h, uint16_t tree, uint16_t snow) {
  const uint16_t trunk = mix(tree, rgb(jollyc::WOOD), 0.6f);
  d.fillRect(x - max(1, h / 24), g - h / 7, max(2, h / 12), h / 7, trunk);
  for (int k = 0; k < 3; k++) {
    const int ay = g - h + k * h * 23 / 100, by = ay + h * 44 / 100, hw = h * (17 + 9 * k) / 100;
    d.fillTriangle(x, ay, x - hw, by, x + hw, by, tree);
    d.drawLine(x - hw, by, x + hw, by, snow);                       // snow along each tier
    d.drawLine(x - hw + 2, by - 1, x - hw / 3, by - 1, snow);
    if (k == 0) d.fillTriangle(x, ay, x - hw / 3, ay + h / 9, x + hw / 3, ay + h / 9, snow);
  }
}

// A string of lights hanging from (x0, y0) to (x1, y1), sagging `sag` px in the middle,
// with n bulbs that twinkle.
inline void jollyLights(lgfx::LovyanGFX& d, int x0, int y0, int x1, int y1, int sag, int n, float phase, uint32_t seed, uint16_t wire) {
  auto at = [&](float f, int& x, int& y) { x = x0 + (int)((x1 - x0) * f); y = y0 + (int)((y1 - y0) * f + sag * 4 * f * (1 - f)); };
  int px, py; at(0, px, py);
  for (int i = 1; i <= 12; i++) { int qx, qy; at(i / 12.0f, qx, qy); d.drawLine(px, py, qx, qy, wire); px = qx; py = qy; }
  for (int i = 0; i < n; i++) {
    int bx, by; at((i + 0.5f) / n, bx, by);
    const uint16_t c = rgb(jollyc::BULB[(i + seed) % 4]);
    const bool on = ((int)(phase * 0.5f) + (int)(hash(i * 7 + seed) % 6)) % 6 != 0;   // most are on; one now and then blinks
    d.fillRect(bx - 1, by + 1, 3, 3, on ? c : mix(c, rgb(0x101010), 0.65f));
    if (on) d.drawPixel(bx, by + 1, mix(c, 0xFFFF, 0.55f));
  }
}

// Falling snow: count flakes, `speed` px per unit of phase, drifting a little sideways.
inline void jollySnow(lgfx::LovyanGFX& d, const SpookyFrame& f, float phase, int count, uint32_t seed, float speed, int size, uint16_t c) {
  const int bottom = f.ground + 4;
  for (int i = 0; i < count; i++) {
    const uint32_t h = hash(i * 31 + seed);
    const float fall = phase * speed * (0.75f + (h & 255) / 600.0f);
    const int y = spookyWrap((int)((h >> 8) % 997) + fall, bottom - 14) + 14;
    const int x = spookyWrap((int)(h % f.W) + (int)(sinf(phase * 0.25f + i * 1.7f) * 5) - (int)(phase * speed * 0.2f), f.W);
    if (size <= 1) d.drawPixel(x, y, c);
    else d.fillRect(x, y, size, size, c);
  }
}

// ---- a log cabin with its lights on --------------------------------------------------------
inline void jollyCabin(lgfx::LovyanGFX& d, const Theme& t, const SpookyFrame& f, float phase, float scroll) {
  using namespace jollyc;
  const int g = f.ground, W = f.W;
  const uint16_t snow = rgb(SNOW), hill = mix(t.bg, snow, 0.15f), farPine = mix(t.bg, rgb(PINE), 0.7f), farSnow = mix(t.bg, snow, 0.32f);
  for (int x = 0; x < W; x += 2) {
    const float xs = x - f.farX + scroll * 0.15f;
    const int y = g - 42 + f.farY + (int)(sinf(xs * 0.012f) * 9 + sinf(xs * 0.031f) * 4);
    d.fillRect(x, y, 2, g - y, hill);
  }
  { // a far row of small pines
    const int pitch = 26, n = W / pitch + 3, span = n * pitch;
    for (int i = 0; i < n; i++) {
      const int x = spookyWrap(i * pitch - scroll * 0.3f, span) - pitch + f.farX;
      const uint32_t h = hash(i * 5 + 11);
      jollyPine(d, x, g - 10 + f.farY, 16 + h % 12, farPine, farSnow);
    }
  }
  // The cabin, far enough off to pass slowly.
  const int cx = spookyWrap(W * 0.6f - scroll * 0.22f, W + 130) - 70 + f.farX, cy = g - 6 + f.farY;
  const uint16_t wall = rgb(WOOD), log = rgb(WOOD_DARK), roof = rgb(0x33241b), warm = rgb(WARM), stone = rgb(0x5b5d68);
  const int smx = cx + 52;
  for (int k = 0; k < 6; k++) {                                    // smoke curling up out of the chimney
    const float age = fmodf(phase * 0.035f + k / 6.0f, 1.0f);
    const int sx = smx + (int)(age * 22 + sinf(phase * 0.2f + k) * 3), sy = cy - 58 - (int)(age * 46);
    d.fillCircle(sx, sy, 2 + (int)(age * 5), mix(t.bg, rgb(0x9aa3ad), 0.4f * (1 - age)));
  }
  d.fillRect(cx + 47, cy - 58, 10, 18, stone);
  d.fillRect(cx + 46, cy - 60, 12, 3, snow);
  d.fillTriangle(cx - 10, cy - 30, cx + 32, cy - 56, cx + 74, cy - 30, snow);
  d.fillTriangle(cx - 4, cy - 30, cx + 32, cy - 51, cx + 68, cy - 30, roof);
  d.fillRect(cx, cy - 30, 64, 30, wall);
  for (int y = cy - 26; y < cy; y += 5) d.drawFastHLine(cx, y, 64, log);
  for (int k = 0; k < 5; k++) d.drawFastVLine(cx + 4 + k * 13, cy - 30, 2 + (int)(hash(k + 3) % 4), snow);   // icicles
  for (int k = 0; k < 2; k++) {
    const int wx = k ? cx + 44 : cx + 8, wy = cy - 23;
    d.fillRect(wx - 2, wy - 2, 16, 15, mix(wall, warm, 0.3f));
    d.fillRect(wx, wy, 12, 11, warm);
    d.drawFastVLine(wx + 6, wy, 11, log); d.drawFastHLine(wx, wy + 5, 12, log);
    d.fillRect(wx - 1, wy + 11, 14, 2, snow);                      // snow on the sill
  }
  d.fillRect(cx + 26, cy - 24, 12, 24, log);
  d.drawCircle(cx + 32, cy - 15, 4, rgb(0x2f9a4c)); d.drawCircle(cx + 32, cy - 15, 3, rgb(0x2f9a4c));
  d.fillRect(cx + 31, cy - 12, 3, 2, rgb(0xd62f2f));
  jollyLights(d, cx - 7, cy - 31, cx + 71, cy - 31, 4, 13, phase, 1, rgb(0x1a2a20));
  // Snowy pines close by, none right where he walks.
  const int pitch = 92, n = W / pitch + 3, span = n * pitch;
  for (int i = 0; i < n; i++) {
    const int x = spookyWrap(i * pitch - scroll, span) - pitch;
    if (x > f.mascotX - 50 && x < f.mascotX + 44) continue;
    const uint32_t h = hash(i * 17 + 9);
    jollyPine(d, x, g, 52 + h % 30, rgb(PINE), snow);
  }
}

// ---- a Christmas tree farm ------------------------------------------------------------------
inline void jollyTreeFarm(lgfx::LovyanGFX& d, const Theme& t, const SpookyFrame& f, float phase, float scroll) {
  using namespace jollyc;
  const int g = f.ground, W = f.W;
  const uint16_t snow = rgb(SNOW), hill = mix(t.bg, snow, 0.13f), backPine = mix(t.bg, rgb(PINE), 0.8f), backSnow = mix(t.bg, snow, 0.45f);
  for (int x = 0; x < W; x += 2) {
    const int y = g - 30 + f.farY + (int)(sinf((x - f.farX + scroll * 0.2f) * 0.009f) * 6);
    d.fillRect(x, y, 2, g - y, hill);
  }
  { // the rows further back: young trees in lines
    const int pitch = 30, n = W / pitch + 3, span = n * pitch;
    for (int i = 0; i < n; i++) {
      const int x = spookyWrap(i * pitch - scroll * 0.5f, span) - pitch + f.farX;
      jollyPine(d, x, g - 14 + f.farY, 24 + hash(i * 3 + 1) % 10, backPine, backSnow);
    }
  }
  { // posts with a string of lights between them
    const int pitch = 118, n = W / pitch + 3, span = n * pitch;
    const uint16_t post = rgb(0x5a3d24);
    for (int i = 0; i < n; i++) {
      const int x = spookyWrap(i * pitch - scroll * 0.75f, span) - pitch;
      d.fillRect(x - 1, g - 44, 3, 40, post);
      d.fillRect(x - 2, g - 46, 5, 2, snow);
      jollyLights(d, x, g - 44, x + pitch, g - 44, 10, 9, phase, i * 3, rgb(0x1a2a20));
      d.fillCircle(x, g - 49, 3, mix(t.bg, rgb(WARM), 0.35f));      // a lantern on top
      d.fillRect(x - 1, g - 50, 3, 3, rgb(WARM));
    }
  }
  // Now and then a red pickup goes by with a tree tied on the roof.
  const int tx = spookyWrap(W * 0.9f - scroll * 1.25f, W + 520) - 120, ty = g - 4;
  const uint16_t truck = rgb(0xb3262b), truckDark = rgb(0x7a1a1d), wheel = rgb(0x1b1b20);
  d.fillRect(tx, ty - 16, 58, 11, truck);
  d.fillRect(tx + 30, ty - 27, 22, 12, truck);
  d.fillRect(tx + 34, ty - 24, 15, 7, mix(t.bg, rgb(0x9ad7ff), 0.4f));
  d.fillRect(tx, ty - 16, 30, 2, truckDark);
  d.fillCircle(tx + 12, ty - 4, 5, wheel); d.fillCircle(tx + 46, ty - 4, 5, wheel);
  d.fillCircle(tx + 12, ty - 4, 2, rgb(0x777777)); d.fillCircle(tx + 46, ty - 4, 2, rgb(0x777777));
  d.fillRect(tx + 57, ty - 13, 2, 3, rgb(WARM));
  d.fillTriangle(tx + 22, ty - 31, tx + 60, ty - 29, tx + 22, ty - 25, rgb(PINE));           // the tree on the roof
  d.fillTriangle(tx + 22, ty - 31, tx + 22, ty - 25, tx + 14, ty - 28, rgb(0x5a3d24));
  d.drawLine(tx + 34, ty - 32, tx + 34, ty - 26, rgb(0xd8c8a0)); d.drawLine(tx + 46, ty - 31, tx + 46, ty - 27, rgb(0xd8c8a0));
  // The front row, for sale: some still bare, some netted, stumps where some were.
  const int pitch = 64, n = W / pitch + 3, span = n * pitch;
  for (int i = 0; i < n; i++) {
    const int x = spookyWrap(i * pitch - scroll, span) - pitch;
    if (x > f.mascotX - 46 && x < f.mascotX + 40) continue;
    const uint32_t h = hash(i * 13 + 7);
    if (h % 5 == 0) {                                                          // a stump
      d.fillRect(x - 4, g - 5, 9, 5, rgb(0x6b4a2b));
      d.fillRect(x - 5, g - 7, 11, 2, snow);
      continue;
    }
    const int th = 38 + h % 26;
    jollyPine(d, x, g, th, rgb(PINE), snow);
    if ((h >> 4) % 3 == 0)                                                     // netted, ready to go
      for (int k = 0; k < 4; k++) d.drawLine(x - th / 5, g - th / 6 - k * th / 6, x + th / 5, g - th / 4 - k * th / 6, rgb(0xd8c8a0));
    if ((h >> 7) & 1) d.fillRect(x + th / 6, g - th / 3, 4, 5, rgb(0xfff6ea));  // a price tag
  }
}

// ---- along a frozen lake --------------------------------------------------------------------
inline void jollyLake(lgfx::LovyanGFX& d, const Theme& t, const SpookyFrame& f, float phase, float scroll) {
  using namespace jollyc;
  const int g = f.ground, W = f.W;
  const uint16_t snow = rgb(SNOW);
  // Northern lights over the far mountains.
  for (int x = 0; x < W; x += 2) {                                 // curtains: bright along the bottom edge, fading up
    const float w = sinf(x * 0.013f + phase * 0.03f) + 0.5f * sinf(x * 0.037f - phase * 0.02f);
    const int y = 44 + (int)(w * 9) + f.skyY / 2;
    const float k = 0.35f + 0.65f * (0.5f + 0.5f * sinf(x * 0.008f + phase * 0.012f));
    for (int j = 0; j < 7; j++)
      d.drawFastVLine(x, y - (j + 1) * 4, 4, mix(t.bg, j < 3 ? rgb(0x5cffb0) : rgb(0x8a7cff), (0.5f - j * 0.065f) * k));
    d.drawFastVLine(x, y, 2, mix(t.bg, rgb(0xb8ffd8), 0.55f * k));
  }
  // The mountains across the lake, snow on top.
  const uint16_t mtn = mix(t.bg, rgb(0x6f86a6), 0.32f), cap = mix(t.bg, snow, 0.55f);
  for (int i = 0; i < 6; i++) {
    const int mx = spookyWrap(i * 120 - scroll * 0.06f, 720) - 120 + f.farX, mh = 46 + hash(i + 40) % 26, base = g - 30 + f.farY;
    d.fillTriangle(mx - 70, base, mx, base - mh, mx + 70, base, mtn);
    d.fillTriangle(mx - 14, base - mh + 12, mx, base - mh, mx + 14, base - mh + 12, cap);
  }
  // The far shore: dark pines and a few cabins with their lights on, shining across the ice.
  const int shore = g - 30 + f.farY;
  const uint16_t ice = mix(t.bg, rgb(0x9fc4dc), 0.2f), iceHi = mix(t.bg, rgb(0xcfe6f3), 0.42f), farPine = mix(t.bg, rgb(PINE), 0.85f);
  d.fillRect(0, shore, W, g - shore, ice);
  for (int x = -8; x < W + 8; x += 7) {
    const int xs = x - spookyWrap(scroll * 0.2f, 7);
    const int h = 6 + (int)(hash((int)((x + scroll * 0.2f) / 7) + 90) % 7);
    d.fillTriangle(xs, shore - h, xs - 4, shore, xs + 4, shore, farPine);
  }
  for (int i = 0; i < 4; i++) {
    const int hx = spookyWrap(i * 150 + 60 - scroll * 0.2f, W + 150) - 75;
    d.fillRect(hx, shore - 6, 9, 6, mix(t.bg, rgb(WOOD), 0.6f));
    d.fillRect(hx + 3, shore - 4, 3, 3, rgb(WARM));
    for (int y = shore + 2; y < g - 2; y += 3) d.drawPixel(hx + 4 + (int)(sinf(y * 0.7f + phase * 0.2f) * 1.5f), y, mix(ice, rgb(WARM), 0.5f * (1 - (float)(y - shore) / (g - shore))));
  }
  for (int k = 0; k < 9; k++) {                                                // glints and skate marks on the ice
    const int lx = spookyWrap(k * 67 - scroll * 0.6f, W + 60) - 30, ly = shore + 5 + (k * 7) % max(4, g - shore - 8);
    d.drawFastHLine(lx, ly, 10 + (k * 5) % 16, iceHi);
  }
  // An ice-fishing hut out on the lake.
  const int fx = spookyWrap(W * 0.2f - scroll * 0.6f, W + 300) - 60, fy = shore + 12;
  d.fillRect(fx, fy - 16, 20, 16, rgb(0x9c2f2a));
  d.fillTriangle(fx - 2, fy - 16, fx + 10, fy - 23, fx + 22, fy - 16, snow);
  d.fillRect(fx + 7, fy - 9, 6, 9, rgb(0x5a1a17));
  d.fillRect(fx + 15, fy - 13, 3, 3, rgb(WARM));
  // The near shore he walks along: snowbanks and a few pines.
  const int pitch = 120, n = W / pitch + 3, span = n * pitch;
  for (int i = 0; i < n; i++) {
    const int x = spookyWrap(i * pitch - scroll, span) - pitch;
    const uint32_t h = hash(i * 19 + 3);
    d.fillEllipse(x + 30, g - 2, 26 + h % 12, 6, mix(t.bg, snow, 0.75f));
    if (x > f.mascotX - 50 && x < f.mascotX + 44) continue;
    if (h & 1) jollyPine(d, x, g, 44 + h % 22, rgb(PINE), snow);
  }
}

// ---- under a repeater tower strung with lights -------------------------------------------------
inline void jollyTower(lgfx::LovyanGFX& d, const Theme& t, const SpookyFrame& f, float phase, float scroll) {
  using namespace jollyc;
  const int g = f.ground, W = f.W;
  const uint16_t snow = rgb(SNOW), steel = mix(t.bg, rgb(0xaab4c0), 0.55f);
  // The tower, a way off: a lattice mast, its lights strung out from the top like a tree.
  const int tx = spookyWrap(W * 0.7f - scroll * 0.12f, W + 100) - 50 + f.farX, base = g - 8 + f.farY;
  const int top = max(26, base - 124);
  for (int k = 0; k < 3; k++) {                                     // its signal going out, ring after ring
    const float r = fmodf(phase * 1.1f + k * 14, 42);
    d.drawCircle(tx, top - 6, (int)r + 4, mix(t.bg, t.green, 0.5f * (1 - r / 42)));
  }
  const int spread = 15;
  d.drawLine(tx - spread, base, tx - 2, top, steel);
  d.drawLine(tx + spread, base, tx + 2, top, steel);
  for (int y = base, k = 0; y > top + 6; y -= 11, k++) {           // the bracing
    const float f0 = (float)(base - y) / (base - top), f1 = (float)(base - y + 11) / (base - top);
    const int w0 = (int)(spread - (spread - 2) * f0), w1 = (int)(spread - (spread - 2) * min(1.0f, f1));
    d.drawLine(tx - w0, y, tx + w1, max(top, y - 11), steel);
    d.drawLine(tx + w0, y, tx - w1, max(top, y - 11), steel);
    d.drawFastHLine(tx - w0, y, 2 * w0, steel);
  }
  d.drawFastVLine(tx, top - 14, 14, steel);                        // the antenna on top
  for (int s = 0; s < 7; s++) {                                     // the lights, top to the ground round it
    const int ex = tx - 46 + s * 92 / 6;
    for (int b = 1; b <= 9; b++) {
      const float ff = b / 9.0f;
      const int bx = tx + (int)((ex - tx) * ff), by = top + (int)((base - 2 - top) * ff);
      const uint16_t c = rgb(BULB[(s + b) % 4]);
      const bool on = ((int)(phase * 0.6f) + s * 3 + b) % 7 != 0;
      d.fillRect(bx - 1, by - 1, 2, 2, on ? c : mix(c, t.bg, 0.6f));
    }
  }
  const bool bright = ((int)(phase * 0.5f)) % 2 == 0;               // the star on top
  d.fillCircle(tx, top - 15, bright ? 6 : 4, mix(t.bg, rgb(0xffd34d), 0.3f));
  mascot::star(d, tx, top - 15, bright ? 5 : 4, rgb(0xffe48a));
  // A little town at its foot, roofs under snow, windows lit, lights along the eaves.
  const int pitch = 84, n = W / pitch + 3, span = n * pitch;
  for (int i = 0; i < n; i++) {
    const int x = spookyWrap(i * pitch - scroll * 0.45f, span) - pitch;
    const uint32_t h = hash(i * 23 + 5);
    const int w = 46 + h % 18, hh = 24 + (h >> 5) % 12, ty = g - 4 - hh;
    const uint16_t wall = mix(t.bg, t.line, 0.9f);
    d.fillTriangle(x - 5, ty, x + w / 2, ty - 15, x + w + 5, ty, snow);
    d.fillTriangle(x - 1, ty, x + w / 2, ty - 12, x + w + 1, ty, mix(t.line, t.dim, 0.3f));
    d.fillRect(x, ty, w, hh, wall);
    for (int k = 0; k < 2; k++) {
      const bool on = (h >> (10 + k)) & 3;
      d.fillRect(k ? x + w - 13 : x + 5, ty + 6, 8, 8, on ? rgb(WARM) : mix(t.bg, t.line, 0.4f));
    }
    if ((h >> 14) & 1) jollyLights(d, x - 3, ty - 1, x + w + 3, ty - 1, 3, 8, phase, i, rgb(0x1a2a20));
  }
  // Pines close by.
  const int pp = 150, pn = W / pp + 3, ps = pn * pp;
  for (int i = 0; i < pn; i++) {
    const int x = spookyWrap(i * pp + 40 - scroll, ps) - pp;
    if (x > f.mascotX - 50 && x < f.mascotX + 44) continue;
    jollyPine(d, x, g, 48 + hash(i * 29 + 1) % 24, rgb(PINE), snow);
  }
}

inline void squatchmas(lgfx::LovyanGFX& d, const Theme& t, const SpookyFrame& f, float phase, float scroll, bool unread) {
  const int g = f.ground, W = f.W;
  const uint16_t snow = rgb(jollyc::SNOW);
  // The sky: a few stars, a pale moon, the far snow falling.
  const int count = 30 * W / 480;
  for (int i = 0; i < count; i++) {
    const uint32_t h = hash(i + 7);
    const int x = spookyWrap((int)(h % W) + f.skyX, W), y = 20 + (int)((h >> 9) % (g - 90)) + f.skyY;
    if (y < 19) continue;
    if (((int)(phase * 0.2f) + i) % 6 != 0) d.drawPixel(x, y, (i % 4 == 0) ? mix(t.dim, 0xFFFF, 0.6f) : t.dim);
  }
  if (jolly::place() != jolly::LAKE) {
    const int mx = (f.narrow ? 46 : 64) + f.skyX * 3 / 4, my = 46 + f.skyY * 3 / 4;
    d.fillCircle(mx, my, 17, mix(t.bg, rgb(0xeef3ff), 0.1f));
    d.fillCircle(mx, my, 13, rgb(0xeef3ff));
    d.fillCircle(mx + 4, my - 3, 3, rgb(0xd6deee)); d.fillCircle(mx - 4, my + 4, 2, rgb(0xd6deee));
  }
  jollySnow(d, f, phase, 34 * W / 480, 3, 2.4f, 1, mix(t.bg, snow, 0.55f));
  switch (jolly::place()) {
    case jolly::TREEFARM: jollyTreeFarm(d, t, f, phase, scroll); break;
    case jolly::LAKE:     jollyLake(d, t, f, phase, scroll); break;
    case jolly::TOWER:    jollyTower(d, t, f, phase, scroll); break;
    default:              jollyCabin(d, t, f, phase, scroll); break;
  }
  // Snow on the ground, in soft drifts that roll by.
  for (int x = 0; x < W; x += 3) {
    const float xs = x + scroll;
    const int y = g - 2 - (int)(1.5f + 1.5f * sinf(xs * 0.045f) + sinf(xs * 0.013f));
    d.fillRect(x, y, 3, g - y + 1, mix(t.bg, snow, 0.85f));
  }
  d.drawFastHLine(0, g, W, mix(t.bg, snow, 0.5f));
  drawSasquatch(d, t, f.mascotX, g - mascotLift(), 88, phase, unread ? t.amber : t.blue, mascotPose());
  // The near snow, bigger and quicker, in front of everything.
  jollySnow(d, f, phase, 16 * W / 480, 77, 4.6f, 2, snow);
}
