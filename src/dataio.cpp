#include "dataio.h"
#include <SD.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
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

static size_t copyFile(fs::FS& from, const char* src, fs::FS& to, const char* dst) {
  File in = from.open(src, FILE_READ);
  if (!in) return 0;
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

void importBeforeNode(char* report, size_t cap) {
  report[0] = 0;
  size_t w = 0;
  auto note = [&](const char* what, const char* from) {
    if (from && w < cap) w += snprintf(report + w, cap - w, "%s<-%s ", what, from);
    if (from) logs.add(LOG_INFO, "%s restored from %s", what, from);
  };
  note("contacts", restoreStore("contacts3", CONTACT_REC));
  note("channels", restoreStore("channels2", CHANNEL_REC));

  if (!SPIFFS.exists("/identity/_main.id")) {
    const char* from = nullptr;
    if (sdMount() && fileSize(SD, "/inw/identity/_main.id") >= 96 &&
        copyFile(SD, "/inw/identity/_main.id", SPIFFS, "/identity/_main.id")) from = "sd mirror";
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

// ---- backups -------------------------------------------------------------------
const char* sdBackupNow() {
  if (!sdMount()) return "no sd card";
  if (!SD.exists("/inw")) SD.mkdir("/inw");
  if (!SD.exists("/inw/identity")) SD.mkdir("/inw/identity");
  uint8_t n = 0;
  if (validStore(SPIFFS, "/contacts3", CONTACT_REC) && copyFile(SPIFFS, "/contacts3", SD, "/inw/contacts3")) n++;
  if (validStore(SPIFFS, "/channels2", CHANNEL_REC) && copyFile(SPIFFS, "/channels2", SD, "/inw/channels2")) n++;
  if (fileSize(SPIFFS, "/identity/_main.id") >= 96 &&
      copyFile(SPIFFS, "/identity/_main.id", SD, "/inw/identity/_main.id")) n++;
  if (copyFile(SPIFFS, "/prefs.json", SD, "/inw/prefs.json")) n++;
  if (copyFile(SPIFFS, "/hist.log", SD, "/inw/hist.log")) n++;
  copyFile(SPIFFS, "/hist_read.bin", SD, "/inw/hist_read.bin");
  ui_settings.lastSdBackup = rtc_clock.getCurrentTime();
  static char msg[32];
  snprintf(msg, sizeof(msg), n >= 3 ? "backed up to sd (%u files)" : "backup incomplete (%u)", n);
  return msg;
}

void sdBackupTick() {
  static uint32_t next = 120000;          // first mirror two minutes after boot
  if ((int32_t)(millis() - next) < 0) return;
  next = millis() + 30UL * 60UL * 1000UL;
  if (!g_node) return;
  const char* r = sdBackupNow();
  logs.add(LOG_INFO, "auto backup: %s", r);
}

void localBackupTick() {
  static uint32_t next = 180000;
  if ((int32_t)(millis() - next) < 0) return;
  next = millis() + 30UL * 60UL * 1000UL;
  if (validStore(SPIFFS, "/contacts3", CONTACT_REC)) copyFile(SPIFFS, "/contacts3", SPIFFS, "/contacts3.bak");
  if (validStore(SPIFFS, "/channels2", CHANNEL_REC)) copyFile(SPIFFS, "/channels2", SPIFFS, "/channels2.bak");
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
