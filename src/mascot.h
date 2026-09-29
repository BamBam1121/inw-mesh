// The lock-screen sasquatch, drawn from primitives. `phase` drives the stride;
// legs use cos so phase 0 isn't both legs at mid-stride.
//
// He's one creature in the theme's secondary colour: the near limbs catch the light,
// the far ones sit in shadow, and shaggy tufts break up his outline. The eye keeps
// the colour it's given (the lock screen turns it amber for unread messages).
// SquatchPose is what he's doing besides walking - blinking, talking, waving - and
// defaults to plain walking, which is all the screen transitions ask for.

#pragma once
#include <math.h>
#include "display_config.h"
#include "theme.h"

struct SquatchPose {
    bool blink = false;       // eyes shut for this frame
    uint8_t mouth = 0;        // 0 shut, 1 open (talking), 2 wide (a yell or a yawn)
    float wave = 0;           // the near arm up and waving, 0..1
    float armsUp = 0;         // both arms flung up, 0..1 (a hard shake)
    float slump = 0;          // tired: head down, eyes half shut, 0..1 (a low battery)
    bool happy = false;       // eyes shut in a smile (the charger)
    float dizzy = -1;         // >= 0: stars circling his head, at this angle
};

namespace mascot {

inline uint16_t mix(uint16_t a, uint16_t b, float f) {
    const int ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31;
    const int br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
    return (uint16_t)(((int)(ar + (br - ar) * f) << 11) | ((int)(ag + (bg - ag) * f) << 5) | (int)(ab + (bb - ab) * f));
}

inline float lerp(float a, float b, float f) { return a + (b - a) * f; }

// A tuft of fur: a thin triangle from a base on his outline, pointing (dx, dy).
inline void tuft(lgfx::LovyanGFX& d, float bx, float by, float dx, float dy, float len, float half, uint16_t c) {
    const float n = sqrtf(dx * dx + dy * dy);
    if (n <= 0) return;
    dx /= n; dy /= n;
    d.fillTriangle((int)lroundf(bx - dy * half), (int)lroundf(by + dx * half),
                   (int)lroundf(bx + dy * half), (int)lroundf(by - dx * half),
                   (int)lroundf(bx + dx * len), (int)lroundf(by + dy * len), c);
}

// A small four-point star (the dizzy ones).
inline void star(lgfx::LovyanGFX& d, int x, int y, int r, uint16_t c) {
    d.drawFastHLine(x - r, y, 2 * r + 1, c);
    d.drawFastVLine(x, y - r, 2 * r + 1, c);
    if (r > 1) { d.drawPixel(x - 1, y - 1, c); d.drawPixel(x + 1, y - 1, c); d.drawPixel(x - 1, y + 1, c); d.drawPixel(x + 1, y + 1, c); }
}

}  // namespace mascot

// x is the hip centre, groundY where the feet land, h the shoulder-to-ground height.
inline void drawSasquatch(lgfx::LovyanGFX& d, const Theme& t, int x, int groundY, int h,
                          float phase, uint16_t eyeColor, const SquatchPose& pose = SquatchPose()) {
    using namespace mascot;
    const float s = h / 90.0f;
    auto Sf = [&](float v) { return v * s; };
    auto S = [&](float v) { return (int)lroundf(v * s); };

    const uint16_t body = t.greenDim;
    const uint16_t nearC = mix(body, 0xFFFF, 0.2f);      // near limbs catch the light
    const uint16_t farC = mix(body, t.bg, 0.45f);        // far limbs in shadow
    const uint16_t shade = mix(body, t.bg, 0.3f);        // fur strokes
    const uint16_t face = mix(body, t.txt, 0.42f);       // bare face and palms
    const uint16_t dark = mix(body, t.bg, 0.75f);        // brow shadow, nose, mouth

    // The stride. Each leg swings with cos; the one moving forward is in the air (its
    // foot lifts), the other is planted as the ground scrolls under it. The body rides
    // highest as the legs pass each other.
    const float a = cosf(phase), sn = sinf(phase);
    const float bob = Sf(2.5f) * (1 - fabsf(a));
    const float gy = (float)groundY;
    const float hipY = gy - Sf(40) - bob, shY = gy - Sf(72) - bob;

    auto leg = [&](float sw, float lift, uint16_t c) {
        const float kx = x - Sf(2) + sw * Sf(13) + lift * 0.5f, ky = gy - Sf(18) - lift * 0.7f;
        const float fx = x - Sf(1) + sw * Sf(24), fy = gy - Sf(3) - lift;
        d.drawWideLine(x - S(4), (int)(hipY + Sf(2)), (int)kx, (int)ky, Sf(6.5f), c);
        d.drawWideLine((int)kx, (int)ky, (int)fx, (int)fy, Sf(5.5f), c);
        // Big feet, toes down a little while the foot is in the air.
        d.drawWideLine((int)(fx - Sf(4)), (int)(fy + Sf(1)), (int)(fx + Sf(8)), (int)(fy + Sf(1) + lift * 0.35f), Sf(3.2f), c);
        // Shaggy calf.
        tuft(d, kx - Sf(4), ky + Sf(4), -1.0f, 0.6f, Sf(5), Sf(2.2f), c);
    };
    // Arm from the shoulder: hanging and swinging, or raised (wave / armsUp).
    const float shx = x + Sf(7), shy = shY + Sf(5);
    auto arm = [&](float sw, float wave, float up, bool nearSide, uint16_t c) {
        // Walking: the arm hangs past the hip and swings.
        float ex = x + Sf(8) + sw * Sf(10), ey = hipY + Sf(2);
        float hx = x + Sf(6) + sw * Sf(18), hy = hipY + Sf(20);
        if (wave > 0) {                                   // raised high over his head, clear of his face
            const float w = sinf(phase * 1.7f) * 0.55f;   // the forearm rocks about the elbow
            const float wex = x + Sf(3), wey = shY - Sf(9);
            ex = lerp(ex, wex, wave); ey = lerp(ey, wey, wave);
            hx = lerp(hx, wex + sinf(w) * Sf(16), wave); hy = lerp(hy, wey - cosf(w) * Sf(16), wave);
        }
        if (up > 0) {                                     // thrown up: the near one back, the far one high over his head
            const float wob = sinf(phase * 3.1f + (nearSide ? 0 : 1.7f)) * Sf(3);
            ex = lerp(ex, nearSide ? x - Sf(2) : x + Sf(14), up); ey = lerp(ey, nearSide ? shY - Sf(6) : shY - Sf(14), up);
            hx = lerp(hx, (nearSide ? x - Sf(9) : x + Sf(24)) + wob, up); hy = lerp(hy, nearSide ? shY - Sf(24) : shY - Sf(34), up);
        }
        d.drawWedgeLine((int)shx, (int)shy, (int)ex, (int)ey, Sf(6), Sf(5), c);
        d.drawWedgeLine((int)ex, (int)ey, (int)hx, (int)hy, Sf(5), Sf(4), c);
        d.fillCircle((int)hx, (int)hy, S(3.6f), c);       // a big mitt of a hand
        // Long hair hanging off the forearm.
        const float mx = (ex + hx) * 0.5f, my = (ey + hy) * 0.5f;
        const float fdx = hx - ex, fdy = hy - ey, fl = sqrtf(fdx * fdx + fdy * fdy) + 0.01f;
        const float side = fdx >= 0 ? -1.0f : 1.0f;       // the underside of the forearm
        tuft(d, mx - fdy / fl * Sf(3) * side, my + fdx / fl * Sf(3) * side, -fdy / fl * side - 0.3f, fdx / fl * side + 0.9f, Sf(4.5f), Sf(1.8f), c);
        tuft(d, ex, ey + Sf(2), -0.5f, 1.0f, Sf(4.5f), Sf(2), c);
    };

    // Far side first: arm and leg in shadow, behind the body.
    const float armFar = a, armNear = -a;                 // arms swing against the legs
    arm(armFar, 0, pose.armsUp, false, farC);
    leg(-a, fmaxf(0, sn) * Sf(6), farC);
    leg(a, fmaxf(0, -sn) * Sf(6), nearC);

    // The body: hunched, shoulders wide and carried forward, hips narrow, rounded off.
    // Drawn twice: first a pixel up and back in the accent colour, then the body over
    // it, which leaves a thin rim of moonlight along his back and head.
    const float cx = x + Sf(1), cy = shY + Sf(18);
    const float P[8][2] = {
        {x - Sf(9), shY - Sf(4)}, {x - Sf(19), shY + Sf(7)}, {x - Sf(17), hipY - Sf(6)}, {x - Sf(11), hipY + Sf(7)},
        {x + Sf(8), hipY + Sf(6)}, {x + Sf(15), hipY - Sf(10)}, {x + Sf(18), shY + Sf(9)}, {x + Sf(10), shY - Sf(5)}};
    const float hx = x + Sf(15) + pose.slump * Sf(3), hy = shY - Sf(7) + pose.slump * Sf(5);
    // Fur hanging down his back: tufts of different lengths, swaying a little as he goes.
    static const float FUR[9][3] = {   // along the back 0..1, length, how far it hangs down
        {0.00f, 5.0f, 0.5f}, {0.12f, 7.0f, 0.8f}, {0.25f, 5.5f, 0.9f}, {0.37f, 7.5f, 1.1f}, {0.50f, 6.0f, 1.2f},
        {0.62f, 7.0f, 1.4f}, {0.74f, 5.0f, 1.5f}, {0.86f, 6.5f, 1.7f}, {1.00f, 5.0f, 1.9f}};
    auto torso = [&](int ox, int oy, uint16_t c) {
        auto tri = [&](float x0, float y0, float x1, float y1, float x2, float y2) {
            d.fillTriangle((int)x0 + ox, (int)y0 + oy, (int)x1 + ox, (int)y1 + oy, (int)x2 + ox, (int)y2 + oy, c);
        };
        for (int i = 0; i < 8; i++) tri(cx, cy, P[i][0], P[i][1], P[(i + 1) % 8][0], P[(i + 1) % 8][1]);
        d.fillCircle((int)(x - Sf(8)) + ox, (int)(shY + Sf(6)) + oy, S(11), c);     // the hump
        d.fillCircle((int)(x + Sf(8)) + ox, (int)(shY + Sf(8)) + oy, S(10), c);     // the chest
        d.fillCircle((int)(x - Sf(3)) + ox, (int)(hipY - Sf(2)) + oy, S(11), c);    // the hips
        for (int i = 0; i < 9; i++) {
            const float f = FUR[i][0];
            const float bx = lerp(x - Sf(12), x - Sf(15), f) - sinf(f * 3.1416f) * Sf(7);
            const float by = lerp(shY - Sf(1), hipY + Sf(5), f);
            const float sway = sinf(phase * 0.5f + i * 1.3f) * 0.2f;
            tuft(d, bx + ox, by + oy, -1.0f, FUR[i][2] + sway, Sf(FUR[i][1]), Sf(2.3f), c);
        }
        // The head: a round skull under a pointed crest, fur off the back of it.
        d.fillEllipse((int)(hx - Sf(1)) + ox, (int)hy + oy, S(10), S(9), c);
        d.fillTriangle((int)(hx - Sf(10)) + ox, (int)(hy + Sf(1)) + oy, (int)(hx - Sf(3)) + ox, (int)(hy - Sf(13)) + oy,
                       (int)(hx + Sf(4)) + ox, (int)(hy - Sf(6)) + oy, c);
        tuft(d, hx - Sf(9) + ox, hy + Sf(1) + oy, -1.0f, 0.9f, Sf(5), Sf(2.2f), c);
        tuft(d, hx - Sf(6) + ox, hy - Sf(7) + oy, -1.0f, 0.2f, Sf(5), Sf(2), c);
    };
    torso(-1, -1, t.green);
    torso(0, 0, body);
    // A few strokes of darker fur on the body.
    static const int8_t STROKE[6][4] = {{-10, 4, -13, 9}, {-4, 10, -7, 15}, {-12, 18, -14, 23},
                                        {2, 16, 0, 21}, {-6, 26, -8, 31}, {8, 4, 7, 9}};
    for (const auto& k : STROKE)
        d.drawLine((int)(x + Sf(k[0])), (int)(shY + Sf(k[1])), (int)(x + Sf(k[2])), (int)(shY + Sf(k[3])), shade);

    // The face: bare skin, a jutting muzzle, a nose.
    d.fillEllipse((int)(hx + Sf(5)), (int)(hy + Sf(2)), S(5), S(6), face);
    d.fillEllipse((int)(hx + Sf(8)), (int)(hy + Sf(6)), S(4), S(3.5f), face);
    d.fillRect((int)(hx + Sf(10)), (int)(hy + Sf(3)), max(1, S(2)), max(1, S(1.5f)), dark);

    // The eye glows under a heavy brow; it shuts to blink, and smiles when he's happy.
    const int ex = (int)(hx + Sf(6)), ey = (int)(hy + Sf(1));
    const int er = max(1, S(2));
    if (pose.happy) {                                     // ^, two pixels thick
        for (int k = 0; k < 2; k++) {
            d.drawLine(ex - er - 1, ey + 1 + k, ex, ey - er + 1 + k, eyeColor);
            d.drawLine(ex, ey - er + 1 + k, ex + er + 1, ey + 1 + k, eyeColor);
        }
    } else if (pose.blink) {
        d.drawFastHLine(ex - er - 1, ey + 1, 2 * er + 3, dark);
    } else {
        d.fillCircle(ex, ey, er, eyeColor);
        if (pose.slump > 0.3f) d.fillRect(ex - er - 1, ey - er - 1, 2 * er + 3, er + 1, body);   // heavy lids
    }
    d.drawWideLine((int)(hx + Sf(1)), (int)(hy - Sf(3)), (int)(hx + Sf(10)), (int)(hy - Sf(2)), Sf(1.8f), body);   // the brow
    // The mouth: a line, or open to talk.
    const int mx = (int)(hx + Sf(9)), my = (int)(hy + Sf(8));
    if (pose.mouth == 2) d.fillEllipse(mx, my + S(1), max(2, S(2)), max(2, S(2.5f)), dark);
    else if (pose.mouth == 1) d.fillEllipse(mx, my, max(1, S(2)), max(1, S(1.5f)), dark);
    else d.drawLine(mx - S(2), my, mx + S(2), my - S(1), dark);

    // Near arm last: it swings across the body.
    arm(armNear, pose.wave, pose.armsUp, true, nearC);

    if (pose.dizzy >= 0)                                  // stars circling his head
        for (int k = 0; k < 3; k++) {
            const float ang = pose.dizzy + k * 2.0944f;
            const int sx = (int)(hx + cosf(ang) * Sf(13)), sy = (int)(hy - Sf(15) + sinf(ang) * Sf(4));
            star(d, sx, sy, max(1, S(2.5f)), sinf(ang) < 0 ? mix(t.amber, t.bg, 0.4f) : t.amber);
        }
}
