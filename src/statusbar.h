#pragma once
#include "display_config.h"
#include "theme.h"

// withClock false: the lock face, whose big clock already says the time.
void drawStatusBar(lgfx::LovyanGFX& d, const Theme& t, bool withClock = true);
// The lock face's larger signal bars, top right under the status bar (nothing when
// the bars are turned off). Draw it after the scene.
void drawLockSignal(lgfx::LovyanGFX& d, const Theme& t);
// Call every loop: repaints just the small bars while they animate.
void animateSignalIcon(lgfx::LovyanGFX* panel, const Theme& t);
// Call every loop: repaints just the battery corner while plugged in.
void animateBatteryIcon(lgfx::LovyanGFX* panel, const Theme& t);
