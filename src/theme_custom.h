// A theme of your own, made at squatchmesh.com/theme-maker and sent over USB: a
// name, fifteen colours, and which of the built-in looks it wears (the lock scene,
// the card shapes, the sounds and the vibration come from that look).
//
// It arrives as one line of text, and this is the whole of what is accepted:
//
//     1 <look 0-3> <90 hex digits: 15 colours, RRGGBB each, Palette's order> <name>
//
// Nothing here touches hardware, so a PC can test it (sim/test_theme.cpp).
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

struct CustomTheme {
  static constexpr uint8_t NAME_LEN = 20;      // characters
  static constexpr uint8_t COLOURS = 15;       // Palette's fields, in its order
  static constexpr uint8_t LOOKS = 4;          // Squatch, Blocks, Hero, Aurora
  char     name[NAME_LEN + 1] = "";
  uint8_t  look = 0;
  uint32_t colour[COLOURS] = {};
};

namespace themeline {

inline int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// A name the screens can show: the letters, digits and signs of plain ASCII (the
// menu font has no others, so anything else becomes '?'), single spaces, none at
// the ends, NAME_LEN at most. Nothing left: "My theme". out holds NAME_LEN + 1.
inline void cleanName(const char* in, char* out) {
  uint8_t n = 0;
  bool space = false;
  for (const unsigned char* p = (const unsigned char*)(in ? in : ""); *p && n < CustomTheme::NAME_LEN; p++) {
    if (*p == ' ' || *p == '\t') { space = n > 0; continue; }
    if (space) { out[n++] = ' '; space = false; if (n >= CustomTheme::NAME_LEN) break; }
    if (*p >= 0x80) {                              // a UTF-8 character: one '?' for the whole of it
      while ((p[1] & 0xC0) == 0x80) p++;
      out[n++] = '?';
    } else out[n++] = *p < 0x20 || *p == 0x7F ? '?' : (char)*p;
  }
  while (n && out[n - 1] == ' ') n--;              // the cut can land just after a space
  out[n] = 0;
  if (!n) strcpy(out, "My theme");
}

// The line into t. False, with why, unless it is exactly the form above.
inline bool parse(const char* s, CustomTheme& t, const char** why = nullptr) {
  const char* dummy;
  if (!why) why = &dummy;
  if (!s || s[0] != '1' || s[1] != ' ') { *why = "not a version 1 theme"; return false; }
  s += 2;
  if (s[0] < '0' || s[0] >= '0' + CustomTheme::LOOKS || s[1] != ' ') { *why = "unknown look"; return false; }
  t.look = (uint8_t)(s[0] - '0');
  s += 2;
  for (uint8_t i = 0; i < CustomTheme::COLOURS; i++) {
    uint32_t v = 0;
    for (uint8_t k = 0; k < 6; k++) {
      const int d = hexDigit(*s++);
      if (d < 0) { *why = "colours cut short or not hex"; return false; }
      v = (v << 4) | (uint32_t)d;
    }
    t.colour[i] = v;
  }
  if (*s && *s != ' ') { *why = "too many colours"; return false; }
  cleanName(*s ? s + 1 : "", t.name);
  return true;
}

// The same line back (what "themes" lists). out holds 2 + 2 + 90 + 1 + NAME_LEN + 1 or more.
inline void format(const CustomTheme& t, char* out, size_t n) {
  if (n < 96) { if (n) out[0] = 0; return; }
  int k = snprintf(out, n, "1 %u ", (unsigned)(t.look < CustomTheme::LOOKS ? t.look : 0));
  for (uint8_t i = 0; i < CustomTheme::COLOURS; i++) k += snprintf(out + k, n - k, "%06lx", (unsigned long)(t.colour[i] & 0xFFFFFF));
  snprintf(out + k, n - k, " %s", t.name);
}

static constexpr size_t LINE_LEN = 2 + 2 + 90 + 1 + CustomTheme::NAME_LEN * 4 + 1;   // a name sent as 4-byte characters

}  // namespace themeline
