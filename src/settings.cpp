#include "settings.h"
#include <Preferences.h>
#include <SPIFFS.h>
#include <SD.h>
#include <stddef.h>

UiSettings ui_settings;

// NVS holds the live copy, but it is the ONE copy: anything that clears NVS -
// running other firmware on this board, a corrupt page ESP-IDF decides to wipe,
// an erase - took every preference with it and there was nothing to restore
// from. Contacts, channels, messages and the identity all have two or three
// copies; these now do too. MIRROR is written beside the mesh stores and
// mirrored to SD by the usual backup.
static const char* MIRROR = "/ui.bin";
static bool s_fromNvs = false;

// A saved copy from an older build is a prefix of ours (fields are only ever
// appended), but its length includes the old struct's tail padding, which must not
// land on fields added since. So a shorter copy is taken only up to the first field
// of the newest release it could hold: each entry below is where a release began
// adding. (Getting this wrong resets the settings added in between, on update.)
static void adopt(UiSettings* s, const uint8_t* buf, size_t len) {
  size_t n = len;
  if (len < sizeof(UiSettings)) {
    // orient: the T-Deck's, after 1.2.2's fields. Without it a pager's 1.2.2
    // settings read here lost everything from tzZone on (a pager given T-Deck
    // firmware by mistake came back without its time zone).
    static const size_t ADDED[] = {offsetof(UiSettings, wifiOn), offsetof(UiSettings, tzZone),
                                   offsetof(UiSettings, orient), offsetof(UiSettings, squatchQuiet)};
    for (size_t b : ADDED) if (len >= b) n = b;
  }
  memcpy((void*)s, buf, min(n, sizeof(UiSettings)));
}

// One blob: forty keys would be forty flash writes every time a toggle moves.
// A version mismatch falls back to defaults, which beats misreading.
void UiSettings::load() {
  Preferences p;
  if (!p.begin("inw-ui", true)) return;
  // A shorter blob is an older build: its fields are a prefix of ours (new ones
  // are only ever appended), so read what's there and keep defaults for the rest.
  // A longer one is a newer build's, or another board's (the T-Deck's has fields
  // after ours): ours are a prefix of it, so adopt() takes those. Turning it away
  // would reset every setting on going back to an older release.
  const size_t len = p.getBytesLength("blob");
  if (p.getUChar("ver", 0) == VERSION && len > 0 && len <= 4096) {
    uint8_t* buf = (uint8_t*)malloc(len);
    if (buf) {
      p.getBytes("blob", buf, len);
      adopt(this, buf, len);
      free(buf);
      s_fromNvs = true;
    }
  }
  p.end();
}

void UiSettings::save() {
  Preferences p;
  if (!p.begin("inw-ui", false)) return;
  p.putUChar("ver", VERSION);
  p.putBytes("blob", this, sizeof(UiSettings));
  p.end();
  saveMirror();
}

// Same bytes as the NVS blob, one version byte in front. Only written when it
// changed: the mirror is flash too, and save() runs on every settings edit.
bool inwQueueReplace(const char* path, const uint8_t* data, size_t len);   // tools/patch_meshcore.py
void UiSettings::saveMirror() {
  static uint8_t last[sizeof(UiSettings) + 1];
  static bool have = false;
  uint8_t buf[sizeof(UiSettings) + 1];
  buf[0] = VERSION;
  memcpy(buf + 1, this, sizeof(UiSettings));
  if (have && !memcmp(last, buf, sizeof(buf))) return;
  // Written by the background store task: an open by name on this SPIFFS scans
  // the partition, and save() runs on every settings edit and on leaving the map.
  if (inwQueueReplace(MIRROR, buf, sizeof(buf))) { memcpy(last, buf, sizeof(buf)); have = true; return; }
  File f = SPIFFS.open(MIRROR, FILE_WRITE);
  if (!f) return;
  if (f.write(buf, sizeof(buf)) == sizeof(buf)) { memcpy(last, buf, sizeof(buf)); have = true; }
  f.close();
}

bool UiSettings::cameFromNvs() { return s_fromNvs; }

// Called once storage is up (load() runs before SPIFFS is mounted, because the
// screen needs a theme first). Only touches anything when NVS came up empty.
const char* UiSettings::restoreIfWiped(bool sdReady) {
  if (s_fromNvs) return nullptr;
  uint8_t buf[sizeof(UiSettings) + 1];
  const char* from = nullptr;
  // As in load(): a longer copy (a newer build's) is read as far as our fields go.
  File f = SPIFFS.open(MIRROR, FILE_READ);
  size_t n = f ? min((size_t)f.size(), sizeof(buf)) : 0;
  if (f && n >= 2 && f.read(buf, n) == (int)n && buf[0] == VERSION) {
    adopt(this, buf + 1, n - 1);
    from = "flash copy";
  }
  if (f) f.close();
  if (!from && sdReady) {
    File g = SD.open("/inw/ui.bin", FILE_READ);
    n = g ? min((size_t)g.size(), sizeof(buf)) : 0;
    if (g && n >= 2 && g.read(buf, n) == (int)n && buf[0] == VERSION) {
      adopt(this, buf + 1, n - 1);
      from = "sd card";
    }
    if (g) g.close();
  }
  if (from) { save(); s_fromNvs = true; }    // back into NVS so it survives the next boot
  return from;
}

// Read by node.cpp, which should not depend on the UI headers.
bool inwAutoRetry()     { return ui_settings.autoRetry; }
bool inwAutoResetPath() { return ui_settings.autoResetPath; }
bool inwBleWanted()     { return ui_settings.ble; }
