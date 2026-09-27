// The simulated board: the T-Deck's pins and capabilities (320x240), or the
// pager's with -DSIM_PAGER, straight from the board folders the firmware builds with.
#pragma once
#ifdef SIM_PAGER
#include "../../src/pager/board_pins.h"
#else
#include "../../src/tdeck/board_pins.h"
#endif
