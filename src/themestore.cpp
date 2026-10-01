#include "themestore.h"
#include <Arduino.h>
#include <Preferences.h>

namespace {

struct Slot {
  bool        used = false;
  CustomTheme t;
  ThemeSpec   spec;          // what the rest of the firmware reads: its look's, renamed and recoloured
  char        blurb[44];
};
Slot s_slot[themes::CUSTOM_MAX];

// As kept in flash (Preferences "inw-themes", one key a slot).
#pragma pack(push, 1)
struct Stored {
  uint8_t ver;
  uint8_t look;
  char    name[CustomTheme::NAME_LEN + 1];
  uint8_t rgb[CustomTheme::COLOURS * 3];
};
#pragma pack(pop)
const char* NS = "inw-themes";
const uint8_t VER = 1;

void key(uint8_t slot, char* k) { k[0] = 't'; k[1] = (char)('0' + slot); k[2] = 0; }

void build(Slot& s) {
  const uint8_t look = s.t.look < THEME_COUNT ? s.t.look : 0;
  const uint32_t* c = s.t.colour;
  s.spec = THEMES[look];
  s.spec.name = s.t.name;
  snprintf(s.blurb, sizeof(s.blurb), "your own, on the %s look", THEMES[look].name);
  s.spec.blurb = s.blurb;
  s.spec.palette = { c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], c[8], c[9], c[10], c[11], c[12], c[13], c[14] };
}

bool write(uint8_t slot, const CustomTheme& t) {
  Stored st;
  memset(&st, 0, sizeof(st));
  st.ver = VER;
  st.look = t.look;
  strlcpy(st.name, t.name, sizeof(st.name));
  for (uint8_t i = 0; i < CustomTheme::COLOURS; i++) {
    st.rgb[i * 3] = (uint8_t)(t.colour[i] >> 16);
    st.rgb[i * 3 + 1] = (uint8_t)(t.colour[i] >> 8);
    st.rgb[i * 3 + 2] = (uint8_t)t.colour[i];
  }
  Preferences p;
  if (!p.begin(NS, false)) return false;
  char k[3];
  key(slot, k);
  const bool ok = p.putBytes(k, &st, sizeof(st)) == sizeof(st);
  p.end();
  return ok;
}

}  // namespace

namespace themes {

void load() {
  Preferences p;
  if (!p.begin(NS, true)) return;            // nothing kept yet
  for (uint8_t i = 0; i < CUSTOM_MAX; i++) {
    char k[3];
    key(i, k);
    Stored st;
    Slot& s = s_slot[i];
    s.used = false;
    if (p.getBytesLength(k) != sizeof(st) || p.getBytes(k, &st, sizeof(st)) != sizeof(st)) continue;
    if (st.ver != VER || st.look >= CustomTheme::LOOKS) continue;
    st.name[CustomTheme::NAME_LEN] = 0;
    themeline::cleanName(st.name, s.t.name);
    s.t.look = st.look;
    for (uint8_t c = 0; c < CustomTheme::COLOURS; c++)
      s.t.colour[c] = ((uint32_t)st.rgb[c * 3] << 16) | ((uint32_t)st.rgb[c * 3 + 1] << 8) | st.rgb[c * 3 + 2];
    s.used = true;
    build(s);
  }
  p.end();
}

uint8_t count() {
  uint8_t n = THEME_COUNT;
  for (const Slot& s : s_slot) if (s.used) n++;
  return n;
}

uint8_t idAt(uint8_t n) {
  if (n < THEME_COUNT) return n;
  n -= THEME_COUNT;
  for (uint8_t i = 0; i < CUSTOM_MAX; i++)
    if (s_slot[i].used && n-- == 0) return CUSTOM_BASE + i;
  return 0;
}

bool valid(uint8_t id) {
  if (id < THEME_COUNT) return true;
  return id >= CUSTOM_BASE && id < CUSTOM_BASE + CUSTOM_MAX && s_slot[id - CUSTOM_BASE].used;
}

const ThemeSpec& spec(uint8_t id) {
  if (id < THEME_COUNT) return THEMES[id];
  return valid(id) ? s_slot[id - CUSTOM_BASE].spec : THEMES[0];
}

const CustomTheme* custom(uint8_t id) {
  return isCustom(id) && valid(id) ? &s_slot[id - CUSTOM_BASE].t : nullptr;
}

int put(const CustomTheme& in) {
  CustomTheme t = in;
  themeline::cleanName(in.name, t.name);
  if (t.look >= CustomTheme::LOOKS) t.look = 0;
  for (uint32_t& c : t.colour) c &= 0xFFFFFF;
  int slot = -1;
  for (uint8_t i = 0; i < CUSTOM_MAX && slot < 0; i++)
    if (s_slot[i].used && !strcasecmp(s_slot[i].t.name, t.name)) slot = i;
  for (uint8_t i = 0; i < CUSTOM_MAX && slot < 0; i++)
    if (!s_slot[i].used) slot = i;
  if (slot < 0) return -1;
  if (!write((uint8_t)slot, t)) return -2;
  Slot& s = s_slot[slot];
  s.t = t;
  s.used = true;
  build(s);
  return CUSTOM_BASE + slot;
}

bool remove(uint8_t id) {
  if (!isCustom(id) || !valid(id)) return false;
  const uint8_t slot = id - CUSTOM_BASE;
  Preferences p;
  if (p.begin(NS, false)) {
    char k[3];
    key(slot, k);
    p.remove(k);
    p.end();
  }
  s_slot[slot].used = false;
  return true;
}

}  // namespace themes
