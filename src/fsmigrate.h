// Bringing a MeshCore install's identity, contacts, channels and radio settings
// across on the first start, with nothing to export first.
//
// MeshCore (and firmware built like it) keeps them in a SPIFFS store at a fixed
// place in flash. Our installer never writes there, and our own store covers
// that area, so they are still in flash when this firmware first starts - right
// up until our store is formatted over them. So: rescue() reads them into memory
// just before that format, and restore() writes them into our store just after.
// Both stores are MeshCore's own DataStore files, so they are copied as they are.
//
// A board opts in by defining MESHCORE_FS_OFFSET and MESHCORE_FS_SIZE (where
// MeshCore's build for it keeps its store) in its board_pins.h.
#pragma once
#include <stddef.h>

namespace fsmigrate {
// Before formatting our store. How many of MeshCore's files were found.
int rescue();
// After our store is formatted and mounted. How many were written into it.
int restore();
// What came across, for the boot screen: "key, 57 contacts, 3 channels, radio".
const char* summary();
}
