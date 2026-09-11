// The lock-screen sasquatch, drawn from primitives. `phase` drives the stride;
// legs use cos so phase 0 isn't both legs at mid-stride.

#pragma once
#include <math.h>
#include "display_config.h"
#include "theme.h"

// x is the hip centre, groundY where the feet land, h the shoulder-to-ground height.
inline void drawSasquatch(lgfx::LovyanGFX& d, const Theme& t, int x, int groundY, int h,
                          float phase, uint16_t eyeColor) {
    const float s = h / 90.0f;
    auto S = [&](int v) { return (int)lroundf(v * s); };
    const int hip = groundY - S(40), sh = groundY - S(72);
    const float a = cosf(phase), b = -a;

    // Back leg first so the near one overlaps it and the stride reads as depth.
    const struct { float sw; uint16_t c; } legs[2] = { {b, t.greenDim}, {a, t.green} };
    for (const auto& L : legs) {
        const int kx = x - S(2) + (int)(L.sw * S(13));
        const int fx = x - S(1) + (int)(L.sw * S(24));
        d.drawWideLine(x - S(4), hip + S(2), kx, groundY - S(18), S(6), L.c);
        d.drawWideLine(kx, groundY - S(18), fx, groundY - S(2), S(5), L.c);
        d.drawWideLine(fx - S(3), groundY - S(1), fx + S(5), groundY - S(1), S(3), L.c);
    }

    // Hunched torso: shoulders wide and carried forward, hips narrow.
    d.fillTriangle(x - S(20), sh + S(12), x - S(14), sh - S(2), x + S(10), sh - S(4),
                   t.greenDim);
    d.fillTriangle(x - S(20), sh + S(12), x + S(10), sh - S(4), x + S(17), sh + S(10),
                   t.greenDim);
    d.fillTriangle(x - S(20), sh + S(12), x + S(17), sh + S(10), x + S(13), hip + S(6),
                   t.greenDim);
    d.fillTriangle(x - S(20), sh + S(12), x + S(13), hip + S(6), x - S(10), hip + S(8),
                   t.greenDim);
    d.drawLine(x - S(14), sh - S(2), x + S(10), sh - S(4), t.green);
    d.drawLine(x + S(10), sh - S(4), x + S(17), sh + S(10), t.green);
    d.drawLine(x - S(20), sh + S(12), x - S(14), sh - S(2), t.green);

    // No neck: the head sits straight on the shoulders, brow out, skull sloping back.
    const int hx = x + S(14), hy = sh - S(8);
    d.fillTriangle(hx - S(11), hy + S(10), hx - S(8), hy - S(3), hx + S(2), hy - S(7),
                   t.greenDim);
    d.fillTriangle(hx - S(11), hy + S(10), hx + S(2), hy - S(7), hx + S(9), hy - S(1),
                   t.greenDim);
    d.fillTriangle(hx - S(11), hy + S(10), hx + S(9), hy - S(1), hx + S(10), hy + S(6),
                   t.greenDim);
    d.fillTriangle(hx - S(11), hy + S(10), hx + S(10), hy + S(6), hx + S(2), hy + S(11),
                   t.greenDim);
    d.drawLine(hx - S(8), hy - S(3), hx + S(2), hy - S(7), t.green);
    d.drawLine(hx + S(2), hy - S(7), hx + S(9), hy - S(1), t.green);
    d.drawLine(hx + S(8), hy + S(6), hx + S(11), hy + S(7), t.green);   // muzzle
    d.fillCircle(hx + S(4), hy + S(2), max(1, S(2)), eyeColor);

    // Arms hang past the hip and swing against the legs.
    const struct { float sw; uint16_t c; } arms[2] = { {b, t.greenDim}, {a, t.green} };
    for (const auto& A : arms) {
        const int ex  = x + S(8) + (int)(A.sw * S(10));
        const int hnd = x + S(6) + (int)(A.sw * S(18));
        d.drawWideLine(x + S(6), sh + S(6), ex, hip + S(2), S(5), A.c);
        d.drawWideLine(ex, hip + S(2), hnd, hip + S(20), S(4), A.c);
    }

    for (int i = 0; i < 7; i++) {                    // shaggy back
        const int ty = sh + S(2) + i * S(8);
        d.drawLine(x - S(19), ty, x - S(24), ty + S(4), t.green);
    }
}
