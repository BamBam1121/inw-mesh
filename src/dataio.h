// Restoring, backing up and exporting the node's data.
//
// On boot, a missing or torn store file is put back from the flash .bak copy,
// then the SD mirror (/inw), then a Wadamesh store (/meshcomod) if one is on the
// card - it uses MeshCore's own file formats, so the files copy across as-is.
// While running, the store is mirrored to SD every 30 minutes.

#pragma once
#include <Arduino.h>

bool sdMount();
bool sdMounted();
uint64_t sdFreeBytes();

// Before nodeBegin(): make sure the SPIFFS store has everything it should.
// Writes a human-readable summary line into `report`.
void importBeforeNode(char* report, size_t cap);
// After nodeBegin(): apply name/radio/position from a JSON export once.
void importPrefsAfterNode(char* report, size_t cap);

// Manual actions from Settings > Backups. Return a short status for a toast.
const char* sdBackupNow();
const char* exportJson();
const char* importJsonNow();     // merge contacts/channels from the newest export
void sdBackupTick();             // call from loop
void localBackupTick();          // flash-side last-good copies
