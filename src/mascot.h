// The lock-screen sasquatch, drawn from primitives. `phase` drives the stride;
// legs use cos so phase 0 isn't both legs at mid-stride.
//
// He's one creature in the theme's secondary colour: the near limbs catch the light,
// the far ones sit in shadow, and shaggy tufts break up his outline. The eye keeps
// the colour it's given (the lock screen turns it amber for unread messages).
// SquatchPose is what he's doing besides walking - blinking, talking, waving - and
// defaults to plain walking, which is all the screen transitions ask for.
//
// In the Halloween theme (theme.scene) he's dressed up: spooky::costume() says as what,
// and it is a different one after every restart.

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

// Halloween: what he's dressed as and where he's walking, chosen at start-up.
namespace spooky {
enum : uint8_t { ZOMBIE = 0, WITCH, VAMPIRE, GHOST, PUMPKIN, SKELETON, MUMMY, COSTUMES };
enum : uint8_t { STREET = 0, GRAVEYARD, PATCH, WOODS, PLACES };
inline uint8_t& costume() { static uint8_t v = ZOMBIE; return v; }
inline uint8_t& place() { static uint8_t v = STREET; return v; }
// The n-th start: both move on by one, so it takes COSTUMES x PLACES starts to see
// the same costume in the same place again.
inline void pick(uint32_t n) { costume() = n % COSTUMES; place() = n % PLACES; }
inline const char* costumeName(uint8_t c) {
    static const char* const N[COSTUMES] = {"zombie", "witch", "vampire", "ghost", "pumpkin", "skeleton", "mummy"};
    return N[c % COSTUMES];
}
inline const char* placeName(uint8_t p) {
    static const char* const N[PLACES] = {"street", "graveyard", "patch", "woods"};
    return N[p % PLACES];
}
}  // namespace spooky

namespace mascot {

inline uint16_t rgb(uint32_t hex) { return lgfx::color565(hex >> 16, (hex >> 8) & 0xFF, hex & 0xFF); }

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

    // Dressed up for Halloween, or 255: himself.
    const uint8_t dress = t.scene == SCENE_HALLOWEEN ? spooky::costume() : 255;
    const bool zombie = dress == spooky::ZOMBIE, ghost = dress == spooky::GHOST, bones = dress == spooky::SKELETON;
    const bool mummy = dress == spooky::MUMMY;
    const uint16_t sheet = rgb(0xe6eaf2), sheetShade = rgb(0xb4bccd), bone = rgb(0xf2efe2), wrap = rgb(0x9c9072);

    const uint16_t body = zombie ? rgb(0x6f9a52) : bones ? rgb(0x2b2535) : mummy ? rgb(0xd8ceb0) : t.greenDim;
    uint16_t nearC = mix(body, 0xFFFF, 0.2f);            // near limbs catch the light
    uint16_t farC = mix(body, t.bg, 0.45f);              // far limbs in shadow
    if (bones) { nearC = mix(body, 0xFFFF, 0.08f); farC = mix(body, t.bg, 0.3f); }
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
        if (bones) {                                      // the bones painted on his suit
            d.drawWideLine(x - S(4), (int)(hipY + Sf(4)), (int)kx, (int)ky, Sf(1.4f), bone);
            d.drawWideLine((int)kx, (int)ky, (int)fx, (int)fy, Sf(1.4f), bone);
            d.fillCircle((int)kx, (int)ky, max(1, S(2)), bone);
        }
        if (mummy)                                        // the wraps go round
            for (int k = 1; k <= 3; k++) {
                const float f = k / 4.0f, wx = lerp(kx, fx, f), wy = lerp(ky, fy, f);
                d.drawLine((int)(wx - Sf(3)), (int)(wy - Sf(1)), (int)(wx + Sf(3)), (int)(wy + Sf(1)), wrap);
            }
    };
    // Arm from the shoulder: hanging and swinging, or raised (wave / armsUp).
    const float shx = x + Sf(7), shy = shY + Sf(5);
    auto arm = [&](float sw, float wave, float up, bool nearSide, uint16_t c) {
        // Walking: the arm hangs past the hip and swings.
        float ex = x + Sf(8) + sw * Sf(10), ey = hipY + Sf(2);
        float hx = x + Sf(6) + sw * Sf(18), hy = hipY + Sf(20);
        if (zombie) {                                     // both arms out in front of him, hands hanging
            ex = x + Sf(20); ey = shY + Sf(nearSide ? 9 : 6);
            hx = x + Sf(35); hy = shY + Sf(nearSide ? 11 : 8) + sinf(phase * 2 + (nearSide ? 0 : 1.5f)) * Sf(1.5f);
        }
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
        if (bones) {
            d.drawWideLine((int)shx, (int)shy, (int)ex, (int)ey, Sf(1.3f), bone);
            d.drawWideLine((int)ex, (int)ey, (int)hx, (int)hy, Sf(1.3f), bone);
            d.fillCircle((int)hx, (int)hy, max(1, S(2)), bone);
        }
        if (mummy) {
            for (int k = 1; k <= 2; k++) {
                const float f = k / 3.0f, wx = lerp(ex, hx, f), wy = lerp(ey, hy, f);
                d.drawLine((int)(wx - Sf(3)), (int)(wy - Sf(1)), (int)(wx + Sf(3)), (int)(wy + Sf(1)), wrap);
            }
            if (nearSide)                                 // a loose end trailing off his wrist
                d.drawWideLine((int)hx, (int)hy, (int)(hx - Sf(7)), (int)(hy + Sf(8) + sinf(phase) * Sf(2)), Sf(1.6f), body);
        }
        // His sweets pail, in the near hand while it's down: a little pumpkin with a handle.
        if (dress != 255 && nearSide && !zombie && wave <= 0 && up <= 0) {
            const int px = (int)hx, py = (int)(hy + Sf(8));
            const uint16_t pail = rgb(0xff8a1f), pailDark = rgb(0xb4560c);
            d.drawLine(px - S(4), py - S(2), px, (int)hy, pailDark);
            d.drawLine(px + S(4), py - S(2), px, (int)hy, pailDark);
            d.fillEllipse(px, py + S(1), max(3, S(5)), max(3, S(4)), pail);
            d.drawFastVLine(px, py - S(2), max(4, S(7)), pailDark);
            d.fillRect(px - S(3), py, max(1, S(1.5f)), max(1, S(1.5f)), rgb(0x2a1206));
            d.fillRect(px + S(2), py, max(1, S(1.5f)), max(1, S(1.5f)), rgb(0x2a1206));
        }
    };

    // Far side first: arm and leg in shadow, behind the body.
    const float armFar = a, armNear = -a;                 // arms swing against the legs
    if (!ghost || pose.armsUp > 0) arm(armFar, 0, pose.armsUp, false, ghost ? sheetShade : farC);   // under the sheet otherwise
    leg(-a, fmaxf(0, sn) * Sf(6), farC);
    leg(a, fmaxf(0, -sn) * Sf(6), nearC);
    const float capeSway = sinf(phase * 0.5f) * Sf(3);
    if (dress == spooky::VAMPIRE) {                       // the cape, behind him: dark outside, red inside
        const int ax = (int)(x + Sf(4)), ay = (int)(shY - Sf(3));
        d.fillTriangle(ax, ay, (int)(x - Sf(34) + capeSway), (int)(gy - Sf(10)), (int)(x + Sf(2)), (int)(gy - Sf(14)), rgb(0x3a1030));
        d.fillTriangle(ax, ay + S(3), (int)(x - Sf(28) + capeSway), (int)(gy - Sf(13)), (int)(x - Sf(2)), (int)(gy - Sf(17)), rgb(0xb3142e));
    }
    if (dress == spooky::WITCH)                           // a short cape off the shoulders
        d.fillTriangle((int)(x + Sf(2)), (int)(shY - Sf(2)), (int)(x - Sf(30) + capeSway), (int)(hipY + Sf(14)),
                       (int)(x - Sf(4)), (int)(hipY + Sf(6)), rgb(0x5b2d8a));

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

    // ---- what he's wearing, over all that ----
    const uint16_t ink = rgb(0x1a1024);                   // holes, sockets, carved shadows
    if (zombie) {
        // A torn shirt, a scar across his brow, and a green drip off his chin.
        const uint16_t rag = rgb(0x5a4a78), ragDark = rgb(0x3d3154);
        d.fillTriangle((int)(x - Sf(14)), (int)(shY + Sf(14)), (int)(x + Sf(13)), (int)(shY + Sf(12)), (int)(x - Sf(2)), (int)(hipY + Sf(4)), rag);
        d.fillTriangle((int)(x - Sf(14)), (int)(shY + Sf(14)), (int)(x - Sf(13)), (int)(hipY + Sf(3)), (int)(x - Sf(4)), (int)(hipY - Sf(3)), rag);
        d.fillTriangle((int)(x + Sf(13)), (int)(shY + Sf(12)), (int)(x + Sf(11)), (int)(hipY + Sf(1)), (int)(x + Sf(3)), (int)(hipY - Sf(5)), rag);
        d.drawLine((int)(x - Sf(8)), (int)(shY + Sf(18)), (int)(x - Sf(3)), (int)(shY + Sf(27)), ragDark);
        d.drawLine((int)(x + Sf(4)), (int)(shY + Sf(17)), (int)(x + Sf(7)), (int)(shY + Sf(25)), ragDark);
        const int sx0 = (int)(hx - Sf(6)), sy0 = (int)(hy - Sf(7));
        d.drawLine(sx0, sy0, sx0 + S(8), sy0 + S(2), dark);
        for (int k = 0; k < 3; k++) d.drawLine(sx0 + S(1.5f + k * 2.5f), sy0 - S(1), sx0 + S(1 + k * 2.5f), sy0 + S(3), dark);
        d.drawFastVLine((int)(hx + Sf(7)), (int)(hy + Sf(9)), max(2, S(3)), rgb(0xb8f24a));
    }
    if (bones) {
        // Ribs, a spine and a hip bone on the suit, and a skull mask.
        const float rx = x + Sf(2);
        d.drawWideLine((int)rx, (int)(shY + Sf(8)), (int)(x - Sf(1)), (int)(hipY - Sf(1)), Sf(1.6f), bone);
        for (int k = 0; k < 4; k++) {
            const float ry = shY + Sf(11 + k * 5.5f), half = Sf(10 - k * 1.2f);
            d.drawLine((int)(rx - half), (int)(ry + Sf(2)), (int)rx, (int)ry, bone);
            d.drawLine((int)rx, (int)ry, (int)(rx + half), (int)(ry + Sf(1.5f)), bone);
        }
        d.fillEllipse((int)(x - Sf(2)), (int)(hipY + Sf(1)), S(8), max(1, S(2.5f)), bone);
        d.fillEllipse((int)(hx + Sf(3)), (int)(hy + Sf(1)), S(8), S(8), bone);            // the skull
        d.fillRect((int)(hx + Sf(3)), (int)(hy + Sf(6)), S(8), max(2, S(4)), bone);       // its jaw
        if (pose.blink) d.drawFastHLine((int)(hx + Sf(3)), (int)(hy + Sf(1)), S(5), ink);
        else { d.fillCircle((int)(hx + Sf(5)), (int)hy, max(2, S(2.6f)), ink); d.fillCircle((int)(hx + Sf(5)), (int)hy, max(1, S(1)), eyeColor); }
        d.fillTriangle((int)(hx + Sf(9)), (int)(hy + Sf(5)), (int)(hx + Sf(10)), (int)(hy + Sf(2)), (int)(hx + Sf(11)), (int)(hy + Sf(5)), ink);
        const int jaw = (int)(hy + Sf(7)) + (pose.mouth ? S(1) : 0);
        d.drawFastHLine((int)(hx + Sf(4)), jaw, S(7), ink);
        for (int k = 0; k < 3; k++) d.drawFastVLine((int)(hx + Sf(5.5f + k * 2)), jaw - S(1), max(2, S(3)), ink);
    }
    if (mummy) {
        // Wrapped head to hip: bands across him, and his eye looking out of a dark slit.
        for (int k = 0; k < 6; k++) {
            const float wy = shY + Sf(6 + k * 6.5f), tilt = (k % 2 ? 1 : -1) * Sf(2.5f);
            d.drawLine((int)(x - Sf(17)), (int)(wy - tilt), (int)(x + Sf(15)), (int)(wy + tilt), wrap);
        }
        d.fillEllipse((int)(hx + Sf(3)), (int)(hy + Sf(1)), S(9), S(8), body);
        d.drawLine((int)(hx - Sf(6)), (int)(hy - Sf(5)), (int)(hx + Sf(10)), (int)(hy - Sf(7)), wrap);
        d.drawLine((int)(hx - Sf(6)), (int)(hy + Sf(6)), (int)(hx + Sf(11)), (int)(hy + Sf(4)), wrap);
        d.fillRect((int)(hx - Sf(1)), (int)(hy - Sf(2)), S(12), max(3, S(5)), ink);
        if (pose.blink) d.drawFastHLine(ex - er, ey + 1, 2 * er + 1, wrap);
        else d.fillCircle(ex, ey + S(0.5f), er, eyeColor);
        if (pose.mouth) d.fillEllipse(mx, my, max(1, S(2)), max(1, S(1.5f)), ink);
        tuft(d, hx - Sf(8), hy + Sf(3), -1.0f, 1.2f, Sf(9), Sf(1.6f), body);              // a loose end off the back of his head
    }
    if (dress == spooky::VAMPIRE) {
        // The collar standing up behind his head, a bow tie, and fangs.
        const uint16_t red = rgb(0xb3142e);
        d.fillTriangle((int)(hx - Sf(13)), (int)(hy + Sf(10)), (int)(hx - Sf(15)), (int)(hy - Sf(9)), (int)(hx - Sf(5)), (int)(hy + Sf(7)), red);
        d.fillTriangle((int)(x + Sf(12)), (int)(shY + Sf(6)), (int)(x + Sf(18)), (int)(shY + Sf(3)), (int)(x + Sf(18)), (int)(shY + Sf(9)), red);
        d.fillTriangle((int)(mx - S(2)), my, (int)(mx - S(1)), my + max(2, S(3)), mx, my, 0xFFFF);
        d.fillTriangle((int)(mx + S(1)), my - 1, (int)(mx + S(2)), my + max(2, S(3)) - 1, (int)(mx + S(3)), my - 1, 0xFFFF);
    }
    if (dress == spooky::WITCH) {
        // A pointed hat with a bent tip, an orange band and a buckle.
        const uint16_t hat = rgb(0x5b2d8a), hatLight = rgb(0x8a55c0);
        const float by = hy - Sf(7);
        d.fillTriangle((int)(hx - Sf(10)), (int)by, (int)(hx + Sf(9)), (int)(by - Sf(1)), (int)(hx - Sf(5)), (int)(by - Sf(24)), hat);
        d.fillTriangle((int)(hx - Sf(7)), (int)(by - Sf(17)), (int)(hx - Sf(4)), (int)(by - Sf(25)), (int)(hx - Sf(15)), (int)(by - Sf(26)), hat);
        d.fillEllipse((int)(hx - Sf(1)), (int)by, S(17), max(2, S(3)), hat);
        d.drawFastHLine((int)(hx - Sf(17)), (int)by - max(1, S(1.5f)), S(30), hatLight);
        d.drawWideLine((int)(hx - Sf(8)), (int)(by - Sf(5)), (int)(hx + Sf(6)), (int)(by - Sf(5.5f)), Sf(1.8f), rgb(0xff8a1f));
        d.fillRect((int)(hx - Sf(2)), (int)(by - Sf(7.5f)), max(3, S(4)), max(3, S(4)), rgb(0xffd866));
    }
    if (dress == spooky::PUMPKIN) {
        // A carved pumpkin over his head, lit from inside.
        const uint16_t pk = rgb(0xff8a1f), pkDark = rgb(0xc45c0e), glow = rgb(0xffe27a);
        const int px = (int)(hx + Sf(1)), py = (int)(hy - Sf(1));
        d.fillEllipse(px, py, S(14), S(12), pk);
        d.drawEllipse(px, py, S(7), S(12), pkDark);
        d.drawEllipse(px, py, S(14), S(12), pkDark);
        d.fillRect(px - S(1), py - S(15), max(2, S(3)), max(3, S(4)), rgb(0x4e7a2a));
        const int e1 = px + S(1), e2 = px + S(8), eyY = py - S(2);
        if (pose.blink) { d.drawFastHLine(e1 - S(2), eyY, S(4), ink); d.drawFastHLine(e2 - S(2), eyY, S(4), ink); }
        else {
            d.fillTriangle(e1 - S(3), eyY + S(1), e1, eyY - S(4), e1 + S(3), eyY + S(1), glow);
            d.fillTriangle(e2 - S(2), eyY + S(1), e2, eyY - S(4), e2 + S(2), eyY + S(1), glow);
        }
        const int gy0 = py + S(4), open = pose.mouth ? S(3) : S(1);       // a jagged grin
        for (int k = 0; k < 4; k++) {
            const int gx = px - S(4) + k * S(4);
            d.fillTriangle(gx, gy0, gx + S(4), gy0, gx + S(2), gy0 + S(3) + open, glow);
        }
        d.drawFastHLine(px - S(4), gy0, S(16), glow);
    }
    if (ghost) {
        // A bedsheet over the lot of him, with two holes to see through; only his shins show.
        const float hem = hipY + Sf(22);
        const float lx0 = x - Sf(29), rx0 = x + Sf(21), top = shY - Sf(3);
        d.fillCircle((int)(hx - Sf(1)), (int)(hy - Sf(1)), S(13), sheet);
        d.fillTriangle((int)lx0, (int)top, (int)rx0, (int)top, (int)(x - Sf(31)), (int)hem, sheet);
        d.fillTriangle((int)rx0, (int)top, (int)(x - Sf(31)), (int)hem, (int)(x + Sf(23)), (int)hem, sheet);
        d.fillCircle((int)(x - Sf(14)), (int)(shY + Sf(6)), S(15), sheet);       // over the hump
        d.fillTriangle((int)(hx - Sf(13)), (int)hy, (int)lx0, (int)top + S(4), (int)(hx + Sf(11)), (int)hy, sheet);
        for (int k = 0; k < 6; k++) {                     // the hem, in points, swaying
            const float w = Sf(54) / 6, bx = x - Sf(31) + k * w;
            d.fillTriangle((int)bx, (int)hem - 1, (int)(bx + w + 1), (int)hem - 1,
                           (int)(bx + w / 2 + sinf(phase * 0.5f + k) * Sf(1.5f)), (int)(hem + Sf(6)), sheet);
        }
        for (int k = 0; k < 3; k++) {                     // folds
            const float fx0 = x - Sf(18) + k * Sf(14);
            d.drawLine((int)fx0, (int)(shY + Sf(16)), (int)(fx0 - Sf(2)), (int)(hem + Sf(1)), sheetShade);
        }
        const int e1 = (int)(hx + Sf(1)), e2 = (int)(hx + Sf(8)), eyY = (int)(hy - Sf(1));
        if (pose.blink) { d.drawFastHLine(e1 - S(2), eyY, S(4), ink); d.drawFastHLine(e2 - S(2), eyY, S(4), ink); }
        else {
            d.fillEllipse(e1, eyY, max(2, S(2.2f)), max(2, S(3)), ink); d.fillEllipse(e2, eyY, max(2, S(2)), max(2, S(3)), ink);
            d.drawPixel(e1 + 1, eyY, eyeColor); d.drawPixel(e2 + 1, eyY, eyeColor);
        }
        d.fillEllipse((int)(hx + Sf(5)), (int)(hy + Sf(6)), max(1, S(2)), pose.mouth ? max(2, S(3)) : max(1, S(1.5f)), ink);
    }

    // Near arm last: it swings across the body.
    arm(armNear, pose.wave, pose.armsUp, true, ghost ? sheet : nearC);

    if (pose.dizzy >= 0)                                  // stars circling his head
        for (int k = 0; k < 3; k++) {
            const float ang = pose.dizzy + k * 2.0944f;
            const int sx = (int)(hx + cosf(ang) * Sf(13)), sy = (int)(hy - Sf(15) + sinf(ang) * Sf(4));
            star(d, sx, sy, max(1, S(2.5f)), sinf(ang) < 0 ? mix(t.amber, t.bg, 0.4f) : t.amber);
        }
}
