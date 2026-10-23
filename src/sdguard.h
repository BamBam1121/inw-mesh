// The microSD card some ThinkNode M9s shipped with carries a Windows worm (Elecrow's
// advisory, September 2026: an autorun.inf that starts a hidden program, reported as
// xlfqf.pif / Sality). It can't run here, but people copy backups off this card onto a
// PC. Every start looks at the card's top folder and deletes the worm's own files:
// an autorun.inf that starts a program, the programs it names, and any .pif file. Nothing
// else is touched and the card is never formatted, so maps and backups stay.
//
// When it found something, Squatch deals with it on screen (drawFight).
#pragma once
#include "ui.h"

namespace sdguard {

struct Found {
  uint8_t removed = 0, failed = 0;
  char names[64] = "";          // what was removed: "autorun.inf, xlfqf.pif"
};

// Looks at the mounted card (after sdMount()). Firmware only.
Found sweep();

// Squatch punching and stomping the virus, ms after it began. FIGHT_MS long; a key
// skips it from SKIP_MS on, once he has won.
constexpr uint32_t FIGHT_MS = 7000, SKIP_MS = 3800;
void drawFight(Canvas& g, uint32_t ms, const Found& f);

}  // namespace sdguard
