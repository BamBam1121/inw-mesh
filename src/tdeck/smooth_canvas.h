// Round shapes and slanted lines with soft edges, for the T-Deck's screens.
//
// The T-Deck's panel has big pixels (320 x 240 on 2.8 inches, about 143 to the
// inch; the pager has about 227), so a circle or a rounded corner drawn pixel-on,
// pixel-off shows its staircase. The screens draw onto a Canvas; here Canvas is this
// class, whose round rectangles, circles and slanted lines blend their edge pixels
// with what is already underneath. Nothing that draws had to change: the same calls
// come out smooth. Straight edges, text and anything drawn through a plain
// LovyanGFX reference (the lock scenes' pixel art) are left exactly as they were.
//
// It costs little: a shape is worked out for one corner and mirrored to the other
// three, only the pixels along its edge are blended (one read of the canvas each,
// and the canvas is in memory), and the inside of each row is one plain line.

#pragma once
#include <math.h>
#include "display_config.h"

class SmoothCanvas : public lgfx::LGFX_Sprite {
public:
  using lgfx::LGFX_Sprite::LGFX_Sprite;

  template <typename T> void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, const T& c) {
    soft(x, y, w, h, (float)r, lgfx::convert_to_rgb888(c), true);
  }
  template <typename T> void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, const T& c) {
    soft(x, y, w, h, (float)r, lgfx::convert_to_rgb888(c), false);
  }
  // A circle of radius r covers 2r+1 pixels across, as LovyanGFX's does.
  template <typename T> void fillCircle(int32_t x, int32_t y, int32_t r, const T& c) {
    soft(x - r, y - r, 2 * r + 1, 2 * r + 1, r + 0.5f, lgfx::convert_to_rgb888(c), true);
  }
  template <typename T> void drawCircle(int32_t x, int32_t y, int32_t r, const T& c) {
    soft(x - r, y - r, 2 * r + 1, 2 * r + 1, r + 0.5f, lgfx::convert_to_rgb888(c), false);
  }
  // Level and upright lines stay crisp; a slanted one is drawn one pixel wide with
  // blended edges.
  template <typename T> void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const T& c) {
    if (x0 == x1 || y0 == y1) lgfx::LGFX_Sprite::drawLine(x0, y0, x1, y1, c);
    else drawWideLine(x0, y0, x1, y1, 0.5f, c);
  }

private:
  static float unit(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

  void blend(int32_t px, int32_t py, uint32_t c, float a) {
    if (px < 0 || py < 0 || px >= width() || py >= height()) return;
    if (a >= 0.98f) { drawPixel(px, py, c); return; }
    const auto bg = readPixelRGB(px, py);
    const int r = (int)(bg.R8() + (((int)(c >> 16) & 0xFF) - bg.R8()) * a);
    const int g = (int)(bg.G8() + (((int)(c >> 8) & 0xFF) - bg.G8()) * a);
    const int b = (int)(bg.B8() + (((int)c & 0xFF) - bg.B8()) * a);
    drawPixel(px, py, (uint32_t)((r << 16) | (g << 8) | b));
  }
  // The same pixel in all four corners (once each where they coincide).
  void blend4(int32_t xl, int32_t xr, int32_t yt, int32_t yb, uint32_t c, float a) {
    blend(xl, yt, c, a);
    if (xr != xl) blend(xr, yt, c, a);
    if (yb != yt) {
      blend(xl, yb, c, a);
      if (xr != xl) blend(xr, yb, c, a);
    }
  }

  // A rectangle w x h at (x, y) with corners of radius rc, filled or a one-pixel
  // outline. Each edge pixel is covered by however much of it lies inside the shape.
  void soft(int32_t x, int32_t y, int32_t w, int32_t h, float rc, uint32_t c, bool fill) {
    if (w <= 0 || h <= 0) return;
    const float hw = w * 0.5f, hh = h * 0.5f;
    if (rc > hw) rc = hw;
    if (rc > hh) rc = hh;
    if (rc < 1.5f) {                       // too small a corner to be worth blending
      if (fill) lgfx::LGFX_Sprite::fillRoundRect(x, y, w, h, (int32_t)rc, c);
      else lgfx::LGFX_Sprite::drawRoundRect(x, y, w, h, (int32_t)rc, c);
      return;
    }
    const int n = (int)ceilf(rc);          // the rows and columns the corners take up
    const int halfW = (w + 1) / 2, halfH = (h + 1) / 2;
    // The rows between the corners: straight sides.
    if (h > 2 * n) {
      if (fill) fillRect(x, y + n, w, h - 2 * n, c);
      else { drawFastVLine(x, y + n, h - 2 * n, c); drawFastVLine(x + w - 1, y + n, h - 2 * n, c); }
    }
    if (!fill && w > 2 * n) { drawFastHLine(x + n, y, w - 2 * n, c); drawFastHLine(x + n, y + h - 1, w - 2 * n, c); }
    // The corner rows: the top left corner, mirrored to the other three.
    for (int j = 0; j < n && j < halfH; j++) {
      const int yt = y + j, yb = y + h - 1 - j;
      const float qy = fabsf(j + 0.5f - hh) - (hh - rc);
      int i = 0;
      for (; i < halfW && (fill || i < n); i++) {
        // How far this pixel's centre is outside the shape (negative: inside).
        const float qx = fabsf(i + 0.5f - hw) - (hw - rc);
        const float sd = (qx > 0 && qy > 0 ? sqrtf(qx * qx + qy * qy) : (qx > qy ? qx : qy)) - rc;
        float a = unit(0.5f - sd);
        if (fill) {
          if (a >= 0.98f) break;           // solid from here to the same place on the right
        } else {
          a -= unit(-0.5f - sd);           // a ring one pixel wide
        }
        if (a > 0.02f) blend4(x + i, x + w - 1 - i, yt, yb, c, a);
      }
      if (fill && i < halfW) {
        drawFastHLine(x + i, yt, w - 2 * i, c);
        if (yb != yt) drawFastHLine(x + i, yb, w - 2 * i, c);
      }
    }
  }
};
