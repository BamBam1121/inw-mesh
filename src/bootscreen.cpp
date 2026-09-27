#include "bootscreen.h"
#include "app.h"

namespace boot {

// A small mesh: five nodes, each linked to its neighbours. While booting, a
// packet hops round the outer ring so a slow step never looks like a freeze.
static const int16_t NX[] = {88, 128, 156, 112, 70}, NY[] = {58, 44, 90, 126, 108};
static const uint8_t LINKS[][2] = {{0,1},{1,2},{2,3},{3,4},{4,0},{0,2},{1,3}};
constexpr uint32_t HOP_MS = 420;

void drawMark(lgfx::LovyanGFX& g, int ox, int oy, uint32_t ms, bool animate) {
  ox += MARK_DX; oy += MARK_DY;
  const int hop = (ms / HOP_MS) % 5;                    // links 0..4 are the outer ring
  const float f = (ms % HOP_MS) / (float)HOP_MS;
  for (int i = 0; i < 7; i++) {
    auto& l = LINKS[i];
    g.drawLine(NX[l[0]] + ox, NY[l[0]] + oy, NX[l[1]] + ox, NY[l[1]] + oy,
               animate && i == hop ? theme.green : theme.greenDim);
  }
  for (int i = 0; i < 5; i++) {
    const bool big = i == 2;
    // the node the packet just reached flares for the first part of the next hop
    const bool lit = animate && i == LINKS[(hop + 4) % 5][1] && f < 0.45f;
    g.fillCircle(NX[i] + ox, NY[i] + oy, big ? 9 : 6, lit ? theme.txt : theme.green);
    g.drawCircle(NX[i] + ox, NY[i] + oy, (big ? 13 : 9) + (lit ? 1 : 0), lit ? theme.green : theme.greenDim);
  }
  if (animate) {
    auto& l = LINKS[hop];
    const int px = NX[l[0]] + (NX[l[1]] - NX[l[0]]) * f + ox;
    const int py = NY[l[0]] + (NY[l[1]] - NY[l[0]]) * f + oy;
    g.fillCircle(px, py, 3, theme.txt);
  }
}

// "SQUATCH / M E S H". tint < 0: the theme's colours, with an offset copy
// underneath that reads as glow; otherwise one flat colour (the power-off tear).
static void wordmark(lgfx::LovyanGFX& g, int dx, int32_t tint) {
  g.setTextSize(1);                                     // at size 2 "SQUATCH" runs off the screen
  if (STACKED) {
    g.setFont(&fonts::FreeSansBold18pt7b);
    const int x = (L::W - g.textWidth("SQUATCH")) / 2 + dx;
    if (tint < 0) { g.setTextColor(theme.greenDim); g.drawString("SQUATCH", x + 2, 130); g.setTextColor(theme.green); }
    else g.setTextColor((uint16_t)tint);
    g.drawString("SQUATCH", x, 128);
    g.setFont(&fonts::FreeSans9pt7b);
    g.setTextColor(tint < 0 ? theme.txt : (uint16_t)tint);
    g.drawString("M E S H", (L::W - g.textWidth("M E S H")) / 2 + dx, 158);
    return;
  }
  g.setFont(&fonts::FreeSansBold24pt7b);
  if (tint < 0) {
    g.setTextColor(theme.greenDim);
    g.drawString("SQUATCH", 199 + dx, 49);
    g.setTextColor(theme.green);
  } else {
    g.setTextColor((uint16_t)tint);
  }
  g.drawString("SQUATCH", 196 + dx, 46);
  g.setFont(&fonts::FreeSans12pt7b);
  g.setTextColor(tint < 0 ? theme.txt : (uint16_t)tint);
  g.drawString("M E S H", 198 + dx, 106);
}

static void caption(lgfx::LovyanGFX& g, const char* s) {
  g.setFont(&fonts::Font2);
  g.setTextColor(theme.dim);
  g.drawString(s, STACKED ? (L::W - g.textWidth(s)) / 2 : 198, CAPTION_Y);
}

void drawLogo(lgfx::LovyanGFX& g) {
  g.fillScreen(theme.bg);
  drawMark(g, 0, 0, 0, false);
  wordmark(g, 0, -1);
  caption(g, "inland northwest  //  " FW_VERSION);
  g.drawRect(BAR_X, BAR_Y, BAR_W, 5, theme.line);
}

// ---- power-off animation ---------------------------------------------------------
// Boot plays forward: the mesh comes up and a packet hops round it. Power off plays
// it back. While storage flushes the boot screen returns with the packet still
// hopping; then the mesh drops link by link (each snapping with a spark, nodes
// going hollow as they lose their last link, the centre flickering out last), the
// wordmark tears, the boot bar drains, and the theme's power-down
// (fx.cpp) takes it to dark.
static const uint8_t KILL_ORDER[7] = {4, 0, 3, 6, 1, 5, 2};   // outer ring first, centre's links last
constexpr int32_t KILL_STEP = 95, SPARK_MS = 130, TEAR_END = 820;

void drawGoodbye(Canvas& g, uint32_t ms, int32_t brk) {
  g.fillScreen(theme.bg);
  g.setTextDatum(textdatum_t::top_left);
  if (brk < 0) {
    drawMark(g, 0, 0, ms, true);
  } else {
    int32_t linkDead[7], nodeDead[5] = {0, 0, 0, 0, 0};
    for (int p = 0; p < 7; p++) linkDead[KILL_ORDER[p]] = p * KILL_STEP;
    for (int i = 0; i < 7; i++)
      for (int e = 0; e < 2; e++) nodeDead[LINKS[i][e]] = max(nodeDead[LINKS[i][e]], linkDead[i]);
    for (int i = 0; i < 7; i++) {
      const int x0 = NX[LINKS[i][0]] + MARK_DX, y0 = NY[LINKS[i][0]] + MARK_DY;
      const int x1 = NX[LINKS[i][1]] + MARK_DX, y1 = NY[LINKS[i][1]] + MARK_DY;
      const int32_t since = brk - linkDead[i];
      if (since < 0) g.drawLine(x0, y0, x1, y1, theme.greenDim);
      else if (since < 45) g.drawLine(x0, y0, x1, y1, theme.txt);      // flashes white as it snaps
      if (since >= 0 && since < SPARK_MS) {
        const int mx = (x0 + x1) / 2, my = (y0 + y1) / 2;
        g.fillCircle(mx, my, max(1, (int)(4 - since * 4 / SPARK_MS)), since < 60 ? theme.txt : theme.green);
        const int d = 3 + since / 11;                                 // two fragments flying apart
        g.drawLine(mx - d, my - 1, mx - d - 4, my - 3, theme.green);
        g.drawLine(mx + d, my + 1, mx + d + 4, my + 3, theme.green);
      }
    }
    for (int i = 0; i < 5; i++) {
      const bool big = i == 2;
      const int32_t since = brk - nodeDead[i];
      bool up = since < 0;
      if (big && since >= 0 && since < 260) up = (since / 65) % 2 == 1;  // the centre flickers out
      const int x = NX[i] + MARK_DX, y = NY[i] + MARK_DY;
      if (up) {
        g.fillCircle(x, y, big ? 9 : 6, theme.green);
        g.drawCircle(x, y, big ? 13 : 9, theme.greenDim);
      } else {
        g.drawCircle(x, y, big ? 9 : 6, theme.greenDim);            // hollow: offline
      }
    }
  }

  wordmark(g, 0, -1);
  if (brk >= 0 && brk < TEAR_END && (brk / 70) % 2 == 0) {
    // A tear: one slice of the wordmark jumps sideways, split red and green.
    const uint32_t h = (uint32_t)(brk / 70 + 1) * 2654435761u;
    const int gy = (STACKED ? 126 : 44) + (int)((h >> 8) % (STACKED ? 46 : 78)), gh = 4 + (int)((h >> 16) % 12);
    const int dx = (int)((h >> 4) % 19) - 9;
    const int cx = STACKED ? 0 : 186, cw = STACKED ? L::W : 294;
    g.setClipRect(cx, gy, cw, gh);
    g.fillRect(cx, gy, cw, gh, theme.bg);
    wordmark(g, dx - 3, theme.red);
    wordmark(g, dx + 2, theme.green);
    g.clearClipRect();
  }

  if (brk < 0) {
    char cap[16];
    snprintf(cap, sizeof(cap), "saving%.*s", (int)((ms / 300) % 4), "...");
    caption(g, cap);
  } else {
    caption(g, "going dark");
  }
  const float left = brk < 0 ? 1.0f : max(0.0f, 1.0f - brk / 800.0f);   // the boot bar, draining
  g.drawRect(BAR_X, BAR_Y, BAR_W, 5, theme.line);
  if (left > 0) g.fillRect(BAR_X + 1, BAR_Y + 1, (int)((BAR_W - 2) * left), 3, theme.green);
}

}  // namespace boot
