#include "regions.h"
#include <SPIFFS.h>
#include <helpers/TransportKeyStore.h>
#include "node.h"
#include "app.h"
#include <algorithm>

bool inwQueueReplace(const char* path, const uint8_t* data, size_t len);   // tools/patch_meshcore.py
void markPrefsDirty();                                                      // main.cpp

namespace regions {
namespace {

// One channel's region, found by the first bytes of its key (indices move when a
// channel is left).
struct Entry {
  uint8_t id[6];
  char    name[NAME_LEN + 1];
};
Entry s_ch[MAX_GROUP_CHANNELS];
int s_nch = 0;
char s_list[LIST_MAX][NAME_LEN + 1];      // the regions added, as the app lists them
int s_nlist = 0;
const char* CH_PATH = "/chregion.bin";
const char* LIST_PATH = "/regions.bin";

void write(const char* path, const void* data, size_t len) {
  if (inwQueueReplace(path, (const uint8_t*)data, len)) return;   // written in the background
  File f = SPIFFS.open(path, FILE_WRITE);
  if (f) { f.write((const uint8_t*)data, len); f.close(); }
}
void saveChannels() { write(CH_PATH, s_ch, sizeof(Entry) * s_nch); }
void saveList() { write(LIST_PATH, s_list, sizeof(s_list[0]) * s_nlist); }

Entry* find(const uint8_t* secret16) {
  for (int i = 0; i < s_nch; i++) if (!memcmp(s_ch[i].id, secret16, 6)) return &s_ch[i];
  return nullptr;
}

}  // namespace

bool clean(const char* in, char* out, size_t cap, const char** err) {
  while (*in == ' ') in++;
  if (*in == '#') in++;
  size_t n = 0;
  for (; *in && *in != ' '; in++) {
    const char c = *in;
    if (c == '$') { if (err) *err = "private regions ($) aren't supported yet"; return false; }
    if (!isalnum((unsigned char)c) && c != '-') { if (err) *err = "letters, digits and - only"; return false; }
    if (n + 1 >= cap || n >= NAME_LEN) { if (err) *err = "too long"; return false; }
    out[n++] = c;
  }
  out[n] = 0;
  if (!n) { if (err) *err = "no name"; return false; }
  return true;
}

void keyFor(const char* name, uint8_t key[16]) {
  char tag[NAME_LEN + 2];
  snprintf(tag, sizeof(tag), "#%s", name);
  TransportKeyStore store;
  TransportKey k;
  store.getAutoKeyFor(0, tag, k);
  memcpy(key, k.key, 16);
}

int list(char names[][NAME_LEN + 1], int max) {
  const int n = min(max, s_nlist);
  for (int i = 0; i < n; i++) strlcpy(names[i], s_list[i], NAME_LEN + 1);
  return n;
}

void add(const char* name) {
  if (!name || !*name) return;
  for (int i = 0; i < s_nlist; i++) if (!strcmp(s_list[i], name)) return;
  if (s_nlist >= LIST_MAX) {                  // full: the oldest goes, unless it's in use
    int drop = -1;
    for (int i = 0; i < s_nlist && drop < 0; i++) {
      bool used = !strcmp(s_list[i], defaultName());
      for (int k = 0; k < s_nch && !used; k++) used = !strcmp(s_ch[k].name, s_list[i]);
      if (!used) drop = i;
    }
    if (drop < 0) return;
    for (int i = drop; i + 1 < s_nlist; i++) memcpy(s_list[i], s_list[i + 1], sizeof(s_list[0]));
    s_nlist--;
  }
  strlcpy(s_list[s_nlist++], name, NAME_LEN + 1);
  saveList();
}

void remove(const char* name) {
  for (int i = 0; i < s_nlist; i++) {
    if (strcmp(s_list[i], name)) continue;
    for (int k = i; k + 1 < s_nlist; k++) memcpy(s_list[k], s_list[k + 1], sizeof(s_list[0]));
    s_nlist--;
    saveList();
    return;
  }
}

const char* defaultName() {
  return g_node ? g_node->prefs().default_scope_name : "";
}

void setDefault(const char* name) {
  if (!g_node) return;
  NodePrefs& p = g_node->prefs();
  if (name && *name) {
    strlcpy(p.default_scope_name, name, sizeof(p.default_scope_name));
    keyFor(p.default_scope_name, p.default_scope_key);
    add(name);
  } else {
    memset(p.default_scope_name, 0, sizeof(p.default_scope_name));
    memset(p.default_scope_key, 0, sizeof(p.default_scope_key));
  }
  markPrefsDirty();          // saved shortly, as the radio settings are: no stall here
}

const char* forChannel(const uint8_t* secret16) {
  const Entry* e = find(secret16);
  return e ? e->name : "";
}

void setForChannel(const uint8_t* secret16, const char* name) {
  Entry* e = find(secret16);
  if (!name || !*name) {                           // cleared: back to the default
    if (!e) return;
    *e = s_ch[--s_nch];
  } else {
    if (!e) {
      if (s_nch >= MAX_GROUP_CHANNELS) {
        // Full of entries for channels since left: forget those first.
        int k = 0;
        for (int i = 0; i < s_nch; i++) {
          uint8_t secret6[6];
          memcpy(secret6, s_ch[i].id, 6);
          if (g_node && g_node->findChannelBySecret(secret6) >= 0) s_ch[k++] = s_ch[i];
        }
        s_nch = k;
        if (s_nch >= MAX_GROUP_CHANNELS) return;
      }
      e = &s_ch[s_nch++];
      memcpy(e->id, secret16, 6);
    }
    strlcpy(e->name, name, sizeof(e->name));
    add(name);
  }
  saveChannels();
}

const char* effective(const uint8_t* secret16) {
  const char* c = forChannel(secret16);
  return *c ? c : defaultName();
}

// ---- asking the repeaters in range ----------------------------------------------------
namespace {
constexpr uint32_t LISTEN_MS = 12000, ASK_MS = 5000;   // discover's listening time; one answer's wait
}

bool Scan::start() {
  *this = Scan();
  _started = true;
  _at = millis();
  if (!g_node || !g_node->discover()) { _failed = _done = true; return false; }
  _gen = g_node->regionsGen;
  return true;
}

bool Scan::tick() {
  if (!_started || _done || !g_node) return false;
  bool changed = false;
  if (_asking) {
    if (g_node->regionsGen != _gen) {
      _gen = g_node->regionsGen;
      record(g_node->regionsReply.names);
      _asking = false;
      changed = true;
    } else if (millis() - _askedAt > ASK_MS) {
      _asking = false;
      silent++;
      changed = true;
    }
  }
  while (!_asking && _next < g_node->discoveredCount) {
    const DiscoverHit& h = g_node->discovered[_next++];
    if (h.type != ADV_TYPE_REPEATER) continue;
    if (!g_node->contact(h.pub)) {
      ContactInfo ci;
      memset(&ci, 0, sizeof(ci));
      ci.id = mesh::Identity(h.pub);
      mesh::Utils::toHex(ci.name, h.pub, 4);   // its next advert names it
      ci.type = ADV_TYPE_REPEATER;
      ci.out_path_len = 0;
      ci.lastmod = app::now();
      if (!g_node->addContact(ci)) { full++; changed = true; continue; }
      added++;
    }
    if (g_node->requestRegions(h.pub)) { _asking = true; _askedAt = millis(); changed = true; }
  }
  if (!_asking && millis() - _at > LISTEN_MS && _next >= g_node->discoveredCount) { _done = true; changed = true; }
  return changed;
}

// "*,spokane,wa," from one repeater.
void Scan::record(const char* list) {
  answered++;
  char tok[NAME_LEN + 2];
  size_t n = 0;
  for (const char* p = list;; p++) {
    if (*p && *p != ',') { if (n + 1 < sizeof(tok)) tok[n++] = *p; continue; }
    tok[n] = 0;
    if (!strcmp(tok, "*")) wholeMesh++;
    else if (n && tok[0] != '$') {                 // private regions need a key we don't have
      char nm[NAME_LEN + 1];
      if (clean(tok, nm, sizeof(nm))) {
        int i = 0;
        while (i < _n && strcmp(_names[i], nm)) i++;
        if (i < _n) _counts[i]++;
        else if (_n < MAX_NAMES) { strlcpy(_names[_n], nm, sizeof(_names[0])); _counts[_n++] = 1; }
      }
    }
    n = 0;
    if (!*p) break;
  }
  sort();
}

void Scan::sort() {
  for (int i = 0; i < _n; i++) _order[i] = i;
  std::sort(_order, _order + _n, [this](int a, int b) { return _counts[a] > _counts[b]; });
}

void begin() {
  s_nch = 0;
  s_nlist = 0;
  File f = SPIFFS.open(CH_PATH, FILE_READ);
  if (f) {
    const size_t len = f.size();
    if (len % sizeof(Entry) == 0 && len <= sizeof(s_ch) && f.read((uint8_t*)s_ch, len) == (int)len)
      s_nch = len / sizeof(Entry);
    f.close();
  }
  int k = 0;
  for (int i = 0; i < s_nch; i++) {
    s_ch[i].name[NAME_LEN] = 0;
    if (s_ch[i].name[0] && s_ch[i].name[0] != '*') s_ch[k++] = s_ch[i];   // "*" was a beta's "no region"
  }
  s_nch = k;
  File g = SPIFFS.open(LIST_PATH, FILE_READ);
  if (g) {
    const size_t len = g.size();
    if (len % sizeof(s_list[0]) == 0 && len <= sizeof(s_list) && g.read((uint8_t*)s_list, len) == (int)len)
      s_nlist = len / sizeof(s_list[0]);
    g.close();
  }
  for (int i = 0; i < s_nlist; i++) s_list[i][NAME_LEN] = 0;
  // Regions in use belong on the list (the default may have come from the app).
  if (*defaultName()) add(defaultName());
  for (int i = 0; i < s_nch; i++) add(s_ch[i].name);
}

}  // namespace regions
