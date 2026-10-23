// Squatch and the SD card's virus (sdguard.h). It drops in and taunts him; he charges
// in, winds up and lands a punch that knocks it flat, then jumps and stomps it to goo,
// and says so. Every frame follows ms alone, so the simulator draws what the device does.
#include "sdguard.h"
#include "app.h"
#include "mascot.h"

namespace sdguard {

using mascot::lerp;
using mascot::mix;
using mascot::rgb;

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
// How far through [a, b] ms is, 0..1.
static float span(uint32_t ms, uint32_t a, uint32_t b) { return clamp01((float)((int32_t)ms - (int32_t)a) / (float)(b - a)); }
static float easeOut(float u) { return 1 - (1 - u) * (1 - u); }
static float easeIn(float u) { return u * u; }

// The moments, in ms.
constexpr uint32_t DROP = 480, SETTLED = 700, WALK_AT = 450, WALK_END = 1650, WIND_AT = 1750, SWING_AT = 2150,
                   HIT = 2230, PULL_AT = 2550, PULL_END = 2850, CROUCH_AT = 2950, JUMP_AT = 3080, LAND = 3600,
                   SAY_AT = 3850, NOTE_AT = 4150;

constexpr int GROUND = L::H - 50;            // the line they stand on; the notes go under it
constexpr int SH = L::H < 230 ? 90 : 100;    // Squatch's shoulder height
constexpr float S = SH / 90.0f;              // his scale (mascot.h draws him 90 high)
constexpr int R = 25;                        // the virus's radius
constexpr int VX = L::W * 66 / 100;          // where it lands
constexpr int SX = VX - (int)(0.7f * R) - (int)(36 * S) - 4;   // where he stops: his fist lands on its near side
constexpr int KX = VX + 40;                  // where the punch knocks it
constexpr int FIST_X = SX + 6 + (int)(36 * S), FIST_DY = (int)((72 - 28) * S);   // the fist at full stretch (mascot.h), above the ground

enum Mood { ANGRY, SCARED, KO };

static void virus(Canvas& g, float cx, float cy, float sx, float sy, float spin, Mood mood, uint32_t ms) {
  const uint16_t body = rgb(0x7bd34a), dark = rgb(0x3d7a20), knob = rgb(0xff4d6d), ink = rgb(0x1a1024), white = 0xFFFF;
  for (int k = 0; k < 9; k++) {                           // spikes, each with a knob on the end
    const float a = spin + k * 6.2832f / 9, ca = cosf(a), sa = sinf(a);
    const float x0 = cx + ca * R * 0.8f * sx, y0 = cy + sa * R * 0.8f * sy;
    const float x1 = cx + ca * R * 1.4f * sx, y1 = cy + sa * R * 1.4f * sy;
    g.drawWideLine(x0, y0, x1, y1, 2.4f, dark);
    g.fillCircle((int)x1, (int)y1, 3, knob);
  }
  g.fillEllipse((int)cx, (int)cy, (int)(R * sx) + 1, (int)(R * sy) + 1, dark);
  g.fillEllipse((int)cx, (int)cy, (int)(R * sx), (int)(R * sy), body);
  g.fillCircle((int)(cx + R * 0.45f * sx), (int)(cy + R * 0.5f * sy), 3, mix(body, dark, 0.45f));   // blotches
  g.fillCircle((int)(cx + R * 0.62f * sx), (int)(cy - R * 0.1f * sy), 2, mix(body, dark, 0.45f));
  g.fillEllipse((int)(cx - R * 0.4f * sx), (int)(cy - R * 0.55f * sy), 5, 2, mix(body, white, 0.55f));   // shine

  // The face, turned towards him (left).
  const int e1 = (int)(cx - 10 * sx), e2 = (int)(cx + 1 * sx), ey = (int)(cy - 3 * sy), my = (int)(cy + 8 * sy);
  if (mood == KO) {
    for (int e : {e1, e2}) {                              // X eyes
      g.drawWideLine(e - 4, ey - 4, e + 4, ey + 4, 1.8f, ink);
      g.drawWideLine(e - 4, ey + 4, e + 4, ey - 4, 1.8f, ink);
    }
    for (int k = 0; k < 4; k++)                           // a wobbly mouth, its tongue hanging out
      g.drawLine(e1 - 2 + k * 4, my + (k & 1 ? 2 : 0), e1 + 2 + k * 4, my + (k & 1 ? 0 : 2), ink);
    g.fillEllipse(e1 + 7, my + 4, 3, 4, knob);
    return;
  }
  const int er = mood == SCARED ? 6 : 5;
  for (int e : {e1, e2}) {
    g.fillCircle(e, ey, er, white);
    g.drawCircle(e, ey, er, ink);
    g.fillCircle(e - 2, ey + (mood == SCARED ? -1 : 1), mood == SCARED ? 1 : 2, mood == SCARED ? ink : rgb(0xd0103a));
  }
  if (mood == ANGRY) {
    g.drawWideLine(e1 - 6, ey - 9, e1 + 4, ey - 4, 2.2f, ink);    // brows down in the middle
    g.drawWideLine(e2 + 6, ey - 9, e2 - 4, ey - 4, 2.2f, ink);
    const int mx = (e1 + e2) / 2;                         // a sneer full of teeth
    g.fillEllipse(mx, my, 9, 4, ink);
    for (int k = 0; k < 4; k++) g.fillTriangle(mx - 8 + k * 4, my - 3, mx - 4 + k * 4, my - 3, mx - 6 + k * 4, my + 1, white);
  } else {
    g.drawWideLine(e1 - 5, ey - 8, e1 + 3, ey - 11, 2.0f, ink);   // brows up
    g.drawWideLine(e2 + 5, ey - 8, e2 - 3, ey - 11, 2.0f, ink);
    g.fillEllipse((e1 + e2) / 2, my + 1, 3, 4 + (int)(sinf(ms * 0.05f) * 1.2f), ink);
    const int dy = (int)((ms / 3) % 14);                  // a bead of sweat running down
    g.fillCircle((int)(cx + R * sx * 0.75f), (int)(cy - R * 0.5f) + dy, 2, rgb(0x8fd8ff));
  }
}

// A comic-book burst with a word in it.
static void burst(Canvas& g, int cx, int cy, float r, const char* word, uint16_t fill, uint16_t edge) {
  if (r < 2) return;
  constexpr int N = 12;
  float px[2 * N], py[2 * N];
  for (int k = 0; k < 2 * N; k++) {
    const float a = k * 3.1416f / N, rr = (k & 1) ? r * 0.62f : r * (k % 4 ? 1.0f : 1.15f);
    px[k] = cx + cosf(a) * rr; py[k] = cy + sinf(a) * rr * 0.8f;
  }
  for (int k = 0; k < 2 * N; k++) {
    const int n = (k + 1) % (2 * N);
    g.fillTriangle(cx, cy, (int)px[k], (int)py[k], (int)px[n], (int)py[n], fill);
  }
  for (int k = 0; k < 2 * N; k++) {
    const int n = (k + 1) % (2 * N);
    g.drawLine((int)px[k], (int)py[k], (int)px[n], (int)py[n], edge);
  }
  if (r < 14) return;
  g.setFont(&fonts::FreeSansBold9pt7b);
  g.setTextDatum(textdatum_t::middle_center);
  g.setTextSize(1);
  g.setTextSize(1.45f * r / g.textWidth(word));          // fills the middle of the burst
  g.setTextColor(edge);
  g.drawString(word, cx + 1, cy + 1);
  g.setTextColor(rgb(0x1a1024));
  g.drawString(word, cx, cy);
  g.setTextSize(1);
}

static uint32_t hash(uint32_t k) { k *= 2654435761u; return k ^ (k >> 15); }

void drawFight(Canvas& g, uint32_t ms, const Found& f) {
  const Theme& t = theme;
  g.fillScreen(t.bg);
  g.setTextSize(1);

  // The screen shakes on the punch and harder on the stomp.
  float shake = 0;
  if (ms >= HIT) shake = 5 * (1 - span(ms, HIT, HIT + 300));
  if (ms >= LAND) shake = 8 * (1 - span(ms, LAND, LAND + 380));
  const int ox = (int)(shake * sinf(ms * 0.9f)), oy = (int)(shake * 0.6f * cosf(ms * 1.3f));
  const int gy = GROUND + oy;

  // The ground, with a little stubble along it.
  g.drawFastHLine(0, gy, L::W, t.greenDim);
  for (int x = 6; x < L::W; x += 23) g.drawLine(x + ox, gy, x + ox + 3, gy - 3 - (int)(hash(x) % 3), t.greenDim);

  // ---- the virus, before the stomp ----
  if (ms < LAND) {
    float vx = VX, vy, sx = 1, sy = 1, spin = ms * 0.0015f;
    Mood mood = ms >= WIND_AT + 120 ? SCARED : ANGRY;
    if (ms < DROP) {                                      // falling in from the top
      vy = lerp(-2.0f * R, GROUND - R, easeIn(span(ms, 0, DROP)));
      sy = 1.12f; sx = 0.9f;
    } else if (ms < SETTLED) {                            // and bouncing once
      const float u = span(ms, DROP, SETTLED);
      vy = GROUND - R - sinf(u * 3.1416f) * 16;
      if (u < 0.15f) { sx = 1.2f; sy = 0.8f; }            // squashed against the ground
    } else if (ms < HIT) {                                // hopping about, taunting him
      const float hop = fabsf(sinf((ms - SETTLED) * (mood == SCARED ? 0.03f : 0.011f)));
      vy = GROUND - R - hop * (mood == SCARED ? 3 : 7);
      sy = 0.94f + hop * 0.08f; sx = 2 - sy;
      if (mood == SCARED) vx += sinf(ms * 0.09f) * 1.5f;  // shaking
    } else {                                              // knocked flying, then out cold
      const float u = span(ms, HIT, HIT + 380);
      vx = lerp(VX, KX, easeOut(u));
      spin += u * 5;
      mood = KO;
      if (u < 1) { sx = 0.8f; sy = 1.15f; vy = GROUND - R - sinf(u * 3.1416f) * 24; }
      else { sx = 1.18f; sy = 0.72f; vy = GROUND - R * sy; }
    }
    vy = min(vy, (float)(GROUND - R * sy));
    virus(g, vx + ox, vy + oy, sx, sy, spin, mood, ms);
    if (mood == KO && ms > HIT + 380)                     // stars round its head
      for (int k = 0; k < 3; k++) {
        const float a = ms * 0.008f + k * 2.0944f;
        mascot::star(g, (int)(vx + cosf(a) * 20) + ox, (int)(vy - R * sy - 8 + sinf(a) * 4) + oy, 2, t.amber);
      }
  }

  // ---- the goo, after it ----
  if (ms >= LAND) {
    const uint32_t s = ms - LAND;
    const uint16_t goo = rgb(0x7bd34a), knob = rgb(0xff4d6d);
    g.fillEllipse(KX + ox, gy - 1, (int)(8 + 22 * easeOut(span(ms, LAND, LAND + 250))), 3, rgb(0x3d7a20));
    const float fade = 1 - span(ms, LAND + 1600, LAND + 2600);
    for (int k = 0; k < 16 && fade > 0; k++) {
      const uint32_t h = hash(k + 7);
      const float a = 3.1416f + 0.15f + (h % 1000) / 1000.0f * 2.84f;   // up and out
      const float v = 0.10f + ((h >> 10) % 100) / 100.0f * 0.16f;
      float x = KX + cosf(a) * v * s, y = gy - 6 + sinf(a) * v * s + 0.0009f * s * s;
      if (y > gy - 2) y = gy - 2;                         // landed: a splat that stays
      x = fminf(fmaxf(x, 3), L::W - 4);
      const int r = (int)((2 + h % 3) * fade + 0.5f);
      if (r > 0) g.fillCircle((int)x + ox, (int)y, r, (k % 3) ? goo : knob);
    }
    for (int side = -1; side <= 1; side += 2) {           // dust off his feet
      const float u = span(ms, LAND, LAND + 450);
      if (u < 1) g.fillCircle(KX + side * (int)(14 + 26 * u) + ox, gy - 4 - (int)(6 * u), (int)(7 * (1 - u)) + 1, mix(t.dim, t.bg, u));
    }
  }

  // ---- Squatch ----
  SquatchPose pose;
  float x = -60, lift = 0, phase = 1.5708f;
  int h = SH;
  if (ms >= WALK_AT && ms < WALK_END) {                   // charging in
    const float u = span(ms, WALK_AT, WALK_END);
    x = lerp(-50, SX, u);
    phase = 1.5708f - (WALK_END - ms) * 0.02f;
    pose.mouth = (ms / 140) % 2 ? 2 : 0;                  // roaring
  } else if (ms >= WALK_END) {
    x = SX;
  }
  if (ms >= WIND_AT && ms < SWING_AT) {                   // winding up
    const float u = easeOut(span(ms, WIND_AT, SWING_AT));
    pose.punch = -u; x -= 4 * u;
    pose.mouth = u > 0.6f ? 2 : 0;
  } else if (ms >= SWING_AT && ms < HIT) {                // the swing
    const float u = easeIn(span(ms, SWING_AT, HIT));
    pose.punch = lerp(-1, 1, u); x += -4 + 10 * u; pose.mouth = 2;
  } else if (ms >= HIT && ms < PULL_END) {                // follow through, then the arm comes back
    pose.punch = 1 - easeOut(span(ms, PULL_AT, PULL_END)); x += 6 * (1 - span(ms, PULL_AT, PULL_END)); pose.mouth = ms < PULL_AT ? 2 : 0;
  }
  if (ms >= CROUCH_AT && ms < JUMP_AT) {                  // crouching to jump
    h -= (int)(7 * span(ms, CROUCH_AT, JUMP_AT));
  } else if (ms >= JUMP_AT && ms < LAND) {                // up and over, arms flung up
    const float u = span(ms, JUMP_AT, LAND);
    x = lerp(SX + 6, KX - 4, u);
    lift = 4 * 62 * u * (1 - u);
    pose.armsUp = clamp01(u * 4);
    phase = 1.5708f + sinf(u * 6.2832f) * 0.6f;           // legs kicking
    pose.mouth = 2;
  } else if (ms >= LAND) {                                // landed on it
    x = KX - 4;
    h -= (int)(8 * (1 - span(ms, LAND, LAND + 220)));
    pose.armsUp = 1 - span(ms, LAND + 50, LAND + 300);
    if (ms >= SAY_AT) {                                   // says so, then throws his arms up
      pose.mouth = ms < SAY_AT + 900 && (ms / 110) % 2 ? 1 : 0;
      pose.armsUp = easeOut(span(ms, SAY_AT + 900, SAY_AT + 1200));
      pose.happy = ms > SAY_AT + 900;
      phase = 1.5708f + sinf(ms * 0.012f) * 0.12f;
    }
  }
  const uint16_t eye = ms >= WIND_AT && ms < LAND + 300 ? t.red : t.amber;
  drawSasquatch(g, t, (int)x + ox, gy - (int)lift, h, phase, eye, pose);

  // ---- the effects over them ----
  if (ms >= SWING_AT && ms < HIT + 60)                    // speed lines behind the fist
    for (int k = 0; k < 3; k++) {
      const int ly = gy - FIST_DY - 6 + k * 6, lx = (int)x + ox + (int)(20 * S);
      g.drawFastHLine(lx - 22 + k * 4, ly, 16, t.txt);
    }
  if (ms >= HIT && ms < HIT + 520) {                      // POW!, where the fist landed
    const float u = span(ms, HIT, HIT + 120), out = span(ms, HIT + 380, HIT + 520);
    burst(g, FIST_X + 4 + ox, gy - FIST_DY - 12 + oy, 32 * easeOut(u) * (1 - out), "POW!", rgb(0xffd23f), rgb(0xd0103a));
  }
  if (ms >= LAND && ms < LAND + 700) {                    // SPLAT!, out in the open where the goo flies
    const float u = span(ms, LAND, LAND + 140), out = span(ms, LAND + 550, LAND + 700);
    burst(g, KX - 105 + ox, gy - 62 + oy, 42 * easeOut(u) * (1 - out), "SPLAT!", rgb(0x9cf06a), rgb(0x2a6a12));
  }

  // ---- what's said ----
  g.setFont(&fonts::FreeSansBold9pt7b);
  g.setTextDatum(textdatum_t::top_center);
  if (ms < LAND) {
    if (ms >= HIT || (ms / 260) % 2 == 0) { g.setTextColor(t.red); g.drawString("VIRUS ON THE SD CARD", L::W / 2, 8); }
  } else {
    g.setTextColor(t.green);
    g.drawString("SD CARD CLEAN", L::W / 2, 8);
  }
  if (ms >= SAY_AT) {                                     // "I got ya!", popping up over his head
    const float u = span(ms, SAY_AT, SAY_AT + 260);
    const float sc = u < 0.7f ? easeOut(u / 0.7f) * 1.12f : lerp(1.12f, 1.0f, (u - 0.7f) / 0.3f);
    g.setFont(&fonts::FreeSansBold12pt7b);
    g.setTextSize(1);
    const int tw = g.textWidth("I got ya!"), bw = (int)((tw + 22) * sc), bh = (int)(34 * sc);
    const int headX = (int)x + (int)(16 * S), headY = gy - (int)(80 * S);
    int bx = min(max(headX + 18, bw / 2 + 4), L::W - bw / 2 - 4);
    const int by = max(headY - 38, 34 + bh / 2);
    if (bw > 4 && bh > 4) {
      g.fillTriangle(bx - 10, by + bh / 2 - 2, bx + 2, by + bh / 2 - 2, headX + 6, headY - 10, t.txt);
      g.fillRoundRect(bx - bw / 2, by - bh / 2, bw, bh, (int)(10 * sc), t.txt);
      if (sc > 0.55f) {
        g.setTextSize(sc);
        g.setTextDatum(textdatum_t::middle_center);
        g.setTextColor(t.bg);
        g.drawString("I got ya!", bx, by + 1);
        g.setTextSize(1);
      }
    }
  }
  if (ms >= NOTE_AT) {                                    // what was done, under the ground
    g.setFont(&fonts::Font2);
    g.setTextDatum(textdatum_t::top_center);
    char line[96];
    snprintf(line, sizeof(line), "Deleted %s", f.names);
    if (g.textWidth(line) > L::W - 12) snprintf(line, sizeof(line), "Deleted %u virus files", (unsigned)f.removed);
    g.setTextColor(t.txt);
    g.drawString(line, L::W / 2, GROUND + 10);
    if (f.failed) {
      snprintf(line, sizeof(line), "%u wouldn't go: format the card", (unsigned)f.failed);
      g.setTextColor(t.amber);
    } else {
      strlcpy(line, "Your maps and backups are safe", sizeof(line));
      g.setTextColor(t.dim);
    }
    g.drawString(line, L::W / 2, GROUND + 27);
  }
  g.setTextDatum(textdatum_t::top_left);
}

}  // namespace sdguard
