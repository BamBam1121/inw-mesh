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

// Starting afresh on purpose: the web installer's "start fresh" and "reset
// everything". wipeRequest only leaves a mark, and the caller restarts; the wipe is
// done on that start (wipePending, wipeNow, then another restart), before the node,
// the radio or Wi-Fi are running.
//   level 1: contacts (and the advert MeshCore keeps for each), channels, messages,
//     their region scopes and the per-chat notification rules go. The keys, the name
//     and radio settings, saved Wi-Fi and the device's own settings stay.
//   level 2: everything, the keys, settings and saved Wi-Fi too: the store is
//     formatted and a new identity is made.
// The copies of what is wiped go with it (flash .bak, our SD mirror, the safety copy),
// and nothing is restored or imported from a backup afterwards: the empty store is
// what was asked for, not damage. Exports and dated backups on the card are the
// owner's and are left alone.
bool wipeRequest(bool everything);
uint8_t wipePending();           // 0, or the level asked for
// progress: called every few files while they are removed, for the screen (may be null).
void wipeNow(uint8_t level, void (*progress)(int done, int total) = nullptr);
void wipeDryRun(bool everything);   // what a wipe would remove and keep, printed; touches nothing

// Manual actions from Settings > Backups. Return a short status for a toast.
// The automatic run passes force=false: it will not replace a backup with a
// store that lost more than a tenth of its records (a torn save looks like that).
const char* sdBackupNow(bool force = true);
// Contact/channel saves are written by a background task (tools/patch_meshcore.py).
// Waits until every queued save is on flash; false if that took longer than ms.
bool inwStoreFlush(uint32_t ms);
const char* exportJson();
const char* importJsonNow();     // merge contacts/channels from the newest export
// Adds back any contact found in the flash backup, the SD mirror, the dated SD
// copies or a Wadamesh store that isn't on the node. Only adds, never removes.
const char* recoverMissingContacts();
void storeReport();              // record counts of every contact store, over USB serial
// Copies the identity, channels and mesh prefs into NVS, which survives a
// partition layout change. Cheap when nothing changed.
void keepEssentials();
void sdBackupTick();             // call from loop; runs sdBackupNow(false) once a day
