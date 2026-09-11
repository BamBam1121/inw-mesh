#include "notify.h"
#include <Preferences.h>

// NVS keys max out at 15 characters: type digit + 12 hex digits.
static void keyFor(const ConvKey& k, char* out) {
  snprintf(out, 16, "%u%02x%02x%02x%02x%02x%02x", k.type, k.id[0], k.id[1], k.id[2], k.id[3], k.id[4], k.id[5]);
}

// A small cache: a message arrival shouldn't mean an NVS read every time.
struct Cached { ConvKey k; uint8_t mode; };
static Cached s_cache[32];
static uint8_t s_cached = 0;

uint8_t notifyMode(const ConvKey& k) {
  for (uint8_t i = 0; i < s_cached; i++) if (s_cache[i].k == k) return s_cache[i].mode;
  char key[16];
  keyFor(k, key);
  Preferences p;
  p.begin("inw-notify", true);
  const uint8_t mode = p.getUChar(key, NM_DEFAULT);
  p.end();
  if (s_cached < 32) s_cache[s_cached++] = {k, mode};
  else s_cache[esp_random() % 32] = {k, mode};
  return mode < NM_COUNT ? mode : NM_DEFAULT;
}

void setNotifyMode(const ConvKey& k, uint8_t mode) {
  char key[16];
  keyFor(k, key);
  Preferences p;
  p.begin("inw-notify", false);
  if (mode == NM_DEFAULT) p.remove(key); else p.putUChar(key, mode);
  p.end();
  for (uint8_t i = 0; i < s_cached; i++) if (s_cache[i].k == k) { s_cache[i].mode = mode; return; }
  if (s_cached < 32) s_cache[s_cached++] = {k, mode};
}

const char* notifyModeName(uint8_t mode) {
  static const char* N[] = {"default", "all messages", "@mentions only", "silent (banner only)", "muted"};
  return mode < NM_COUNT ? N[mode] : N[0];
}
