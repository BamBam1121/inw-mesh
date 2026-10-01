// The themes the Theme menu lists: the four built in (themes.h), then the owner's
// own (theme_custom.h), kept on the device and nowhere else.
//
// A theme is known by an id, which is what the settings store: 0..THEME_COUNT-1 are
// the built-in ones, and CUSTOM_BASE + slot is one of the owner's. The gap between is
// on purpose: another built-in theme can be added without renumbering anyone's own,
// and firmware from before this (which reads an id it doesn't know as the first
// theme) is not confused by one.
#pragma once
#include "themes.h"
#include "theme_custom.h"

namespace themes {
  static constexpr uint8_t CUSTOM_MAX = 4;
  static constexpr uint8_t CUSTOM_BASE = 16;

  void    load();                          // setup(), before the first applyTheme
  uint8_t count();                         // how many the menu lists
  uint8_t idAt(uint8_t n);                 // the nth of them
  bool    valid(uint8_t id);
  inline bool isCustom(uint8_t id) { return id >= CUSTOM_BASE; }
  const ThemeSpec& spec(uint8_t id);       // an id that isn't there: the first theme
  const CustomTheme* custom(uint8_t id);   // nullptr unless it is one of the owner's
  // Keep t: over the one of the same name if there is one, else in a free slot.
  // Its id; -1 all four slots are taken; -2 it couldn't be written.
  int     put(const CustomTheme& t);
  bool    remove(uint8_t id);
}
