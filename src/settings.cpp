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

// One blob: forty keys would be forty flash writes every time a toggle moves.
// A size or version mismatch falls back to defaults, which beats misreading.
void UiSettings::load() {
  Preferences p;
  if (!p.begin("inw-ui", true)) return;
  // A shorter blob is an older build: its fields are a prefix of ours (new ones
  // are only ever appended), so read what's there and keep defaults for the rest.
  const size_t len = p.getBytesLength("blob");
  if (p.getUChar("ver", 0) == VERSION && len > 0 && len <= sizeof(UiSettings)) {
    uint8_t* buf = (uint8_t*)malloc(len);
    if (buf) {
      p.getBytes("blob", buf, len);
      // An older blob's trailing padding must not land on the new fields.
      const size_t firstNew = offsetof(UiSettings, wifiOn);
      memcpy((void*)this, buf, len < sizeof(UiSettings) ? min(len, firstNew) : len);
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
void UiSettings::saveMirror() {
  static uint8_t last[sizeof(UiSettings) + 1];
  static bool have = false;
  uint8_t buf[sizeof(UiSettings) + 1];
  buf[0] = VERSION;
  memcpy(buf + 1, this, sizeof(UiSettings));
  if (have && !memcmp(last, buf, sizeof(buf))) return;
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
  File f = SPIFFS.open(MIRROR, FILE_READ);
  if (f && f.size() >= 2 && f.size() <= sizeof(buf) && f.read(buf, f.size()) == (int)f.size() && buf[0] == VERSION) {
    memcpy((void*)this, buf + 1, min(f.size() - 1, sizeof(UiSettings)));
    from = "flash copy";
  }
  if (f) f.close();
  if (!from && sdReady) {
    File g = SD.open("/inw/ui.bin", FILE_READ);
    if (g && g.size() >= 2 && g.size() <= sizeof(buf) && g.read(buf, g.size()) == (int)g.size() && buf[0] == VERSION) {
      memcpy((void*)this, buf + 1, min(g.size() - 1, sizeof(UiSettings)));
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
