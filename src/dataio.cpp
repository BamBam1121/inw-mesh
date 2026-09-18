#include "dataio.h"
#include <SD.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <algorithm>
#include <vector>
#include <esp_heap_caps.h>
#include <helpers/IdentityStore.h>
#include "board_pins.h"
#include "node.h"
#include "settings.h"
#include "logstore.h"

#if __has_include("identity_seed.h")
#include "identity_seed.h"   // gitignored: this device's own key, never committed
#endif

extern LogStore logs;

static bool s_sd = false;
static const size_t CONTACT_REC = 152;   // DataStore's /contacts3 record
static const size_t CHANNEL_REC = 68;    // DataStore's /channels2 record
static const char* JSON_PATH = "/meshcore-backup.json";

bool sdMount() {
  if (s_sd) return true;
  inw_spi.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, -1);   // no-op if the radio already did
  s_sd = SD.begin(PIN_SD_CS, inw_spi, 4000000, "/sd", 5, false);
  return s_sd;
}
bool sdMounted() { return s_sd; }
uint64_t sdFreeBytes() { return s_sd ? SD.totalBytes() - SD.usedBytes() : 0; }

void inwProgress(const char* what, uint32_t done, uint32_t total);   // main.cpp
static const char* s_progress = nullptr;   // set while a backup runs: copies show a progress screen

static size_t copyFileImpl(fs::FS& from, const char* src, fs::FS& to, const char* dst);
static size_t copyFile(fs::FS& from, const char* src, fs::FS& to, const char* dst) {
  const uint32_t t0 = millis();
  const size_t n = copyFileImpl(from, src, to, dst);
  if (millis() - t0 > 500) Serial.printf("[copy] %s -> %s: %u kB in %lums\n", src, dst, (unsigned)(n / 1024), (unsigned long)(millis() - t0));
  return n;
}

static size_t copyFileImpl(fs::FS& from, const char* src, fs::FS& to, const char* dst) {
  File in = from.open(src, FILE_READ);
  if (!in) return 0;
  const size_t size = in.size();
  // Write beside the target and swap, so a cut mid-copy never leaves a torn file.
  char tmp[64];
  snprintf(tmp, sizeof(tmp), "%s.new", dst);
  File out = to.open(tmp, FILE_WRITE);
  if (!out) { in.close(); return 0; }
  static uint8_t buf[2048];
  size_t total = 0;
  int n;
  while ((n = in.read(buf, sizeof(buf))) > 0) {
    if (out.write(buf, n) != (size_t)n) { total = 0; break; }
    total += n;
    if (s_progress && size) inwProgress(s_progress, total, size);
  }
  in.close(); out.close();
  if (!total) { to.remove(tmp); return 0; }
  to.remove(dst);
  to.rename(tmp, dst);
  return total;
}

static size_t fileSize(fs::FS& fs, const char* p) {
  if (!fs.exists(p)) return 0;
  File f = fs.open(p, FILE_READ);
  if (!f) return 0;
  const size_t s = f.size();
  f.close();
  return s;
}

static bool validStore(fs::FS& fs, const char* p, size_t rec) {
  const size_t s = fileSize(fs, p);
  return s >= rec && s % rec == 0;
}

// Restore order for a store file: our own flash copy, our SD mirror, then
// Wadamesh's store on the card. Returns a label for what was used, or null.
static const char* restoreStore(const char* name, size_t rec) {
  char live[32], bak[40], mine[40], wada[48];
  snprintf(live, sizeof(live), "/%s", name);
  // A save writes <name>.tmp and then swaps it in. With the live file present the
  // .tmp is a save that never finished; without it, the swap was cut short.
  char tmp[40];
  snprintf(tmp, sizeof(tmp), "/%s.tmp", name);
  if (SPIFFS.exists(tmp)) {
    if (!SPIFFS.exists(live) && validStore(SPIFFS, tmp, rec)) {
      SPIFFS.rename(tmp, live);
      return "finished save";
    }
    SPIFFS.remove(tmp);
  }
  if (validStore(SPIFFS, live, rec)) return nullptr;
  snprintf(bak, sizeof(bak), "/%s.bak", name);
  if (validStore(SPIFFS, bak, rec) && copyFile(SPIFFS, bak, SPIFFS, live)) return "flash backup";
  if (!sdMount()) return nullptr;
  snprintf(mine, sizeof(mine), "/inw/%s", name);
  if (validStore(SD, mine, rec) && copyFile(SD, mine, SPIFFS, live)) return "sd mirror";
  snprintf(wada, sizeof(wada), "/meshcomod/%s", name);
  if (validStore(SD, wada, rec) && copyFile(SD, wada, SPIFFS, live)) return "wadamesh sd";
  return nullptr;
}

static bool identityFromHex(const char* prvHex, const char* pubHex) {
  if (!prvHex || strlen(prvHex) != 128 || !pubHex || strlen(pubHex) != 64) return false;
  uint8_t buf[96];
  if (!mesh::Utils::fromHex(buf, 64, prvHex) || !mesh::Utils::fromHex(buf + 64, 32, pubHex)) return false;
  mesh::LocalIdentity id;
  id.readFrom(buf, sizeof(buf));
  memset(buf, 0, sizeof(buf));
  IdentityStore store(SPIFFS, "/identity");
  return store.save("_main", id);
}

struct PsramAllocator : ArduinoJson::Allocator {
  void* allocate(size_t n) override {
    void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(n);
  }
  void deallocate(void* p) override { free(p); }
  void* reallocate(void* p, size_t n) override {
    void* q = heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return q ? q : realloc(p, n);
  }
};
static PsramAllocator s_alloc;

// ---- NVS safety copy ------------------------------------------------------------
// NVS sits where it is in every partition layout this firmware has used, so the
// small essentials kept there survive a layout change even with no SD card:
// the identity (keys), channels and mesh prefs. Contacts are too big for NVS;
// they come back from SD or refill from adverts.
static const struct { const char* key; const char* path; size_t max; } KEEP[] = {
  {"id", "/identity/_main.id", 256},
  {"ch", "/channels2", 40 * CHANNEL_REC},
  {"pr", "/prefs.json", 4096},
};

void keepEssentials() {
  Preferences p;
  if (!p.begin("inw-keep", false)) return;
  static uint8_t buf[4096], old[4096];
  for (const auto& k : KEEP) {
    File f = SPIFFS.open(k.path, FILE_READ);
    if (!f) continue;
    const size_t n = f.size();
    if (!n || n > k.max || n > sizeof(buf)) { f.close(); continue; }
    const size_t got = f.read(buf, n);
    f.close();
    if (got != n) continue;
    // Only write when it changed: NVS is flash too.
    if (p.getBytesLength(k.key) == n && p.getBytes(k.key, old, n) == n && !memcmp(old, buf, n)) continue;
    p.putBytes(k.key, buf, n);
  }
  p.end();
}

static bool restoreFromKeep(const char* key, const char* path) {
  Preferences p;
  if (!p.begin("inw-keep", true)) return false;
  const size_t n = p.getBytesLength(key);
  static uint8_t buf[4096];
  bool ok = false;
  if (n && n <= sizeof(buf) && p.getBytes(key, buf, n) == n) {
    if (!strncmp(path, "/identity/", 10) && !SPIFFS.exists("/identity")) SPIFFS.mkdir("/identity");
    File f = SPIFFS.open(path, FILE_WRITE);
    if (f) { ok = f.write(buf, n) == n; f.close(); }
  }
  p.end();
  return ok;
}

// Two records with the same secret are the same channel, whatever they're
// named; old restores can bring copies back. Keep the first of each.
static uint8_t dedupeChannels() {
  if (!validStore(SPIFFS, "/channels2", CHANNEL_REC)) return 0;
  static uint8_t recs[MAX_GROUP_CHANNELS * CHANNEL_REC];
  File f = SPIFFS.open("/channels2", FILE_READ);
  if (!f) return 0;
  const size_t n = f.size() / CHANNEL_REC;
  if (!n || n > MAX_GROUP_CHANNELS) { f.close(); return 0; }
  const size_t got = f.read(recs, n * CHANNEL_REC);
  f.close();
  if (got != n * CHANNEL_REC) return 0;

  const size_t SECRET = 36;            // 4 unused + 32 name, then the 32-byte secret
  size_t keep = 0;
  uint8_t dropped = 0;
  for (size_t i = 0; i < n; i++) {
    const uint8_t* rec = recs + i * CHANNEL_REC;
    bool dup = false;
    for (size_t k = 0; k < keep && !dup; k++) dup = !memcmp(recs + k * CHANNEL_REC + SECRET, rec + SECRET, 32);
    if (dup) { dropped++; continue; }
    if (keep != i) memmove(recs + keep * CHANNEL_REC, rec, CHANNEL_REC);
    keep++;
  }
  if (!dropped) return 0;
  File w = SPIFFS.open("/channels2", FILE_WRITE);
  if (!w) return 0;
  const bool ok = w.write(recs, keep * CHANNEL_REC) == keep * CHANNEL_REC;
  w.close();
  logs.add(LOG_WARN, "channels: removed %u duplicate%s", dropped, dropped == 1 ? "" : "s");
  return ok ? dropped : 0;
}

void importBeforeNode(char* report, size_t cap) {
  report[0] = 0;
  size_t w = 0;
  logs.add(LOG_INFO, "flash store %u/%u kB", (unsigned)(SPIFFS.usedBytes() / 1024), (unsigned)(SPIFFS.totalBytes() / 1024));
  // One pass over the store. On this 8 MB SPIFFS every exists()/open() by name
  // scans the whole partition, and a normal boot used to ask a dozen of them
  // (~2.6 s) only to find everything already in place. If this listing shows
  // every store present and whole, with no half-finished save, skip all of it.
  // Anything missing or odd falls through to the full restore below, unchanged.
  long szContacts = -1, szChannels = -1, szPrefs = -1, szHist = -1, szId = -1;
  bool leftovers = false;
  const uint32_t t0 = millis();
  {
    File root = SPIFFS.open("/");
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
      const char* p = f.path();
      const long sz = (long)f.size();
      if (sz > 16384) Serial.printf("[store] %s %u kB\n", p, (unsigned)(sz / 1024));
      if (!strcmp(p, "/contacts3")) szContacts = sz;
      else if (!strcmp(p, "/channels2")) szChannels = sz;
      else if (!strcmp(p, "/prefs.json")) szPrefs = sz;
      else if (!strcmp(p, "/hist.log")) szHist = sz;
      else if (!strcmp(p, "/identity/_main.id")) szId = sz;
      else if (strstr(p, ".tmp")) leftovers = true;
    }
  }
  // The key sits in /identity/; if the listing shows that as a folder rather
  // than the file, ask for the one file by name.
  if (szId < 0) szId = (long)fileSize(SPIFFS, "/identity/_main.id");
  const bool whole = !leftovers &&
      szContacts >= (long)CONTACT_REC && szContacts % CONTACT_REC == 0 &&
      szChannels >= (long)CHANNEL_REC && szChannels % CHANNEL_REC == 0 &&
      szPrefs > 0 && szId >= 96 && szHist >= 0;
  if (whole) {
    uint8_t d = dedupeChannels();
    Serial.printf("[store] all present, restore skipped (%lums)\n", (unsigned long)(millis() - t0));
    if (d) { snprintf(report, cap, "channels<-duplicates removed "); logs.add(LOG_INFO, "channels restored from duplicates removed"); }
    return;
  }
  Serial.printf("[store] something missing or unfinished, full restore check (%lums)\n", (unsigned long)(millis() - t0));
  auto note = [&](const char* what, const char* from) {
    if (from && w < cap) w += snprintf(report + w, cap - w, "%s<-%s ", what, from);
    if (from) logs.add(LOG_INFO, "%s restored from %s", what, from);
  };
  note("contacts", restoreStore("contacts3", CONTACT_REC));
  const char* chFrom = restoreStore("channels2", CHANNEL_REC);
  if (!chFrom && !validStore(SPIFFS, "/channels2", CHANNEL_REC) && restoreFromKeep("ch", "/channels2")) chFrom = "safety copy";
  note("channels", chFrom);
  if (dedupeChannels()) note("channels", "duplicates removed");

  // Mesh prefs, message history: from the SD mirror, then (prefs only) NVS.
  if (!SPIFFS.exists("/prefs.json")) {
    const char* from = nullptr;
    if (sdMount() && fileSize(SD, "/inw/prefs.json") && copyFile(SD, "/inw/prefs.json", SPIFFS, "/prefs.json")) from = "sd mirror";
    else if (restoreFromKeep("pr", "/prefs.json")) from = "safety copy";
    note("settings", from);
  }
  if (!SPIFFS.exists("/hist.log") && sdMount() && fileSize(SD, "/inw/hist.log")) {
    if (copyFile(SD, "/inw/hist.log", SPIFFS, "/hist.log")) note("messages", "sd mirror");
    if (fileSize(SD, "/inw/hist_read.bin")) copyFile(SD, "/inw/hist_read.bin", SPIFFS, "/hist_read.bin");
  }

  if (!SPIFFS.exists("/identity/_main.id")) {
    const char* from = nullptr;
    if (sdMount() && fileSize(SD, "/inw/identity/_main.id") >= 96 &&
        copyFile(SD, "/inw/identity/_main.id", SPIFFS, "/identity/_main.id")) from = "sd mirror";
    if (!from && restoreFromKeep("id", "/identity/_main.id") && fileSize(SPIFFS, "/identity/_main.id") >= 96) from = "safety copy";
#if defined(SEED_PRV64_HEX)
    if (!from && identityFromHex(SEED_PRV64_HEX, SEED_PUB_HEX)) from = "built-in key";
#endif
    if (!from && sdMount() && fileSize(SD, "/meshcomod/identity/_main.id") >= 96 &&
        copyFile(SD, "/meshcomod/identity/_main.id", SPIFFS, "/identity/_main.id")) from = "wadamesh sd";
    if (!from && sdMount() && SD.exists(JSON_PATH)) {
      File f = SD.open(JSON_PATH, FILE_READ);
      JsonDocument filter;
      filter["public_key"] = true; filter["private_key"] = true;
      JsonDocument doc(&s_alloc);
      if (f && !deserializeJson(doc, f, DeserializationOption::Filter(filter)) &&
          identityFromHex(doc["private_key"] | "", doc["public_key"] | "")) from = "json export";
      if (f) f.close();
    }
    note("identity", from);
  }
}

static bool isDefaultName(const char* name, const uint8_t* pub) {
  char hex[10];
  mesh::Utils::toHex(hex, pub, 4);
  return !strcmp(name, hex) || !strcmp(name, "NONAME") || !name[0];
}

static int32_t coord(JsonVariantConst v) {
  if (v.is<const char*>()) return (int32_t)lround(atof(v.as<const char*>()) * 1e6);
  if (v.is<double>()) return (int32_t)lround(v.as<double>() * 1e6);
  return 0;
}

// Pull contacts/channels out of a MeshCore JSON export into the live node.
// Existing entries are left alone; returns how many were added.
static uint16_t mergeJson(JsonDocument& doc, uint16_t& chans) {
  uint16_t added = 0;
  chans = 0;
  for (JsonObjectConst ch : doc["channels"].as<JsonArrayConst>()) {
    const char* name = ch["name"] | "";
    const char* sec = ch["secret"] | "";
    uint8_t s[16];
    if (!name[0] || strlen(sec) != 32 || !mesh::Utils::fromHex(s, 16, sec)) continue;
    if (g_node->addChannelNamed(name, s)) chans++;
  }
  for (JsonObjectConst c : doc["contacts"].as<JsonArrayConst>()) {
    const char* pk = c["public_key"] | "";
    uint8_t pub[32];
    if (strlen(pk) != 64 || !mesh::Utils::fromHex(pub, 32, pk)) continue;
    if (g_node->contact(pub)) continue;
    ContactInfo ci;
    memset(&ci, 0, sizeof(ci));
    ci.id = mesh::Identity(pub);
    strlcpy(ci.name, c["name"] | "", sizeof(ci.name));
    ci.type = c["type"] | 1;
    ci.flags = c["flags"] | 0;
    ci.out_path_len = OUT_PATH_UNKNOWN;
    ci.last_advert_timestamp = c["last_advert"] | 0;
    ci.lastmod = c["last_modified"] | 0;
    ci.gps_lat = coord(c["latitude"]);
    ci.gps_lon = coord(c["longitude"]);
    if (!ci.type || !ci.name[0]) continue;
    if (!g_node->addContact(ci)) break;          // table full
    added++;
  }
  if (added) g_node->saveContactsNow();
  return added;
}

void importPrefsAfterNode(char* report, size_t cap) {
  report[0] = 0;
  if (!g_node) return;
  NodePrefs& p = g_node->prefs();
  const bool needPrefs = !(ui_settings.importDone & 2) && isDefaultName(p.node_name, g_node->self_id.pub_key);
  bool needChans = true;
  {
    int named = 0;
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) { ChannelDetails ch; if (g_node->getChannel(i, ch) && ch.name[0]) named++; }
    needChans = named <= 1;
  }
  const bool needContacts = g_node->getNumContacts() == 0;
  if (!needPrefs && !needChans && !needContacts) return;

  size_t w = 0;
  if (sdMount() && SD.exists(JSON_PATH)) {
    File f = SD.open(JSON_PATH, FILE_READ);
    JsonDocument doc(&s_alloc);
    if (f && !deserializeJson(doc, f)) {
      // Only take a file that is this device's own export.
      uint8_t pub[32];
      const char* pk = doc["public_key"] | "";
      const bool mine = strlen(pk) == 64 && mesh::Utils::fromHex(pub, 32, pk) &&
                        !memcmp(pub, g_node->self_id.pub_key, 32);
      if (mine && needPrefs) {
        strlcpy(p.node_name, doc["name"] | p.node_name, sizeof(p.node_name));
        JsonObjectConst r = doc["radio_settings"];
        if (!r.isNull()) {
          const float fq = r["frequency"] | 0.0f;
          const float bw = r["bandwidth"] | 0.0f;
          // Exports disagree on units (MHz vs kHz, kHz vs Hz); normalise.
          if (fq > 100 && fq < 1100) p.freq = fq; else if (fq > 100000) p.freq = fq / 1000.0f;
          if (bw > 5 && bw <= 500) p.bw = bw; else if (bw > 5000) p.bw = bw / 1000.0f;
          p.sf = constrain((int)(r["spreading_factor"] | p.sf), 5, 12);
          p.cr = constrain((int)(r["coding_rate"] | p.cr), 5, 8);
          p.tx_power_dbm = constrain((int)(r["tx_power"] | p.tx_power_dbm), -9, 22);
        }
        JsonObjectConst pos = doc["position_settings"];
        if (!pos.isNull()) {
          p.node_lat = coord(pos["latitude"]) / 1e6;
          p.node_lon = coord(pos["longitude"]) / 1e6;
          sensors.node_lat = p.node_lat; sensors.node_lon = p.node_lon;
        }
        g_node->savePrefsNow();
        g_node->applyRadio();
        w += snprintf(report + w, cap - w, "prefs<-json ");
      }
      if (mine && (needChans || needContacts)) {
        uint16_t ch = 0;
        const uint16_t n = mergeJson(doc, ch);
        if (w < cap) w += snprintf(report + w, cap - w, "+%u contacts +%u chans ", n, ch);
      }
    }
    if (f) f.close();
  }
#if defined(SEED_NAME)
  if (needPrefs && isDefaultName(p.node_name, g_node->self_id.pub_key)) {
    strlcpy(p.node_name, SEED_NAME, sizeof(p.node_name));
    g_node->savePrefsNow();
  }
#endif
  ui_settings.importDone |= 3;
  ui_settings.save();
  if (report[0]) logs.add(LOG_INFO, "import: %s", report);
}

const char* importJsonNow() {
  static char msg[48];
  if (!g_node) return "node not running";
  if (!sdMount()) return "no sd card";
  if (!SD.exists(JSON_PATH)) return "no meshcore-backup.json on sd";
  File f = SD.open(JSON_PATH, FILE_READ);
  JsonDocument doc(&s_alloc);
  const bool ok = f && !deserializeJson(doc, f);
  if (f) f.close();
  if (!ok) return "export file unreadable";
  uint16_t ch = 0;
  const uint16_t n = mergeJson(doc, ch);
  snprintf(msg, sizeof(msg), "added %u contacts, %u channels", n, ch);
  return msg;
}

// ---- recovering contacts ---------------------------------------------------------------
// Adds every contact in a store-format file (152-byte /contacts3 records) that the
// node doesn't already have. Nothing is removed or overwritten, so it is safe to run
// against any backup, old or new.
static uint16_t mergeStoreFile(fs::FS& fs, const char* path, uint16_t& seen) {
  seen = 0;
  if (!g_node || !validStore(fs, path, CONTACT_REC)) return 0;
  File f = fs.open(path, FILE_READ);
  if (!f) return 0;
  uint8_t r[CONTACT_REC];
  uint16_t added = 0;
  while (f.read(r, CONTACT_REC) == CONTACT_REC) {
    seen++;
    if (g_node->contact(r)) continue;
    ContactInfo ci;
    memset(&ci, 0, sizeof(ci));
    ci.id = mesh::Identity(r);
    memcpy(ci.name, r + 32, sizeof(ci.name));
    ci.name[sizeof(ci.name) - 1] = 0;
    ci.type = r[64];
    ci.flags = r[65];
    memcpy(&ci.sync_since, r + 67, 4);
    ci.out_path_len = r[71];
    memcpy(&ci.last_advert_timestamp, r + 72, 4);
    memcpy(ci.out_path, r + 76, 64);
    memcpy(&ci.lastmod, r + 140, 4);
    memcpy(&ci.gps_lat, r + 144, 4);
    memcpy(&ci.gps_lon, r + 148, 4);
    if (!ci.type) continue;
    if (!g_node->addContact(ci)) break;          // table full
    added++;
  }
  f.close();
  return added;
}

static void listDaily(std::vector<String>& out) {
  out.clear();
  File dir = SD.open("/inw/daily");
  if (!dir) return;
  for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
    const String n = e.name();
    if (!e.isDirectory() && n.indexOf("contacts3-") >= 0) out.push_back(String("/inw/daily/") + n.substring(n.lastIndexOf('/') + 1));
    e.close();
  }
  std::sort(out.begin(), out.end());
}

const char* recoverMissingContacts() {
  static char msg[64];
  if (!g_node) return "node not running";
  const int before = g_node->getNumContacts();
  uint16_t added = 0, seen = 0;
  added += mergeStoreFile(SPIFFS, "/contacts3.bak", seen);
  if (sdMount()) {
    added += mergeStoreFile(SD, "/inw/contacts3", seen);
    std::vector<String> daily;
    listDaily(daily);
    for (const String& p : daily) added += mergeStoreFile(SD, p.c_str(), seen);
    added += mergeStoreFile(SD, "/meshcomod/contacts3", seen);
  }
  if (added) g_node->saveContactsNow();
  logs.add(LOG_INFO, "recover contacts: %d -> %d (+%u from backups)", before, g_node->getNumContacts(), added);
  snprintf(msg, sizeof(msg), added ? "recovered %u contacts" : "nothing missing from backups", added);
  return msg;
}

void storeReport() {
  auto line = [](const char* what, fs::FS& fs, const char* p) {
    const size_t s = fileSize(fs, p);
    Serial.printf("[stores] %-24s %7u bytes  %5u records%s\n", what, (unsigned)s, (unsigned)(s / CONTACT_REC),
                  s && s % CONTACT_REC ? "  (NOT a whole number of records)" : "");
  };
  Serial.printf("[stores] contacts in memory: %d\n", g_node ? g_node->getNumContacts() : -1);
  line("flash /contacts3", SPIFFS, "/contacts3");
  line("flash /contacts3.bak", SPIFFS, "/contacts3.bak");
  if (sdMount()) {
    line("sd /inw/contacts3", SD, "/inw/contacts3");
    line("sd /meshcomod/contacts3", SD, "/meshcomod/contacts3");
    std::vector<String> daily;
    listDaily(daily);
    for (const String& p : daily) line(p.c_str() + 11, SD, p.c_str());
  } else {
    Serial.println("[stores] no sd card");
  }
  Serial.printf("[stores] last sd backup: %lu\n", (unsigned long)ui_settings.lastSdBackup);
}

// Called after every successful store save. A contact list that shrinks without
// anyone forgetting contacts is how the 2026-09-16 loss looked, so a drop is logged
// loudly with both counts; small drops (forgetting a few) are normal.
void inwStoreSaved(const char* path, size_t bytes) {
  if (strcmp(path, "/contacts3") != 0) return;
  static long last = -1;
  const long n = (long)(bytes / CONTACT_REC);
  if (last >= 0 && n + 5 < last) logs.add(LOG_WARN, "contacts saved: %ld, was %ld - %ld fewer", n, last, last - n);
  last = n;
}

// ---- backups -------------------------------------------------------------------
// Copies a store file over its backup unless that would throw away a lot of
// records. A torn save looks exactly like that, and the backup is the way back.
static bool mirrorStore(const char* src, fs::FS& to, const char* dst, size_t rec, bool force) {
  if (!validStore(SPIFFS, src, rec)) return false;
  const size_t have = fileSize(SPIFFS, src) / rec, kept = fileSize(to, dst) / rec;
  if (!force && kept > 10 && have < kept * 9 / 10) {
    logs.add(LOG_WARN, "kept %s backup: %u records, store has only %u", dst, (unsigned)kept, (unsigned)have);
    return false;
  }
  return copyFile(SPIFFS, src, to, dst) > 0;
}

// Flash-side last-good copies, then the SD mirror. Each step names itself on the
// progress screen.
const char* sdBackupNow(bool force) {
  inwStoreFlush(10000);              // copy what's on flash only once queued saves have landed
  keepEssentials();
  s_progress = "backing up contacts";
  mirrorStore("/contacts3", SPIFFS, "/contacts3.bak", CONTACT_REC, force);
  s_progress = "backing up channels";
  mirrorStore("/channels2", SPIFFS, "/channels2.bak", CHANNEL_REC, force);
  uint8_t n = 0;
  const bool sd = sdMount();
  if (sd) {
    if (!SD.exists("/inw")) SD.mkdir("/inw");
    if (!SD.exists("/inw/identity")) SD.mkdir("/inw/identity");
    s_progress = "copying contacts to sd";
    if (mirrorStore("/contacts3", SD, "/inw/contacts3", CONTACT_REC, force)) n++;
    // A dated copy too, newest 7 kept. The mirror above can be replaced by a list
    // that quietly lost a few percent; a dated copy from before never is.
    if (validStore(SPIFFS, "/contacts3", CONTACT_REC)) {
      if (!SD.exists("/inw/daily")) SD.mkdir("/inw/daily");
      const time_t now = rtc_clock.getCurrentTime();
      struct tm tm;
      gmtime_r(&now, &tm);
      char dated[48];
      snprintf(dated, sizeof(dated), "/inw/daily/contacts3-%04d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
      copyFile(SPIFFS, "/contacts3", SD, dated);
      std::vector<String> daily;
      listDaily(daily);
      for (size_t i = 0; daily.size() > 7 && i < daily.size() - 7; i++) SD.remove(daily[i].c_str());
    }
    s_progress = "copying channels to sd";
    if (mirrorStore("/channels2", SD, "/inw/channels2", CHANNEL_REC, force)) n++;
    s_progress = "copying identity to sd";
    if (fileSize(SPIFFS, "/identity/_main.id") >= 96 &&
        copyFile(SPIFFS, "/identity/_main.id", SD, "/inw/identity/_main.id")) n++;
    s_progress = "copying settings to sd";
    if (copyFile(SPIFFS, "/prefs.json", SD, "/inw/prefs.json")) n++;
    if (copyFile(SPIFFS, "/ui.bin", SD, "/inw/ui.bin")) n++;      // theme, brightness, the rest of the UI
    s_progress = "copying messages to sd";
    if (copyFile(SPIFFS, "/hist.log", SD, "/inw/hist.log")) n++;
    copyFile(SPIFFS, "/hist_read.bin", SD, "/inw/hist_read.bin");
  }
  s_progress = nullptr;
  inwProgress(nullptr, 0, 0);
  if (!sd) return "flash copy done, no sd card";
  ui_settings.lastSdBackup = rtc_clock.getCurrentTime();
  ui_settings.save();
  static char msg[32];
  snprintf(msg, sizeof(msg), n >= 3 ? "backed up to sd (%u files)" : "backup incomplete (%u)", n);
  return msg;
}

// Once a day. Checked every few minutes so a clock that only becomes valid
// later (wifi, gps) is still honoured; without one, every 24 h of uptime.
bool inwCanSaveNow();
void sdBackupTick() {
  static const uint32_t DAY = 24UL * 3600UL;
  static uint32_t next = 5UL * 60UL * 1000UL, lastRun = 0;
  if ((int32_t)(millis() - next) < 0) return;
  next = millis() + 5UL * 60UL * 1000UL;
  if (!g_node || !inwCanSaveNow()) return;   // only with the screen off, never mid-use
  const uint32_t now = rtc_clock.getCurrentTime();
  const bool clockOk = now > 1700000000UL;
  const bool due = clockOk ? (now - ui_settings.lastSdBackup >= DAY)
                           : (!lastRun || millis() - lastRun >= DAY * 1000UL);
  if (!due) return;
  if (lastRun && millis() - lastRun < 3600000UL) return;   // failed recently (no card): hourly at most
  lastRun = millis();
  const char* r = sdBackupNow(false);
  logs.add(LOG_INFO, "daily backup: %s", r);
}

// ---- export ----------------------------------------------------------------------
static void jsonString(File& f, const char* s) {
  f.print('"');
  for (; *s; s++) {
    const char c = *s;
    if (c == '"' || c == '\\') { f.print('\\'); f.print(c); }
    else if ((uint8_t)c < 0x20) f.printf("\\u%04x", (uint8_t)c);
    else f.print(c);
  }
  f.print('"');
}

const char* exportJson() {
  static char msg[48];
  if (!g_node) return "node not running";
  if (!sdMount()) return "no sd card";
  if (!SD.exists("/inw")) SD.mkdir("/inw");
  NodePrefs& p = g_node->prefs();
  char path[80], safe[24];
  size_t k = 0;
  for (const char* s = p.node_name; *s && k < sizeof(safe) - 1; s++)
    safe[k++] = isalnum((unsigned char)*s) ? *s : '_';
  safe[k] = 0;
  snprintf(path, sizeof(path), "/inw/%s_meshcore_config_%lu.json", safe, (unsigned long)rtc_clock.getCurrentTime());
  File f = SD.open(path, FILE_WRITE);
  if (!f) return "could not write sd";

  uint8_t key[96];
  g_node->self_id.writeTo(key, sizeof(key));
  char hex[129];
  f.print("{\n  \"name\": "); jsonString(f, p.node_name);
  mesh::Utils::toHex(hex, g_node->self_id.pub_key, 32);
  f.printf(",\n  \"public_key\": \"%s\",\n", hex);
  mesh::Utils::toHex(hex, key, 64);
  memset(key, 0, sizeof(key));
  f.printf("  \"private_key\": \"%s\",\n", hex);
  memset(hex, 0, sizeof(hex));
  f.printf("  \"radio_settings\": {\"frequency\": %.4f, \"bandwidth\": %.2f, \"spreading_factor\": %u, "
           "\"coding_rate\": %u, \"tx_power\": %d},\n", p.freq, p.bw, p.sf, p.cr, p.tx_power_dbm);
  f.printf("  \"position_settings\": {\"latitude\": %.6f, \"longitude\": %.6f},\n", p.node_lat, p.node_lon);
  f.printf("  \"other_settings\": {\"manual_add_contacts\": %u, \"advert_location_policy\": %u},\n",
           p.manual_add_contacts, p.advert_loc_policy);
  f.print("  \"channels\": [");
  bool first = true;
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    ChannelDetails ch;
    if (!g_node->getChannel(i, ch) || !ch.name[0]) continue;
    mesh::Utils::toHex(hex, ch.channel.secret, 16);
    f.print(first ? "\n    {\"name\": " : ",\n    {\"name\": ");
    jsonString(f, ch.name);
    f.printf(", \"secret\": \"%s\"}", hex);
    first = false;
  }
  f.print("\n  ],\n  \"contacts\": [");
  first = true;
  uint16_t n = 0;
  ContactsIterator it = g_node->startContactsIterator();
  ContactInfo c;
  while (it.hasNext(g_node, c)) {
    if (!c.type) continue;
    mesh::Utils::toHex(hex, c.id.pub_key, 32);
    f.print(first ? "\n    {\"type\": " : ",\n    {\"type\": ");
    f.printf("%u, \"name\": ", c.type);
    jsonString(f, c.name);
    f.printf(", \"custom_name\": null, \"public_key\": \"%s\", \"flags\": %u, \"latitude\": \"%.6f\", "
             "\"longitude\": \"%.6f\", \"last_advert\": %lu, \"last_modified\": %lu, \"out_path\": null}",
             hex, c.flags, c.gps_lat / 1e6, c.gps_lon / 1e6,
             (unsigned long)c.last_advert_timestamp, (unsigned long)c.lastmod);
    first = false;
    n++;
  }
  f.print("\n  ]\n}\n");
  f.close();
  snprintf(msg, sizeof(msg), "exported %u contacts to sd", n);
  return msg;
}
