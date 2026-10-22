#include "history.h"
#include "archive.h"
#include "logstore.h"
extern LogStore logs;
#include <esp_heap_caps.h>

History history;

static const char* LOG_PATH   = "/hist.log";
static const char* READS_PATH = "/hist_read.bin";
// Routes (History::route) by message id: {id, sz, n, n * sz hash bytes} records. A file
// of its own rather than a new kind of record in the log, which older firmware would
// read as a torn tail and stop at, losing every message after it.
static const char* ROUTE_PATH = "/hist_route.bin";
static const char* REGION_PATH = "/hist_region.bin";   // History::region(), GitHub issue #6

struct StatusRec { uint32_t id; uint8_t status, attempts, repeats; uint16_t rtt10; } __attribute__((packed));

bool History::begin() {
  // A tidy-up (compact) cut off between removing the old log and renaming the new one
  // into its place: the new one is whole (it was checked before the old one went).
  if (!SPIFFS.exists(LOG_PATH) && SPIFFS.exists("/hist.tmp")) SPIFFS.rename("/hist.tmp", LOG_PATH);
  _ring = (HistMsg*)heap_caps_calloc(CAP, sizeof(HistMsg), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!_ring) _ring = (HistMsg*)calloc(CAP, sizeof(HistMsg));
  if (!_ring) return false;
  _routes = (Route*)heap_caps_calloc(CAP, sizeof(Route), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  loadLog();
  loadReads();
  loadRoutes();
  _rgn = (char*)heap_caps_calloc(CAP, REGION_LEN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  loadRegions();
  _rescued = rescue(false);       // lost messages back from any copy there is (history_rescue.cpp)
  return true;
}

// Background store writer (tools/patch_meshcore.py). Every message and every
// status change (sending, sent, delivered, each "heard x3") used to open the log
// on the UI loop, and on this 8 MB SPIFFS an open by name scans the partition:
// a few hundred ms per record, several per message on a busy channel. Records
// are queued in order and appended by the task instead; if the queue can't take
// one, it's written here as before.
bool inwQueueAppend(const char* path, const uint8_t* data, size_t len);
bool inwQueueReplace(const char* path, const uint8_t* data, size_t len);
void inwDropPath(const char* path);

void History::appendRecord(char kind, const void* data, size_t len) {
  uint8_t rec[1 + sizeof(HistMsg)];
  if (1 + len <= sizeof(rec)) {
    rec[0] = (uint8_t)kind;
    memcpy(rec + 1, data, len);
    if (inwQueueAppend(LOG_PATH, rec, 1 + len)) return;
  }
  File f = SPIFFS.open(LOG_PATH, FILE_APPEND);
  if (!f) return;
  f.write((const uint8_t*)&kind, 1);
  f.write((const uint8_t*)data, len);
  f.close();
}

void History::writeStatus(const HistMsg& m) {
  StatusRec r{ m.id, m.status, m.attempts, m.repeats, m.rtt10 };
  appendRecord('S', &r, sizeof(r));
}

void History::loadLog() {
  File f = SPIFFS.open(LOG_PATH, FILE_READ);
  if (!f) return;
  const size_t fileSize = f.size();
  while (f.available()) {
    char kind;
    if (f.read((uint8_t*)&kind, 1) != 1) break;
    if (kind == 'M' || kind == 'N') {
      HistMsg m;
      memset(&m, 0, sizeof(m));
      if (kind == 'N') {
        if (f.read((uint8_t*)&m, sizeof(m)) != sizeof(m)) break;
      } else {
        // Older record: same fields up to the text, no path.
        HistMsgV1 v1;
        if (f.read((uint8_t*)&v1, sizeof(v1)) != sizeof(v1)) break;
        memcpy(&m, &v1, sizeof(v1));
      }
      m.sender[sizeof(m.sender) - 1] = 0;
      m.text[sizeof(m.text) - 1] = 0;
      if (m.path_len > sizeof(m.path)) m.path_len = 0;
      _ring[_head] = m;
      _head = (_head + 1) % CAP;
      if (_count < CAP) _count++;
      if (m.id >= _nextId) _nextId = m.id + 1;
    } else if (kind == 'S') {
      StatusRec r;
      if (f.read((uint8_t*)&r, sizeof(r)) != sizeof(r)) break;
      HistMsg* m = find(r.id);
      if (m) { m->status = r.status; m->attempts = r.attempts; m->repeats = r.repeats; m->rtt10 = r.rtt10; }
    } else if (kind == 'D') {
      ConvKey k;
      if (f.read((uint8_t*)&k, sizeof(k)) != sizeof(k)) break;
      uint32_t upTo = 0;
      for (uint16_t i = 0; i < _count; i++) if (at(i)->conv == k) { if (at(i)->ts > upTo) upTo = at(i)->ts; at(i)->conv.type = 0; }
      noteCleared(&k, upTo);       // so putting lost messages back leaves this chat cleared
    } else if (kind == 'X') {
      uint32_t upTo = 0;
      for (uint16_t i = 0; i < _count; i++) if (at(i)->ts > upTo) upTo = at(i)->ts;
      noteCleared(nullptr, upTo);
      _count = 0; _head = 0;
    } else {
      break;   // torn tail from a power cut: keep everything read so far
    }
  }
  f.close();
  // A message still "sending" when the power went is not going to finish.
  for (uint16_t i = 0; i < _count; i++) if (at(i)->status == ST_SENDING) at(i)->status = ST_FAILED;
  if (fileSize > (size_t)CAP * (sizeof(HistMsg) + 1) * 2) compact();
}

// The log rewritten with only what is in history. The new copy has to be all there
// before the old log goes: on a store with little room left (thousands of contacts'
// files) it used to come out short or empty and was swapped in anyway - every message
// gone at the next start, and the SD card's copy written over by the next backup.
void History::compact() {
  if (!writeWhole()) logs.add(LOG_WARN, "msgs tidy-up skipped: store full?");
}

// The log written afresh from what history holds: put together in memory, written in
// one go (each write call on this store costs a good fraction of a second, whatever its
// size), read back for its size, and only then swapped in for the old log.
bool History::writeWhole() {
  size_t n = 0;
  for (uint16_t i = 0; i < _count; i++) if (at(i)->conv.type) n++;
  const size_t want = n * (1 + sizeof(HistMsg));
  uint8_t* buf = want ? (uint8_t*)heap_caps_malloc(want, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : nullptr;
  if (want && !buf) { Serial.printf("[history] save: no memory for %u bytes\n", (unsigned)want); return false; }
  size_t o = 0;
  for (uint16_t i = 0; i < _count; i++) {
    if (!at(i)->conv.type) continue;
    buf[o++] = 'N';               // must match what add() writes, or paths are lost
    memcpy(buf + o, at(i), sizeof(HistMsg));
    o += sizeof(HistMsg);
  }
  inwDropPath(LOG_PATH);          // queued appends are in what is written here
  File f = SPIFFS.open("/hist.tmp", FILE_WRITE);
  if (!f) { free(buf); Serial.println("[history] save: /hist.tmp would not open"); return false; }
  // In pieces: one write of the whole log (~180 kB) comes back with nothing written on
  // a store that has seen a lot of churn, where room has to be cleared as it goes.
  size_t wrote = 0;
  while (wrote < want) {
    const size_t n = min(want - wrote, (size_t)16384);
    const size_t w = f.write(buf + wrote, n);
    wrote += w;
    if (w != n) break;
  }
  f.close();
  free(buf);
  File chk = SPIFFS.open("/hist.tmp", FILE_READ);
  const size_t got = chk ? chk.size() : 0;
  if (chk) chk.close();
  if (wrote != want || got != want) {
    Serial.printf("[history] save: wanted %u bytes, wrote %u, file has %u\n", (unsigned)want, (unsigned)wrote, (unsigned)got);
    SPIFFS.remove("/hist.tmp");
    return false;
  }
  SPIFFS.remove(LOG_PATH);
  SPIFFS.rename("/hist.tmp", LOG_PATH);
  return true;
}

uint32_t History::add(const ConvKey& k, uint8_t flags, uint8_t status, const char* sender,
                      const char* text, uint32_t ts, uint8_t hops, int8_t snr4,
                      const uint8_t* path, uint8_t path_len) {
  HistMsg& m = _ring[_head];
  if (_count == CAP) archive::push(m);  // the oldest is about to go: to the SD card (issue #7)
  memset(&m, 0, sizeof(m));
  m.id = _nextId++;
  m.conv = k;
  m.flags = flags;
  m.status = status;
  m.hops = hops;
  m.snr4 = snr4;
  m.ts = ts;
  strlcpy(m.sender, sender ? sender : "", sizeof(m.sender));
  strlcpy(m.text, text ? text : "", sizeof(m.text));
  m.path_len = path && path_len ? min<uint8_t>(path_len, sizeof(m.path)) : 0;
  if (m.path_len) memcpy(m.path, path, m.path_len);
  if (Route* r = routeOf(&m)) r->n = 0;
  if (char* g = regionOf(&m)) g[0] = 0;
  _head = (_head + 1) % CAP;
  if (_count < CAP) _count++;
  appendRecord('N', &m, sizeof(m));    // 'M' was the layout before paths
  gen++;
  return m.id;
}

HistMsg* History::find(uint32_t id) {
  if (!id || !_count) return nullptr;
  // Newest are at the end and are the ones asked about, so search backwards.
  for (int i = _count - 1; i >= 0; i--) if (at(i)->id == id) return at(i);
  return nullptr;
}

void History::setStatus(uint32_t id, uint8_t st, uint8_t attempts, uint16_t rtt10) {
  HistMsg* m = find(id);
  if (!m) return;
  m->status = st;
  if (attempts != 0xFF) m->attempts = attempts;
  if (rtt10) m->rtt10 = rtt10;
  writeStatus(*m);
  gen++;
}

void History::bumpRepeat(uint32_t id) {
  HistMsg* m = find(id);
  if (!m || m->repeats == 255) return;
  m->repeats++;
  if (m->status == ST_SENDING) m->status = ST_SENT;
  writeStatus(*m);
  gen++;
}

static bool sameHash(const Route& r, const uint8_t* h) {
  for (uint8_t i = 0; i < r.n; i++) if (!memcmp(r.h + i * r.sz, h, r.sz)) return true;
  return false;
}

void History::setRoute(uint32_t id, const uint8_t* hashes, uint8_t sz, uint8_t n) {
  HistMsg* m = find(id);
  Route* r = m ? routeOf(m) : nullptr;
  if (!r || !n || sz < 1 || sz > 3) return;
  r->sz = sz;
  r->n = min<uint8_t>(n, Route::MAX);
  memcpy(r->h, hashes, r->n * sz);
  saveRoutes();
}

void History::heardVia(uint32_t id, const uint8_t* hashes, uint8_t sz, uint8_t n) {
  HistMsg* m = find(id);
  Route* r = m && (m->flags & HF_OUT) ? routeOf(m) : nullptr;
  if (!r || sz < 1 || sz > 3) return;
  if (r->n && r->sz != sz) r->n = 0;          // the mesh changed hash size: start over
  r->sz = sz;
  bool changed = false;
  for (uint8_t i = 0; i < n && r->n < Route::MAX; i++) {
    if (sameHash(*r, hashes + i * sz)) continue;
    memcpy(r->h + r->n * sz, hashes + i * sz, sz);
    r->n++;
    changed = true;
  }
  if (!changed) return;
  gen++;
  saveRoutes();
}

const Route* History::route(uint32_t id) {
  HistMsg* m = find(id);
  const Route* r = m ? routeOf(m) : nullptr;
  return r && r->n ? r : nullptr;
}

// The whole file, rebuilt from history: queued as a replace, which the background
// writer merges with any still waiting and writes with the screen off. (An append
// would share the log's one append buffer and, while that's busy, write on the
// spot: seconds on this SPIFFS.) Only messages still in history are kept.
void History::saveRoutes() {
  if (!_routes) return;
  static uint8_t* buf = nullptr;
  static size_t cap = 0;
  size_t len = 0;
  for (uint16_t i = 0; i < _count; i++) {
    const HistMsg* m = at(i);
    const Route* r = routeOf(m);
    if (!r->n) continue;
    const size_t need = 6 + r->n * r->sz;
    if (len + need > cap) {
      size_t nc = cap ? cap * 2 : 4096;
      while (nc < len + need) nc *= 2;
      uint8_t* nb = (uint8_t*)ps_realloc(buf, nc);
      if (!nb) return;
      buf = nb; cap = nc;
    }
    memcpy(buf + len, &m->id, 4);
    buf[len + 4] = r->sz;
    buf[len + 5] = r->n;
    memcpy(buf + len + 6, r->h, r->n * r->sz);
    len += need;
  }
  inwQueueReplace(ROUTE_PATH, buf, len);
}

void History::setRegion(uint32_t id, const char* name) {
  HistMsg* m = find(id);
  char* g = m ? regionOf(m) : nullptr;
  if (!g || !name || !*name || !strcmp(g, name)) return;
  strlcpy(g, name, REGION_LEN);
  saveRegions();
}

const char* History::region(uint32_t id) {
  HistMsg* m = find(id);
  const char* g = m ? regionOf(m) : nullptr;
  return g ? g : "";
}

// As saveRoutes: the whole file from what is in history, {id 4}{len 1}{name len}.
void History::saveRegions() {
  if (!_rgn) return;
  static uint8_t* buf = nullptr;
  static size_t cap = 0;
  size_t len = 0;
  for (uint16_t i = 0; i < _count; i++) {
    const HistMsg* m = at(i);
    const char* g = regionOf(m);
    const size_t n = strnlen(g, REGION_LEN - 1);
    if (!n) continue;
    if (len + 5 + n > cap) {
      size_t nc = cap ? cap * 2 : 2048;
      while (nc < len + 5 + n) nc *= 2;
      uint8_t* nb = (uint8_t*)ps_realloc(buf, nc);
      if (!nb) return;
      buf = nb; cap = nc;
    }
    memcpy(buf + len, &m->id, 4);
    buf[len + 4] = (uint8_t)n;
    memcpy(buf + len + 5, g, n);
    len += 5 + n;
  }
  inwQueueReplace(REGION_PATH, buf, len);
}

void History::loadRegions() {
  if (!_rgn) return;
  File f = SPIFFS.open(REGION_PATH, FILE_READ);
  if (!f) return;
  uint8_t hd[5];
  char name[REGION_LEN];
  while (f.read(hd, 5) == 5) {
    uint32_t id;
    memcpy(&id, hd, 4);
    const uint8_t n = hd[4];
    if (!n || n >= REGION_LEN || f.read((uint8_t*)name, n) != n) break;
    name[n] = 0;
    HistMsg* m = find(id);
    if (m) strlcpy(regionOf(m), name, REGION_LEN);
  }
  f.close();
}

void History::loadRoutes() {
  if (!_routes) return;
  File f = SPIFFS.open(ROUTE_PATH, FILE_READ);
  if (!f) return;
  uint8_t hd[6], h[Route::MAX * 3];
  while (f.read(hd, 6) == 6) {
    uint32_t id;
    memcpy(&id, hd, 4);
    const uint8_t sz = hd[4], n = hd[5];
    if (sz < 1 || sz > 3 || n > Route::MAX || f.read(h, n * sz) != (int)(n * sz)) break;
    HistMsg* m = find(id);
    if (!m) continue;
    Route* r = routeOf(m);
    r->sz = sz; r->n = n;
    memcpy(r->h, h, n * sz);
  }
  f.close();
}

uint16_t History::collect(const ConvKey& k, uint32_t* ids, uint16_t max) {
  uint16_t n = 0;
  for (uint16_t i = 0; i < _count; i++) {
    if (!(at(i)->conv == k)) continue;
    if (n < max) ids[n++] = at(i)->id;
    else { memmove(ids, ids + 1, (max - 1) * sizeof(uint32_t)); ids[max - 1] = at(i)->id; }
  }
  return n;
}

HistMsg* History::last(const ConvKey& k) {
  for (int i = _count - 1; i >= 0; i--) if (at(i)->conv == k) return at(i);
  return nullptr;
}

uint16_t History::count(const ConvKey& k) {
  uint16_t n = 0;
  for (uint16_t i = 0; i < _count; i++) if (at(i)->conv == k) n++;
  return n;
}

uint32_t History::readMark(const ConvKey& k) {
  for (uint8_t i = 0; i < _readCount; i++) if (_reads[i].k == k) return _reads[i].id;
  return 0;
}

uint16_t History::unread(const ConvKey& k) {
  const uint32_t mark = readMark(k);
  uint16_t n = 0;
  for (uint16_t i = 0; i < _count; i++) {
    const HistMsg* m = at(i);
    if (m->conv == k && !(m->flags & HF_OUT) && m->id > mark) n++;
  }
  return n;
}

uint16_t History::unreadMentions(const ConvKey& k) {
  const uint32_t mark = readMark(k);
  uint16_t n = 0;
  for (uint16_t i = 0; i < _count; i++) {
    const HistMsg* m = at(i);
    if (m->conv == k && !(m->flags & HF_OUT) && (m->flags & HF_MENTION) && m->id > mark) n++;
  }
  return n;
}

bool History::hasMention(const ConvKey& k) {
  const uint32_t mark = readMark(k);
  for (uint16_t i = 0; i < _count; i++) {
    const HistMsg* m = at(i);
    if (m->conv == k && (m->flags & HF_MENTION) && m->id > mark) return true;
  }
  return false;
}

uint16_t History::totalUnread() {
  ConvKey keys[64];
  const uint16_t n = conversations(keys, 64);
  uint16_t total = 0;
  for (uint16_t i = 0; i < n; i++) total += unread(keys[i]);
  return total;
}

void History::markRead(const ConvKey& k) {
  HistMsg* l = last(k);
  if (!l) return;
  for (uint8_t i = 0; i < _readCount; i++) {
    if (_reads[i].k == k) {
      if (_reads[i].id == l->id) return;
      _reads[i].id = l->id;
      saveReads(); gen++;
      return;
    }
  }
  if (_readCount == READ_MAX) {       // drop the oldest mark
    memmove(_reads, _reads + 1, sizeof(ReadMark) * (READ_MAX - 1));
    _readCount--;
  }
  _reads[_readCount++] = { k, l->id };
  saveReads(); gen++;
}

void History::clearConv(const ConvKey& k) {
  uint32_t upTo = 0;
  for (uint16_t i = 0; i < _count; i++) if (at(i)->conv == k) { if (at(i)->ts > upTo) upTo = at(i)->ts; at(i)->conv.type = 0; }
  noteCleared(&k, upTo);           // cleared on purpose: never put back (history_rescue.cpp)
  appendRecord('D', &k, sizeof(k));
  archive::forget(k);              // and what the SD card kept of it
  gen++;
}

void History::clearAll() {
  uint32_t upTo = 0;
  for (uint16_t i = 0; i < _count; i++) if (at(i)->ts > upTo) upTo = at(i)->ts;
  noteCleared(nullptr, upTo);      // cleared on purpose: never put back (history_rescue.cpp)
  _count = 0; _head = 0;
  archive::forgetAll();
  inwDropPath(LOG_PATH);          // queued records would recreate what's being cleared
  inwDropPath(READS_PATH);
  SPIFFS.remove(LOG_PATH);
  _readCount = 0;
  SPIFFS.remove(READS_PATH);
  gen++;
}

uint16_t History::conversations(ConvKey* out, uint16_t max) {
  uint16_t n = 0;
  for (int i = _count - 1; i >= 0 && n < max; i--) {
    const ConvKey& k = at(i)->conv;
    if (!k.type) continue;
    bool seen = false;
    for (uint16_t j = 0; j < n; j++) if (out[j] == k) { seen = true; break; }
    if (!seen) out[n++] = k;
  }
  return n;
}

void History::saveReads() {
  if (inwQueueReplace(READS_PATH, (const uint8_t*)_reads, sizeof(ReadMark) * _readCount)) return;
  File f = SPIFFS.open(READS_PATH, FILE_WRITE);
  if (!f) return;
  f.write((const uint8_t*)_reads, sizeof(ReadMark) * _readCount);
  f.close();
}

void History::loadReads() {
  File f = SPIFFS.open(READS_PATH, FILE_READ);
  if (!f) return;
  _readCount = (uint8_t)min<size_t>(f.size() / sizeof(ReadMark), READ_MAX);
  f.read((uint8_t*)_reads, sizeof(ReadMark) * _readCount);
  f.close();
}
