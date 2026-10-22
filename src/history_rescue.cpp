// Putting lost messages back (History::rescue). Every copy of the message log there is -
// the SD card's backups (and a copy a backup set aside), a tidy-up's leftovers on flash,
// a hist*.log anyone copied onto the card - is read and merged into history: whatever is
// missing comes back, nothing is doubled, and chats cleared on purpose stay cleared.
//
// Why: a tidy-up of the log on a nearly full store could replace it with a short or empty
// copy (fixed in compact()), and then the SD card's copy was not restored - the log was
// there, just empty - and the next backup wrote over the card's copy too. Owners lost
// every message at the restart an update makes.
//
// Runs from begin(), before anything can be sending, so message numbers can be handed
// out afresh: at every start where a copy has changed since the last look (a signature of
// their names and sizes, in NVS), and when Settings > Backups asks (requestRescue, then a
// restart).
#include "history.h"
#include "dataio.h"
#include "logstore.h"
#include <SD.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <vector>
extern LogStore logs;

namespace {

struct StatusRec { uint32_t id; uint8_t status, attempts, repeats; uint16_t rtt10; } __attribute__((packed));

struct Ent {
  HistMsg m;
  Route r;
#if HISTORY_HAS_REGIONS
  char rg[History::REGION_LEN];
#endif
  uint8_t src;      // 0: in history now
  bool read;        // seen (history's own by its read mark; anything put back counts as seen)
  bool dup;
};

// A growing array in PSRAM: a few thousand messages of a few hundred bytes each.
struct Pool {
  Ent* p = nullptr;
  size_t n = 0, cap = 0;
  Ent* push() {
    if (n == cap) {
      const size_t nc = cap ? cap * 2 : 1024;
      Ent* q = (Ent*)heap_caps_realloc(p, nc * sizeof(Ent), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (!q) return nullptr;
      p = q; cap = nc;
    }
    Ent* e = &p[n++];
    memset(e, 0, sizeof(Ent));
    return e;
  }
  ~Pool() { free(p); }
};

// Chats cleared on purpose: everything up to this time stays gone. NVS "inw-hclr".
struct Cleared { ConvKey k; uint32_t ts; };
constexpr uint8_t CLEARED_MAX = 48;

struct Clears {
  uint32_t all = 0;
  Cleared c[CLEARED_MAX];
  uint8_t n = 0;
  void load() {
    Preferences p;
    if (!p.begin("inw-hclr", true)) return;
    all = p.getUInt("all", 0);
    const size_t len = p.getBytesLength("cv");
    if (len && len % sizeof(Cleared) == 0 && len <= sizeof(c)) { p.getBytes("cv", c, len); n = len / sizeof(Cleared); }
    p.end();
  }
  bool gone(const HistMsg& m) const {
    if (m.ts <= all && all) return true;
    for (uint8_t i = 0; i < n; i++) if (c[i].k == m.conv && m.ts <= c[i].ts) return true;
    return false;
  }
};

uint32_t fnv(uint32_t h, const void* d, size_t n) {
  const uint8_t* b = (const uint8_t*)d;
  for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 16777619u; }
  return h;
}

bool isLog(const char* base) {
  return !strncmp(base, "hist", 4) && strstr(base, ".log") != nullptr;
}

// hist*.log files on the card: its root, /inw and the folders under it (not the
// archive of older messages, which is text kept apart on purpose).
void findOnCard(const char* dir, int depth, std::vector<String>& out) {
  File d = SD.open(dir);
  if (!d || !d.isDirectory()) return;
  std::vector<String> sub;
  for (File e = d.openNextFile(); e; e = d.openNextFile()) {
    String name = e.name();
    const int sl = name.lastIndexOf('/');
    const String base = sl >= 0 ? name.substring(sl + 1) : name;
    String full = String(dir);
    if (!full.endsWith("/")) full += "/";
    full += base;
    if (e.isDirectory()) {
      if (depth > 0 && base != "msgs" && !base.startsWith("System") && !base.startsWith(".")) sub.push_back(full);
    } else if (isLog(base.c_str()) && e.size() > 0 && e.size() < 8u * 1024 * 1024) {
      out.push_back(full);
    }
    e.close();
    if (out.size() > 40) break;
  }
  d.close();
  for (auto& s : sub) findOnCard(s.c_str(), depth - 1, out);
}

// One log, read the way loadLog reads it: its own clears and statuses applied.
void parse(File& f, Pool& pool, uint8_t src) {
  const size_t first = pool.n;
  while (f.available()) {
    char kind;
    if (f.read((uint8_t*)&kind, 1) != 1) break;
    if (kind == 'M' || kind == 'N') {
      HistMsg m;
      memset(&m, 0, sizeof(m));
      if (kind == 'N') { if (f.read((uint8_t*)&m, sizeof(m)) != sizeof(m)) break; }
      else {
        HistMsgV1 v1;
        if (f.read((uint8_t*)&v1, sizeof(v1)) != sizeof(v1)) break;
        memcpy(&m, &v1, sizeof(v1));
      }
      m.sender[sizeof(m.sender) - 1] = 0;
      m.text[sizeof(m.text) - 1] = 0;
      if (m.path_len > sizeof(m.path)) m.path_len = 0;
      Ent* e = pool.push();
      if (!e) return;
      e->m = m; e->src = src; e->read = true;
    } else if (kind == 'S') {
      StatusRec r;
      if (f.read((uint8_t*)&r, sizeof(r)) != sizeof(r)) break;
      for (size_t i = pool.n; i-- > first;)
        if (pool.p[i].m.id == r.id) { HistMsg& m = pool.p[i].m; m.status = r.status; m.attempts = r.attempts; m.repeats = r.repeats; m.rtt10 = r.rtt10; break; }
    } else if (kind == 'D') {
      ConvKey k;
      if (f.read((uint8_t*)&k, sizeof(k)) != sizeof(k)) break;
      for (size_t i = first; i < pool.n; i++) if (pool.p[i].m.conv == k) pool.p[i].m.conv.type = 0;
    } else if (kind == 'X') {
      for (size_t i = first; i < pool.n; i++) pool.p[i].m.conv.type = 0;
    } else {
      break;   // a torn tail: keep what came before it
    }
  }
}

// The same message, from two copies: same chat, way, time, sender and words.
int cmpSame(const HistMsg& a, const HistMsg& b) {
  if (a.conv.type != b.conv.type) return a.conv.type < b.conv.type ? -1 : 1;
  if (int c = memcmp(a.conv.id, b.conv.id, 6)) return c;
  if (a.ts != b.ts) return a.ts < b.ts ? -1 : 1;
  const uint8_t ao = a.flags & HF_OUT, bo = b.flags & HF_OUT;
  if (ao != bo) return ao < bo ? -1 : 1;
  if (int c = strncmp(a.sender, b.sender, sizeof(a.sender))) return c;
  return strncmp(a.text, b.text, sizeof(a.text));
}

}  // namespace

void History::requestRescue() {
  Preferences p;
  if (p.begin("inw-hclr", false)) { p.putUChar("force", 1); p.end(); }
}

// A chat (or all of them, k null) cleared on purpose, up to this time: a rescue leaves
// those messages gone.
void History::noteCleared(const ConvKey* k, uint32_t ts) {
  if (!ts) return;
  Clears c;
  c.load();
  Preferences p;
  if (!p.begin("inw-hclr", false)) return;
  if (!k) {
    if (ts > c.all) p.putUInt("all", ts);
  } else {
    int i = 0;
    while (i < c.n && !(c.c[i].k == *k)) i++;
    if (i < c.n && c.c[i].ts >= ts) { p.end(); return; }
    if (i == c.n) {
      if (c.n == CLEARED_MAX) { memmove(c.c, c.c + 1, sizeof(Cleared) * (CLEARED_MAX - 1)); i = CLEARED_MAX - 1; }
      else c.n++;
      c.c[i].k = *k;
    }
    c.c[i].ts = ts;
    p.putBytes("cv", c.c, sizeof(Cleared) * c.n);
  }
  p.end();
}

uint16_t History::rescue(bool force) {
  const uint32_t t0 = millis();
  // Where copies can be.
  std::vector<String> flashSrc, cardSrc;
  for (const char* f : {"/hist.tmp", "/hist.log.new", "/hist.tmp.new"})
    if (SPIFFS.exists(f)) flashSrc.push_back(f);
  if (sdMount()) {
    if (SD.exists("/hist.log")) cardSrc.push_back("/hist.log");
    findOnCard("/inw", 2, cardSrc);
  }
  // Changed since the last look? (names and sizes)
  uint32_t sig = 2166136261u;
  for (auto& s : flashSrc) { File f = SPIFFS.open(s, FILE_READ); const uint32_t z = f ? f.size() : 0; if (f) f.close(); sig = fnv(fnv(sig, s.c_str(), s.length()), &z, 4); }
  for (auto& s : cardSrc) { File f = SD.open(s, FILE_READ); const uint32_t z = f ? f.size() : 0; if (f) f.close(); sig = fnv(fnv(sig, s.c_str(), s.length()), &z, 4); }
  Preferences p;
  uint32_t was = 0;
  if (p.begin("inw-hclr", true)) { was = p.getUInt("sig", 0); force = force || p.getUChar("force", 0); p.end(); }
  if (!force && sig == was) return 0;
  if (flashSrc.empty() && cardSrc.empty()) {
    if (force) logs.add(LOG_WARN, "msgs: no copies found to restore");
    return 0;
  }

  Pool pool;
  // What history holds now first, so its copy of a message is the one kept.
  for (uint16_t i = 0; i < _count; i++) {
    Ent* e = pool.push();
    if (!e) return 0;
    const HistMsg* m = at(i);
    e->m = *m;
    e->src = 0;
    e->read = m->id <= readMark(m->conv);
    if (const Route* r = routeOf(m)) e->r = *r;
#if HISTORY_HAS_REGIONS
    if (const char* g = regionOf(m)) strlcpy(e->rg, g, REGION_LEN);
#endif
  }
  const size_t mine = pool.n;
  uint8_t src = 1;
  for (auto& s : flashSrc) { File f = SPIFFS.open(s, FILE_READ); if (f) { parse(f, pool, src); f.close(); } src++; }
  for (auto& s : cardSrc) {
    File f = SD.open(s, FILE_READ);
    if (f) { Serial.printf("[history] reading %s (%u bytes)\n", s.c_str(), (unsigned)f.size()); parse(f, pool, src); f.close(); }
    if (src < 250) src++;
  }
  const size_t found = pool.n - mine;

  // Cleared, broken or doubled: out.
  Clears clears;
  clears.load();
  for (size_t i = mine; i < pool.n; i++) {
    const HistMsg& m = pool.p[i].m;
    if ((m.conv.type != CONV_CONTACT && m.conv.type != CONV_CHANNEL) || !m.text[0] || clears.gone(m)) pool.p[i].dup = true;
  }
  std::vector<uint32_t> idx;
  idx.reserve(pool.n);
  for (size_t i = 0; i < pool.n; i++) if (!pool.p[i].dup) idx.push_back(i);
  std::stable_sort(idx.begin(), idx.end(), [&](uint32_t a, uint32_t b) { return cmpSame(pool.p[a].m, pool.p[b].m) < 0; });
  // History's own copy sorts first (stable, and it went in first): the later ones go.
  // Two of history's own that look alike both stay - it is not this that tidies them.
  for (size_t j = 1; j < idx.size(); j++)
    if (cmpSame(pool.p[idx[j - 1]].m, pool.p[idx[j]].m) == 0 && pool.p[idx[j]].src != 0) pool.p[idx[j]].dup = true;
  // What comes back, oldest first; history's own keep their order, and each one put back
  // goes in before the first of them that is newer.
  std::vector<uint32_t> back, now;
  for (size_t i = 0; i < pool.n; i++) if (!pool.p[i].dup) (i < mine ? now : back).push_back(i);
  std::stable_sort(back.begin(), back.end(), [&](uint32_t a, uint32_t b) { return pool.p[a].m.ts < pool.p[b].m.ts; });
  std::vector<uint32_t> all;
  all.reserve(now.size() + back.size());
  size_t bi = 0;
  for (uint32_t n : now) {
    while (bi < back.size() && pool.p[back[bi]].m.ts < pool.p[n].m.ts) all.push_back(back[bi++]);
    all.push_back(n);
  }
  while (bi < back.size()) all.push_back(back[bi++]);
  // Only the newest CAP fit.
  const size_t from = all.size() > CAP ? all.size() - CAP : 0;
  uint16_t added = 0;
  for (size_t i = from; i < all.size(); i++) if (all[i] >= mine) added++;

  Preferences w;
  if (!added) {
    if (w.begin("inw-hclr", false)) { w.putUInt("sig", sig); w.remove("force"); w.end(); }
    if (force) logs.add(LOG_INFO, "msgs: %u copies, none missing", (unsigned)(flashSrc.size() + cardSrc.size()));
    Serial.printf("[history] %u copies looked through (%u messages), none missing here, %lu ms\n",
                  (unsigned)(flashSrc.size() + cardSrc.size()), (unsigned)found, (unsigned long)(millis() - t0));
    return 0;
  }

  // Into history, numbered afresh in order; read marks follow.
  memset(_ring, 0, sizeof(HistMsg) * CAP);
  if (_routes) memset(_routes, 0, sizeof(Route) * CAP);
#if HISTORY_HAS_REGIONS
  if (_rgn) memset(_rgn, 0, (size_t)CAP * REGION_LEN);
#endif
  _count = 0; _head = 0;
  uint32_t id = 1;
  ReadMark marks[READ_MAX];
  uint8_t nm = 0;
  for (size_t i = from; i < all.size(); i++) {
    Ent& e = pool.p[all[i]];
    HistMsg& m = _ring[_count];
    m = e.m;
    m.id = id++;
    if (m.status == ST_SENDING) m.status = ST_FAILED;
    if (_routes) _routes[_count] = e.r;
#if HISTORY_HAS_REGIONS
    if (_rgn) strlcpy(_rgn + (size_t)_count * REGION_LEN, e.rg, REGION_LEN);
#endif
    _count++;
    if (e.read) {
      int k = 0;
      while (k < nm && !(marks[k].k == m.conv)) k++;
      if (k == nm && nm < READ_MAX) { marks[nm].k = m.conv; nm++; }
      if (k < nm) marks[k].id = m.id;
    }
  }
  _head = _count % CAP;
  _nextId = id;
  memcpy(_reads, marks, sizeof(ReadMark) * nm);
  _readCount = nm;

  // Written whole and checked, then swapped in (as compact()).
  if (!writeWhole()) {
    // Shown from memory this time; the copies stay where they are for the next try.
    logs.add(LOG_WARN, "msgs back %u, not saved: store full?", (unsigned)added);
    Serial.printf("[history] %u messages put back, but they couldn't be saved\n", (unsigned)added);
    return added;
  }
  saveReads();
  saveRoutes();
#if HISTORY_HAS_REGIONS
  saveRegions();
#endif
  if (w.begin("inw-hclr", false)) { w.putUInt("sig", sig); w.remove("force"); w.end(); }
  logs.add(LOG_WARN, "msgs put back: %u from %u copies", (unsigned)added, (unsigned)(flashSrc.size() + cardSrc.size()));
  Serial.printf("[history] %u messages put back from %u copies (%u messages in them), %lu ms\n", (unsigned)added,
                (unsigned)(flashSrc.size() + cardSrc.size()), (unsigned)found, (unsigned long)(millis() - t0));
  gen++;
  return added;
}
