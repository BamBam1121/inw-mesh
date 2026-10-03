#include "history.h"
#include <esp_heap_caps.h>

History history;

static const char* LOG_PATH   = "/hist.log";
static const char* READS_PATH = "/hist_read.bin";
// Routes (History::route) by message id: {id, sz, n, n * sz hash bytes} records. A file
// of its own rather than a new kind of record in the log, which older firmware would
// read as a torn tail and stop at, losing every message after it.
static const char* ROUTE_PATH = "/hist_route.bin";

struct StatusRec { uint32_t id; uint8_t status, attempts, repeats; uint16_t rtt10; } __attribute__((packed));

bool History::begin() {
  _ring = (HistMsg*)heap_caps_calloc(CAP, sizeof(HistMsg), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!_ring) _ring = (HistMsg*)calloc(CAP, sizeof(HistMsg));
  if (!_ring) return false;
  _routes = (Route*)heap_caps_calloc(CAP, sizeof(Route), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  loadLog();
  loadReads();
  loadRoutes();
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
      for (uint16_t i = 0; i < _count; i++) if (at(i)->conv == k) at(i)->conv.type = 0;
    } else if (kind == 'X') {
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

void History::compact() {
  File f = SPIFFS.open("/hist.tmp", FILE_WRITE);
  if (!f) return;
  const char kind = 'N';          // must match what add() writes, or paths are lost
  for (uint16_t i = 0; i < _count; i++) {
    if (!at(i)->conv.type) continue;
    f.write((const uint8_t*)&kind, 1);
    f.write((const uint8_t*)at(i), sizeof(HistMsg));
  }
  f.close();
  SPIFFS.remove(LOG_PATH);
  SPIFFS.rename("/hist.tmp", LOG_PATH);
}

uint32_t History::add(const ConvKey& k, uint8_t flags, uint8_t status, const char* sender,
                      const char* text, uint32_t ts, uint8_t hops, int8_t snr4,
                      const uint8_t* path, uint8_t path_len) {
  HistMsg& m = _ring[_head];
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
  for (uint16_t i = 0; i < _count; i++) if (at(i)->conv == k) at(i)->conv.type = 0;
  appendRecord('D', &k, sizeof(k));
  gen++;
}

void History::clearAll() {
  _count = 0; _head = 0;
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
