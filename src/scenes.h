// Lock-screen scenes, one per theme. Each draws the band between the status bar
// and y = 172; the clock and summary text go underneath.

#pragma once
#include <math.h>
#include "display_config.h"
#include "theme.h"
#include "mascot.h"

namespace scenes {

constexpr int GROUND = 170;

inline uint16_t rgb(uint32_t hex) { return lgfx::color565(hex >> 16, (hex >> 8) & 0xFF, hex & 0xFF); }

inline uint16_t mix(uint16_t a, uint16_t b, float f) {
  const int ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31;
  const int br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
  return (uint16_t)(((int)(ar + (br - ar) * f) << 11) | ((int)(ag + (bg - ag) * f) << 5) | (int)(ab + (bb - ab) * f));
}

inline uint32_t hash(int32_t x) {
  uint32_t h = (uint32_t)x * 2654435761u;
  h ^= h >> 13; h *= 0x5bd1e995; h ^= h >> 15;
  return h;
}

inline void stars(lgfx::LovyanGFX& d, uint16_t c, float phase, int count, int maxY) {
  for (int i = 0; i < count; i++) {
    const uint32_t h = hash(i + 7);
    const int x = h % 480, y = 20 + (h >> 9) % (maxY - 20);
    const bool on = ((int)(phase * 0.2f) + i) % 7 != 0;          // an occasional twinkle
    if (on) d.drawPixel(x, y, (i % 5 == 0) ? mix(c, 0xFFFF, 0.6f) : c);
  }
}

// ---- INW: mountains, scrolling pines, the walking sasquatch ---------------------------
inline void inw(lgfx::LovyanGFX& d, const Theme& t, float phase, float scroll, bool unread) {
  d.fillTriangle(0, 120, 90, 66, 160, 120, t.line);
  d.fillTriangle(120, 124, 230, 58, 340, 124, t.line);
  d.fillTriangle(300, 120, 400, 72, 480, 120, t.line);
  const int span = 480 + 60;
  for (int i = 0; i < 14; i++) {
    int px = (int)(i * 46 - scroll);
    px = ((px % span) + span) % span - 30;
    if (px > 200 && px < 300) continue;
    const int ph = 20 + (i % 3) * 7;
    d.fillTriangle(px, GROUND - 4, px + 9, GROUND - 4 - ph, px + 18, GROUND - 4, t.greenDim);
  }
  d.drawFastHLine(0, GROUND, 480, t.greenDim);
  drawSasquatch(d, t, 250, GROUND, 88, phase, unread ? t.amber : t.green);
}

// ---- Blocks: block terrain, square moon, a blocky explorer -----------------------------
inline void blocks(lgfx::LovyanGFX& d, const Theme& t, float phase, float scroll, bool unread) {
  constexpr int B = 12;
  const uint16_t dirt = rgb(0x7a5230), dirtDark = rgb(0x5c3c22), grass = t.green, grassDark = t.greenDim;
  const uint16_t leaf = rgb(0x2f6b1f), trunk = rgb(0x6b4a2b), cloud = rgb(0x2c3442);
  stars(d, t.dim, phase, 40, 110);
  const int mx = 40 + (int)fmodf(scroll * 0.05f, 400.0f);
  d.fillRect(mx, 30, 18, 18, rgb(0xefe7c4));
  d.fillRect(mx + 4, 34, 4, 4, rgb(0xcfc6a0));
  d.fillRect(mx + 10, 40, 3, 3, rgb(0xcfc6a0));
  for (int c = 0; c < 3; c++) {                                  // chunky clouds
    int cx = (int)(c * 190 - scroll * 0.3f);
    cx = ((cx % 620) + 620) % 620 - 70;
    const int cy = 55 + c * 14;
    d.fillRect(cx, cy, 64, 10, cloud);
    d.fillRect(cx + 10, cy - 8, 36, 8, cloud);
  }
  const int off = (int)scroll % B;
  const int first = (int)scroll / B;
  for (int col = -1; col <= 480 / B + 1; col++) {
    const int world = first + col;
    const int h = 2 + hash(world) % 3;
    const int x = col * B - off;
    const int top = GROUND - h * B;
    for (int r = 0; r < h; r++) {
      const int y = top + r * B;
      if (r == 0) {
        d.fillRect(x, y, B, B, dirt);
        d.fillRect(x, y, B, 4, grass);
        d.fillRect(x + 2, y + 4, 2, 2, grass);                    // drips
        d.fillRect(x + 7, y + 4, 2, 3, grassDark);
      } else {
        d.fillRect(x, y, B, B, (r % 2) ? dirt : dirtDark);
        d.fillRect(x + 3, y + 5, 2, 2, dirtDark);
      }
      d.drawRect(x, y, B, B, mix(dirtDark, t.bg, 0.5f));
    }
    if (hash(world * 31) % 9 == 0) {                              // a tree
      d.fillRect(x + 4, top - 3 * B, 4, 3 * B, trunk);
      d.fillRect(x - B, top - 5 * B, 3 * B, 2 * B, leaf);
      d.fillRect(x, top - 6 * B, B, B, leaf);
    }
  }
  // The explorer stands on whichever column is under him.
  const int px = 234;
  const int world = first + (px + off) / B;
  const int ground = GROUND - (2 + hash(world) % 3) * B;
  const int swing = (int)(sinf(phase) * 3);
  const uint16_t skin = rgb(0xc89b6e), hair = rgb(0x4a2e1a), shirt = rgb(0x2f9e9e), pants = rgb(0x3b3f9e);
  d.fillRect(px + 2 + swing, ground - 14, 5, 14, pants);          // legs
  d.fillRect(px + 7 - swing, ground - 14, 5, 14, rgb(0x33378a));
  d.fillRect(px, ground - 30, 14, 16, shirt);                     // body
  d.fillRect(px - 4, ground - 29 + swing, 4, 12, skin);           // arms
  d.fillRect(px + 14, ground - 29 - swing, 4, 12, skin);
  d.fillRect(px + 1, ground - 42, 12, 12, skin);                  // head
  d.fillRect(px + 1, ground - 42, 12, 4, hair);
  d.fillRect(px + 4, ground - 36, 2, 2, rgb(0x3a5bd9));
  d.fillRect(px + 9, ground - 36, 2, 2, rgb(0x3a5bd9));
  if (unread) {                                                   // a gold block bobs overhead
    const int by = ground - 62 + (int)(sinf(phase * 0.5f) * 3);
    d.fillRect(px + 2, by, 10, 10, t.amber);
    d.drawRect(px + 2, by, 10, 10, mix(t.amber, 0, 0.4f));
  }
}

// ---- Hero: night hills, a castle, a hooded adventurer and a fairy ----------------------
inline void heart(lgfx::LovyanGFX& d, int x, int y, uint16_t c) {
  static const uint8_t H[6] = {0x36, 0x7F, 0x7F, 0x3E, 0x1C, 0x08};
  for (int r = 0; r < 6; r++)
    for (int b = 0; b < 7; b++)
      if (H[r] & (0x40 >> b)) d.fillRect(x + b * 2, y + r * 2, 2, 2, c);
}

inline void hero(lgfx::LovyanGFX& d, const Theme& t, float phase, float scroll, bool unread,
                 uint8_t batteryPct, uint16_t unreadCount) {
  stars(d, t.dim, phase, 50, 100);
  d.fillCircle(400, 44, 13, rgb(0xe9eef5));
  d.fillCircle(405, 40, 11, t.bg);
  // Far hill with a castle.
  for (int x = 0; x < 480; x += 2) {
    const int y = 118 + (int)(sinf((x + scroll * 0.2f) * 0.012f) * 12);
    d.fillRect(x, y, 2, GROUND - y, t.line);
  }
  const int cx = 80 - (int)fmodf(scroll * 0.2f, 600.0f);
  if (cx > -80) {
    const uint16_t stone = mix(t.line, t.bg, 0.4f);
    d.fillRect(cx, 84, 46, 34, stone);
    d.fillRect(cx - 8, 72, 12, 46, stone);
    d.fillRect(cx + 42, 72, 12, 46, stone);
    d.fillTriangle(cx - 10, 72, cx - 2, 58, cx + 6, 72, stone);
    d.fillTriangle(cx + 40, 72, cx + 48, 58, cx + 56, 72, stone);
    d.fillRect(cx + 18, 100, 10, 18, t.bg);
    d.fillRect(cx + 4, 90, 4, 5, t.amber);                         // a lit window
  }
  // Front hill.
  auto hill = [&](int x) { return 146 + (int)(sinf((x + scroll) * 0.018f) * 9); };
  for (int x = 0; x < 480; x += 2) {
    const int y = hill(x);
    d.fillRect(x, y, 2, GROUND - y, t.greenDim);
    if (hash((int)(x + scroll) / 2) % 11 == 0) d.drawFastVLine(x, y - 3, 3, mix(t.greenDim, t.green, 0.3f));
  }
  // The adventurer.
  const int px = 232, g = hill(px + 6);
  const int step = (int)(sinf(phase) * 3);
  const uint16_t cloak = rgb(0x6b3f24), hood = rgb(0x3e9b5c), cloakDark = rgb(0x3a2414), skin = rgb(0xe0b48a);
  d.drawLine(px + 4, g - 12, px + 2 + step, g, cloakDark);          // legs
  d.drawLine(px + 9, g - 12, px + 11 - step, g, cloakDark);
  d.fillTriangle(px - 4, g - 10, px + 6, g - 38, px + 17, g - 10, cloak);   // cloak
  d.fillCircle(px + 6, g - 40, 6, hood);                            // hood
  d.fillCircle(px + 8, g - 39, 3, skin);                            // face
  d.drawLine(px + 16, g - 30, px + 26, g - 50, rgb(0xd8dee8));      // sword over the shoulder
  d.drawLine(px + 14, g - 27, px + 19, g - 32, t.amber);            // hilt
  d.fillCircle(px - 2, g - 22, 6, t.blue);                          // shield
  d.drawCircle(px - 2, g - 22, 6, rgb(0xd8dee8));
  // The fairy loops around them.
  const float fa = phase * 0.25f;
  const int fx = px + 6 + (int)(cosf(fa) * 34), fy = g - 56 + (int)(sinf(fa * 1.7f) * 14);
  for (int k = 1; k <= 4; k++) {
    const float fb = fa - k * 0.18f;
    d.drawPixel(px + 6 + (int)(cosf(fb) * 34), g - 56 + (int)(sinf(fb * 1.7f) * 14), mix(t.blue, t.bg, k * 0.2f));
  }
  d.fillCircle(fx, fy, 5, mix(t.blue, t.bg, 0.6f));
  d.fillCircle(fx, fy, 3, t.blue);
  d.fillCircle(fx, fy, 1, 0xFFFF);
  const bool flap = ((int)(phase * 2)) % 2;
  d.drawLine(fx - 2, fy - 1, fx - 6, fy - (flap ? 5 : 2), mix(t.blue, 0xFFFF, 0.5f));
  d.drawLine(fx + 2, fy - 1, fx + 6, fy - (flap ? 5 : 2), mix(t.blue, 0xFFFF, 0.5f));
  // Hearts are the battery, gems are unread messages.
  const int full = (batteryPct + 10) / 20;
  for (int i = 0; i < 5; i++) heart(d, 8 + i * 18, 24, i < full ? t.red : t.line);
  if (unread) {
    const int gx = 110;
    d.fillTriangle(gx, 30, gx + 6, 23, gx + 12, 30, t.green);
    d.fillTriangle(gx, 30, gx + 6, 39, gx + 12, 30, mix(t.green, 0, 0.35f));
    d.setFont(&fonts::Font2);
    d.setTextColor(t.green);
    char b[8];
    snprintf(b, sizeof(b), "x%u", unreadCount);
    d.drawString(b, gx + 16, 24);
  }
}

// ---- Aurora: northern lights over a pine ridge -----------------------------------------
// What the scene was last drawn with, so a screen change can carry on from exactly
// what was on screen (fx_aurora.cpp).
struct AuroraState { float phase, scroll; bool unread; };
inline AuroraState& auroraLast() { static AuroraState s = {0, 0, false}; return s; }

// The ridge, the pines and the sasquatch walking under the lights, dy px lower.
inline void auroraGround(lgfx::LovyanGFX& d, const Theme& t, float phase, float scroll, bool unread, int dy = 0) {
  const uint16_t ridge = mix(t.bg, t.line, 0.6f), pine = mix(t.bg, t.line, 0.35f);
  d.fillTriangle(0, 150 + dy, 110, 112 + dy, 230, 150 + dy, ridge);
  d.fillTriangle(180, 152 + dy, 330, 104 + dy, 480, 152 + dy, ridge);
  d.fillRect(0, 150 + dy, 480, GROUND - 150, ridge);
  // Same speed and wrap as the INW scene, so the walk cycle matches the ground.
  const int span = 480 + 60;
  for (int i = 0; i < 18; i++) {
    int x = (int)(i * 29 + (hash(i) % 13) - scroll);
    x = ((x % span) + span) % span - 30;
    if (x > 205 && x < 290) continue;
    const int h = 18 + hash(i * 3) % 16;
    d.fillTriangle(x, GROUND + dy, x + 7, GROUND - h + dy, x + 14, GROUND + dy, pine);
  }
  d.drawFastHLine(0, GROUND + dy, 480, t.line);
  drawSasquatch(d, t, 250, GROUND + dy, 80, phase, unread ? t.amber : t.green);   // walking, like INW: the pines scroll past
}

inline void aurora(lgfx::LovyanGFX& d, const Theme& t, float phase, float scroll, bool unread) {
  auroraLast() = {phase, scroll, unread};
  stars(d, t.dim, phase, 70, 150);
  const float p = phase * 0.05f;
  for (int band = 0; band < 2; band++) {
    const uint16_t c = band ? t.greenDim : t.green;
    const int base = band ? 42 : 64;
    for (int x = 0; x < 480; x += 3) {
      const float fx = x * 0.011f;
      const int y = base + (int)(sinf(fx * (band ? 1.6f : 1.0f) + p * (band ? -1.3f : 1.0f)) * 16 + sinf(fx * 3.1f - p * 0.7f) * 6);
      const int len = 26 + (int)((sinf(fx * 2.3f + p * 1.9f) + 1) * 16);
      const float glow = 0.55f + 0.45f * sinf(fx * 1.7f + p * 2.4f);
      for (int k = 0; k < 4; k++) {
        const float f = 1.0f - (k + 1) / 5.0f * glow;
        d.drawFastVLine(x, y + k * len / 4, len / 4 + 1, mix(t.bg, c, f * (1.0f - k * 0.22f)));
      }
    }
  }
  d.fillCircle(88, 40, 11, rgb(0xf2f5ff));
  d.fillCircle(93, 36, 10, t.bg);
  auroraGround(d, t, phase, scroll, unread);
}

}  // namespace scenes
