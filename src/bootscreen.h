// The boot and power-off screens: the mesh mark, the wordmark and the progress
// bar. Side by side on the pager's wide screen; stacked on a narrower one (the
// T-Deck's 320 x 240). main.cpp runs them; here so the simulator can draw them too.
#pragma once
#include "ui.h"

namespace boot {

constexpr bool STACKED = L::W < 400;

// The mark's five nodes are drawn at these coordinates plus (MARK_DX, MARK_DY).
constexpr int MARK_DX = STACKED ? L::W / 2 - 113 : 0;
constexpr int MARK_DY = STACKED ? -16 : 0;
// The box the animated mark is redrawn in, on the screen.
constexpr int MARK_X = 54 + MARK_DX, MARK_Y = 28 + MARK_DY, MARK_W = 124, MARK_H = 118;

constexpr int BAR_X = STACKED ? 40 : 90, BAR_W = STACKED ? L::W - 80 : 300, BAR_Y = STACKED ? 198 : 184;
constexpr int CAPTION_Y = STACKED ? 176 : 146;
constexpr int ERR_Y0 = STACKED ? 208 : 196;   // where failed steps are listed, one line each

// The mesh mark. ox/oy shift it (into a sprite); animate: a packet hops round it.
void drawMark(lgfx::LovyanGFX& g, int ox, int oy, uint32_t ms, bool animate);
// The whole still boot screen.
void drawLogo(lgfx::LovyanGFX& g);
// Power off: brk < 0 still saving (the mark animating), brk >= 0 ms into the teardown.
void drawGoodbye(Canvas& g, uint32_t ms, int32_t brk);
constexpr int32_t BREAK_MS = 980;

}  // namespace boot
