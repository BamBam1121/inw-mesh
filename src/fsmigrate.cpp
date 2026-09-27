#include "fsmigrate.h"
#include "board_pins.h"

#ifndef MESHCORE_FS_OFFSET

namespace fsmigrate {
int rescue() { return 0; }
int restore() { return 0; }
const char* summary() { return ""; }
}

#else

#include <Arduino.h>
#include <SPIFFS.h>
#include <esp_partition.h>
#include <esp_heap_caps.h>
#include <Preferences.h>

namespace fsmigrate {
namespace {

struct Found {
  const char* path;
  uint8_t* data;
  size_t len;
};
// MeshCore's DataStore files (examples/companion_radio/DataStore.cpp). Settings
// are prefs.json in current MeshCore and new_prefs before it; our DataStore reads
// either, preferring prefs.json.
Found s_files[] = {
  {"/identity/_main.id", nullptr, 0},
  {"/contacts3", nullptr, 0},
  {"/channels2", nullptr, 0},
  {"/prefs.json", nullptr, 0},
  {"/new_prefs", nullptr, 0},
};
constexpr size_t MAX_FILE = 512 * 1024;
constexpr size_t CONTACT_REC = 152, CHANNEL_REC = 68;   // as dataio.cpp
char s_summary[64] = "";

Found* find(const char* path) {
  for (auto& f : s_files) if (!strcmp(f.path, path)) return &f;
  return nullptr;
}

void drop(Found& f) {
  if (f.data) { memset(f.data, 0, f.len); free(f.data); }   // the identity is a private key
  f.data = nullptr;
  f.len = 0;
}

// Our store's partition entry, pointed at MeshCore's store for as long as it takes
// to read it. SPIFFS only mounts a partition, and the partition API won't add a
// second one over flash that ours already covers. Nothing has ours open yet (this
// runs only when it wouldn't mount), and the entry is put back before anything else
// can look at it - the destructor runs before our store is formatted.
struct Retarget {
  esp_partition_t* p;
  uint32_t address, size;
  Retarget(esp_partition_t* part, uint32_t a, uint32_t s) : p(part), address(part->address), size(part->size) {
    p->address = a;
    p->size = s;
  }
  ~Retarget() { p->address = address; p->size = size; }
};

}  // namespace

// A mark in NVS for the length of the read. If reading another firmware's store
// ever brought the board down, the next start must not try again - it would never
// get as far as formatting ours - so a mark still set means: skip it.
static bool markTrying(bool on) {
  Preferences p;
  if (!p.begin("inw-mig", false)) return false;
  const bool was = p.getUChar("trying", 0);
  if (on) p.putUChar("trying", 1);
  else p.remove("trying");
  p.end();
  return was;
}

int rescue() {
  auto* part = const_cast<esp_partition_t*>(
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr));
  // MeshCore's store must lie wholly inside ours, or our format wouldn't reach it -
  // and then there is nothing to rescue it from.
  if (!part || MESHCORE_FS_OFFSET < part->address ||
      MESHCORE_FS_OFFSET + MESHCORE_FS_SIZE > part->address + part->size) return 0;
  if (markTrying(true)) {
    markTrying(false);              // one skip only: the format that follows ends it
    Serial.println("[migrate] the last try didn't finish: skipped");
    return 0;
  }
  int found = 0;
  {
    Retarget there(part, MESHCORE_FS_OFFSET, MESHCORE_FS_SIZE);
    // Not formatted if it doesn't mount: that is Meshtastic's LittleFS, or nothing.
    if (!SPIFFS.begin(false, "/mcold", 4, part->label)) {
      markTrying(false);
      Serial.println("[migrate] no MeshCore store in flash");
      return 0;
    }
    for (auto& f : s_files) {
      File in = SPIFFS.open(f.path, FILE_READ);
      if (!in) continue;
      const size_t len = in.size();
      if (len && len <= MAX_FILE) {
        f.data = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!f.data) f.data = (uint8_t*)malloc(len);
        if (f.data && in.read(f.data, len) == len) f.len = len;
        else drop(f);
      }
      in.close();
    }
    SPIFFS.end();
  }
  markTrying(false);

  // Only what reads as MeshCore's: a key of the right size, whole records.
  Found* id = find("/identity/_main.id");
  if (id->data && id->len < 96) drop(*id);
  Found* ct = find("/contacts3");
  if (ct->data && ct->len % CONTACT_REC) drop(*ct);
  Found* ch = find("/channels2");
  if (ch->data && ch->len % CHANNEL_REC) drop(*ch);

  int named = 0;
  if (ch->data)
    for (size_t o = 0; o + CHANNEL_REC <= ch->len; o += CHANNEL_REC) named += ch->data[o + 4] != 0;   // 4 unused, then the name
  const bool radio = find("/prefs.json")->data || find("/new_prefs")->data;
  size_t w = 0;
  s_summary[0] = 0;
  auto add = [&](const char* s) { w += snprintf(s_summary + w, sizeof(s_summary) - w, "%s%s", w ? ", " : "", s); };
  char tmp[24];
  if (id->data) add("key");
  if (ct->data) { snprintf(tmp, sizeof(tmp), "%u contacts", (unsigned)(ct->len / CONTACT_REC)); add(tmp); }
  if (ch->data) { snprintf(tmp, sizeof(tmp), "%d channels", named); add(tmp); }
  if (radio) add("radio");
  for (auto& f : s_files) found += f.data != nullptr;
  Serial.printf("[migrate] MeshCore store: %s\n", found ? s_summary : "nothing usable");
  return found;
}

int restore() {
  int written = 0;
  for (auto& f : s_files) {
    if (!f.data) continue;
    File out = SPIFFS.open(f.path, FILE_WRITE);
    if (out && out.write(f.data, f.len) == f.len) written++;
    if (out) out.close();
    drop(f);
  }
  Serial.printf("[migrate] %d files written into our store\n", written);
  return written;
}

const char* summary() { return s_summary; }

}  // namespace fsmigrate

#endif
