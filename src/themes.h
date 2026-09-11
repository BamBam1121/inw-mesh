// The four themes: colours, lock-screen scene, sounds and vibration.
//
// Sounds are original tunes written in each theme's spirit, not copies of any
// game's music. Vibration sequences are DRV2605 ERM library-1 effect ids; a byte
// with bit 7 set is a pause of (value & 0x7F) x 10 ms.

#pragma once
#include "theme.h"
#include "audio_jingle.h"

struct VibePattern { uint8_t seq[8]; uint8_t n; };

struct ThemeSpec {
    const char* name;
    const char* blurb;
    Palette palette;
    uint8_t style;
    const Jingle *boot, *msg, *dm, *mention;
    VibePattern vibeMsg, vibeDm, vibeMention;
    uint8_t tickEffect, tickClamp;
};

namespace tunes {
  // INW: clean sine chimes.
  static const ToneStep INW_BOOT[] = {{523, 90}, {659, 90}, {784, 140}};
  static const ToneStep INW_MSG[]  = {{880, 90}, {1318, 110}};
  static const ToneStep INW_DM[]   = {{1047, 90}, {1568, 120}};
  static const ToneStep INW_MEN[]  = {{1318, 70}, {1760, 70}, {2349, 130}};

  // Blocks: soft decaying "piano" notes and a quick rising pop.
  static const ToneStep BLK_BOOT[] = {{262, 260}, {330, 260}, {392, 260}, {523, 520}};
  static const ToneStep BLK_MSG[]  = {{440, 110, 1180}, {0, 35}, {620, 120, 1500}};   // pop-pop
  static const ToneStep BLK_DM[]   = {{659, 220}, {988, 380}};
  static const ToneStep BLK_MEN[]  = {{440, 110, 1180}, {0, 40}, {784, 200}, {1047, 360}};

  // Hero: square-wave chiptune fanfares.
  static const ToneStep HERO_BOOT[] = {{523, 110}, {523, 110}, {784, 110}, {659, 110}, {1047, 360}};
  static const ToneStep HERO_MSG[]  = {{784, 60}, {1047, 60}, {1319, 110}};
  static const ToneStep HERO_DM[]   = {{784, 90}, {880, 90}, {988, 90}, {1175, 260}};
  static const ToneStep HERO_MEN[]  = {{1047, 55}, {1175, 55}, {1319, 55}, {1568, 55}, {2093, 90}, {0, 50}, {2093, 240}};

  // Aurora: slow, bell-like shimmer.
  static const ToneStep AUR_BOOT[] = {{440, 420}, {523, 420}, {659, 420}, {880, 800}};
  static const ToneStep AUR_MSG[]  = {{880, 260}, {1319, 460}};
  static const ToneStep AUR_DM[]   = {{659, 240}, {988, 240}, {1319, 520}};
  static const ToneStep AUR_MEN[]  = {{1047, 160}, {1319, 160}, {1568, 160}, {1976, 600}};

  #define J(name, steps, wave, bell) static const Jingle name = {#name, steps, sizeof(steps) / sizeof(steps[0]), wave, bell}
  J(INW_BOOT_J, INW_BOOT, WAVE_SINE, false);
  J(INW_MSG_J,  INW_MSG,  WAVE_SINE, false);
  J(INW_DM_J,   INW_DM,   WAVE_SINE, false);
  J(INW_MEN_J,  INW_MEN,  WAVE_SINE, false);
  J(BLK_BOOT_J, BLK_BOOT, WAVE_SINE, true);
  J(BLK_MSG_J,  BLK_MSG,  WAVE_TRIANGLE, false);
  J(BLK_DM_J,   BLK_DM,   WAVE_SINE, true);
  J(BLK_MEN_J,  BLK_MEN,  WAVE_TRIANGLE, true);
  J(HERO_BOOT_J, HERO_BOOT, WAVE_SQUARE, false);
  J(HERO_MSG_J,  HERO_MSG,  WAVE_SQUARE, false);
  J(HERO_DM_J,   HERO_DM,   WAVE_SQUARE, false);
  J(HERO_MEN_J,  HERO_MEN,  WAVE_SQUARE, false);
  J(AUR_BOOT_J, AUR_BOOT, WAVE_SINE, true);
  J(AUR_MSG_J,  AUR_MSG,  WAVE_SINE, true);
  J(AUR_DM_J,   AUR_DM,   WAVE_SINE, true);
  J(AUR_MEN_J,  AUR_MEN,  WAVE_TRIANGLE, true);
  #undef J
}

static const ThemeSpec THEMES[] = {
  { "INW", "terminal green, the walking sasquatch",
    { 0x060a09, 0x0b120e, 0x16241c, 0x3dffa8, 0x1f6f4e, 0xb9d4c6, 0x5f8074, 0xe6b955,
      0xff5a5a, 0xe8f2ed, 0x161d1a, 0x0f3d33, 0x4da3ff, 0x122a21, 0x3a3012 },
    STYLE_INW, &tunes::INW_BOOT_J, &tunes::INW_MSG_J, &tunes::INW_DM_J, &tunes::INW_MEN_J,
    {{14, 0x80 | 22, 14, 0x80 | 22, 14}, 5},
    {{14, 0x80 | 22, 14, 0x80 | 22, 14}, 5},
    {{14, 0x80 | 12, 14, 0x80 | 12, 14, 0x80 | 12, 14}, 7},
    7, 0x40 },
  { "Blocks", "grass, dirt and a pixel sky",
    { 0x0e1622, 0x2b2118, 0x45362a, 0x6cc24a, 0x3f7a2c, 0xe8e0d0, 0x9a8f7a, 0xf2b233,
      0xd9412e, 0xffffff, 0x3a3a3a, 0x2f5a22, 0x5aa9e6, 0x3b2d20, 0x4a3a12 },
    STYLE_BLOCKS, &tunes::BLK_BOOT_J, &tunes::BLK_MSG_J, &tunes::BLK_DM_J, &tunes::BLK_MEN_J,
    // Quick taps, like chipping at a block. Click effects (1) are too short to
    // spin this ERM up, so these use the short buzz (47).
    {{47, 0x80 | 5, 47}, 3},
    {{47, 0x80 | 5, 47, 0x80 | 5, 47}, 5},
    {{14, 0x80 | 8, 47, 0x80 | 5, 47, 0x80 | 5, 47}, 7},
    1, 0x50 },
  { "Hero", "night hills, a hooded adventurer, hearts",
    { 0x081420, 0x10263a, 0x1d3a52, 0xf2c14e, 0x2e7d4f, 0xe6edf2, 0x7f98ab, 0xff9f43,
      0xe84a5f, 0xffffff, 0x13283a, 0x1e4a33, 0x5cc8ff, 0x163248, 0x3a2d10 },
    STYLE_HERO, &tunes::HERO_BOOT_J, &tunes::HERO_MSG_J, &tunes::HERO_DM_J, &tunes::HERO_MEN_J,
    {{47, 0x80 | 8, 47}, 3},
    {{47, 0x80 | 8, 47, 0x80 | 8, 14}, 5},                  // da-da-daaa
    {{47, 0x80 | 6, 47, 0x80 | 6, 47, 0x80 | 6, 14}, 7},
    7, 0x40 },
  { "Aurora", "northern lights over the ridge",
    { 0x040716, 0x0b1230, 0x18214a, 0x72f5c8, 0x6a4fc2, 0xd6e4ff, 0x7c86b8, 0xffc46b,
      0xff6b8b, 0xffffff, 0x121a3e, 0x1b3a4a, 0x7db8ff, 0x141d45, 0x2c2250 },
    STYLE_AURORA, &tunes::AUR_BOOT_J, &tunes::AUR_MSG_J, &tunes::AUR_DM_J, &tunes::AUR_MEN_J,
    // Slow waves. The ramp effects (70/82) are too gentle to spin this ERM up,
    // so the swell comes from strong pulses (52) with long gaps between them.
    {{52, 0x80 | 12, 52}, 3},
    {{52, 0x80 | 12, 52, 0x80 | 12, 52}, 5},
    {{14, 0x80 | 10, 52, 0x80 | 8, 52, 0x80 | 8, 52}, 7},
    7, 0x28 },
};
static constexpr uint8_t THEME_COUNT = sizeof(THEMES) / sizeof(THEMES[0]);
