#pragma once
#include "display_config.h"
#include "theme.h"

// withClock false: the lock face, whose big clock already says the time.
void drawStatusBar(lgfx::LovyanGFX& d, const Theme& t, bool withClock = true);
// Call every loop: repaints just the battery corner while plugged in.
void animateBatteryIcon(lgfx::LovyanGFX* panel, const Theme& t);
