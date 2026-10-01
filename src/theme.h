// Colours for the active theme, resolved once when the theme is applied.

#pragma once
#include "display_config.h"

enum : uint8_t { STYLE_INW = 0, STYLE_BLOCKS, STYLE_HERO, STYLE_AURORA };
// What the lock screen shows, when it isn't the style's own scene. Halloween has the
// Squatch look (cards, screen changes) with its own streets and a dressed-up sasquatch.
enum : uint8_t { SCENE_STYLE = 0, SCENE_HALLOWEEN };

struct Palette {
    uint32_t bg, panel, line, green, greenDim, txt, dim, amber;
    uint32_t red, white, bubbleIn, bubbleOut, blue, focus, mentionBg;
};

struct Theme {
    // `green` is the accent and `greenDim` its secondary; the names predate themes.
    uint16_t bg, panel, line, green, greenDim, txt, dim, amber;
    uint16_t red, white, bubbleIn, bubbleOut, blue, focus, mentionBg;
    uint8_t  style = STYLE_INW;
    bool     own = false;      // one of the owner's own (themestore.h), not a built-in one
    uint8_t  scene = SCENE_STYLE;

    void apply(LGFX& d, const Palette& p, uint8_t s) {
        auto c = [&](uint32_t hex) { return d.color565(hex >> 16, (hex >> 8) & 0xFF, hex & 0xFF); };
        bg = c(p.bg); panel = c(p.panel); line = c(p.line); green = c(p.green);
        greenDim = c(p.greenDim); txt = c(p.txt); dim = c(p.dim); amber = c(p.amber);
        red = c(p.red); white = c(p.white); bubbleIn = c(p.bubbleIn); bubbleOut = c(p.bubbleOut);
        blue = c(p.blue); focus = c(p.focus); mentionBg = c(p.mentionBg);
        style = s;
    }
};
