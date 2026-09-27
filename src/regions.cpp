#include "regions.h"
#include <SPIFFS.h>
#include <helpers/TransportKeyStore.h>
#include "node.h"

bool inwQueueReplace(const char* path, const uint8_t* data, size_t len);   // tools/patch_meshcore.py

namespace regions {
namespace {

// One channel's choice, found by the first bytes of its key (indices move when
// a channel is left).
struct Entry {
  uint8_t id[6];
  char    name[NAME_LEN + 1];              // WHOLE_MESH, or a region name
};
Entry s_list[MAX_GROUP_CHANNELS];
int s_n = 0;
const char* PATH = "/chregion.bin";

void save() {
  const size_t len = sizeof(Entry) * s_n;
  if (inwQueueReplace(PATH, (const uint8_t*)s_list, len)) return;   // written in the background
  File f = SPIFFS.open(PATH, FILE_WRITE);
  if (f) { f.write((const uint8_t*)s_list, len); f.close(); }
}

Entry* find(const uint8_t* secret16) {
  for (int i = 0; i < s_n; i++) if (!memcmp(s_list[i].id, secret16, 6)) return &s_list[i];
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
    if (!isalnum((unsigned char)c) && c != '-' && c != '_') { if (err) *err = "letters, digits, - and _ only"; return false; }
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

const char* defaultName() {
  return g_node ? g_node->prefs().default_scope_name : "";
}

void setDefault(const char* name) {
  if (!g_node) return;
  NodePrefs& p = g_node->prefs();
  if (name && *name) {
    strlcpy(p.default_scope_name, name, sizeof(p.default_scope_name));
    keyFor(p.default_scope_name, p.default_scope_key);
  } else {
    memset(p.default_scope_name, 0, sizeof(p.default_scope_name));
    memset(p.default_scope_key, 0, sizeof(p.default_scope_key));
  }
  g_node->savePrefsNow();
}

const char* forChannel(const uint8_t* secret16) {
  const Entry* e = find(secret16);
  return e ? e->name : "";
}

void setForChannel(const uint8_t* secret16, const char* choice) {
  Entry* e = find(secret16);
  if (!choice || !*choice) {                       // back to the default: drop the entry
    if (!e) return;
    *e = s_list[--s_n];
  } else {
    if (!e) {
      if (s_n >= MAX_GROUP_CHANNELS) {
        // Full of entries for channels since left: forget those first.
        int k = 0;
        for (int i = 0; i < s_n; i++) {
          uint8_t secret6[6];
          memcpy(secret6, s_list[i].id, 6);
          if (g_node && g_node->findChannelBySecret(secret6) >= 0) s_list[k++] = s_list[i];
        }
        s_n = k;
        if (s_n >= MAX_GROUP_CHANNELS) return;
      }
      e = &s_list[s_n++];
      memcpy(e->id, secret16, 6);
    }
    strlcpy(e->name, choice, sizeof(e->name));
  }
  save();
}

String describe(const uint8_t* secret16) {
  const char* c = forChannel(secret16);
  if (!strcmp(c, WHOLE_MESH)) return "whole mesh";
  if (!*c) c = defaultName();
  return *c ? String("#") + c : String("whole mesh");
}

int known(char names[][NAME_LEN + 1], int max) {
  int n = 0;
  auto add = [&](const char* s) {
    if (!*s || !strcmp(s, WHOLE_MESH) || n >= max) return;
    for (int i = 0; i < n; i++) if (!strcmp(names[i], s)) return;
    strlcpy(names[n++], s, NAME_LEN + 1);
  };
  add(defaultName());
  for (int i = 0; i < s_n; i++) add(s_list[i].name);
  return n;
}

void begin() {
  s_n = 0;
  File f = SPIFFS.open(PATH, FILE_READ);
  if (!f) return;
  const size_t len = f.size();
  if (len % sizeof(Entry) == 0 && len <= sizeof(s_list) && f.read((uint8_t*)s_list, len) == (int)len)
    s_n = len / sizeof(Entry);
  f.close();
  for (int i = 0; i < s_n; i++) s_list[i].name[NAME_LEN] = 0;
}

}  // namespace regions
