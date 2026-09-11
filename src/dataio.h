// Restoring, backing up and exporting the node's data.
//
// On boot, a missing or torn store file is put back from the flash .bak copy,
// then the SD mirror (/inw), then a Wadamesh store (/meshcomod) if one is on the
// card - it uses MeshCore's own file formats, so the files copy across as-is.
// While running, the store is backed up to flash and SD once a day.

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
// The automatic run passes force=false: it will not replace a backup with a
// store that lost more than a tenth of its records (a torn save looks like that).
const char* sdBackupNow(bool force = true);
const char* exportJson();
const char* importJsonNow();     // merge contacts/channels from the newest export
// Copies the identity, channels and mesh prefs into NVS, which survives a
// partition layout change. Cheap when nothing changed.
void keepEssentials();
void sdBackupTick();             // call from loop; runs sdBackupNow(false) once a day
