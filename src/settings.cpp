#include "settings.h"
#include <Preferences.h>
#include <stddef.h>

UiSettings ui_settings;

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
}

// Read by node.cpp, which should not depend on the UI headers.
bool inwAutoRetry()     { return ui_settings.autoRetry; }
bool inwAutoResetPath() { return ui_settings.autoResetPath; }
bool inwBleWanted()     { return ui_settings.ble; }
