// T-Deck: two screens for checking a unit on the spot, such as right after the
// first flash. The hardware check lists each part of the board with a dot - green
// works, amber waiting for you to try it, red not found - and updates live as you
// touch, roll, click and type. The touch test has you tap four rings; if the
// finger and the screen disagree it flips the touch mapping to match and says so
// (Settings > Display > screen + touch has the same switches by hand).
#pragma once

void openHardwareCheck();
void openTouchTest();
