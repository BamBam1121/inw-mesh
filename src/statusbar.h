#pragma once
#include "display_config.h"
#include "theme.h"

void drawStatusBar(lgfx::LovyanGFX& d, const Theme& t);
// Call every loop: repaints just the battery corner while plugged in.
void animateBatteryIcon(lgfx::LovyanGFX* panel, const Theme& t);
