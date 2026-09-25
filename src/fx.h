// The animation layer. Short effects drawn over whatever the screen shows (rings
// for each repeat heard, sparks when a message goes, a tick when it's delivered, a
// splash when the charger goes in), and the transitions when the screen turns on,
// off, and when the pager powers down.
//
// Everything is drawn in the style of the current theme:
//   Squatch  a terminal: CRT warm-up and collapse, radar rings, spark streaks
//   Blocks   pixels: tiles that build and break, square rings, debris that falls
//   Hero     a cartoon: iris wipes, hearts, gold stars
//   Aurora   light: curtains with a wavy seam, soft coloured rings, drifting motes

#pragma once
#include "ui.h"

namespace fx {

// ---- overlays: fire and forget; Nav keeps redrawing while any is alive ----------
void ping(int x, int y);             // a repeat heard
void burst(int x, int y);            // a message sent
void check(int x, int y);            // delivered
void fail();                         // failed: a shake and a red flash
void charge(uint8_t pct);            // the charger went in
bool active();
void draw(lgfx::LovyanGFX& g);       // from Nav::draw, after the view and overlays
int  shakeX();                       // horizontal offset while shaking

// ---- radar (Discover): the scope, and a blip for each answer ------------------------
// sweep: radians, or < 0 once listening is over. A blip is drawn at x,y; `fresh`
// (0..1) is how new it is, 1 just landed; `focus` marks the selected one.
void radar(lgfx::LovyanGFX& g, int cx, int cy, int r, float sweep);
void blip(lgfx::LovyanGFX& g, int x, int y, float fresh, bool focus);

// ---- transitions: block a few hundred ms, drawing to the panel ----------------------
// `frame` holds the picture being revealed (on) or turned off (off / power down).
void screenOn(Canvas& frame);
void screenOff(Canvas& frame);
void powerDown(Canvas& frame);

// ---- screen to screen ------------------------------------------------------------------
// From `from` (the picture that was showing) to `to` (the new screen, already
// composed), drawn to the panel. Nav calls this for every push and pop.
enum class Trans : uint8_t { None, Forward, Back, Unlock, Lock };
void transition(Trans kind, Canvas& from, Canvas& to);
// The same, stopped atMs in and drawn into dst instead of the panel (USB checks).
void transitionFrame(Trans kind, Canvas& from, Canvas& to, lgfx::LovyanGFX& dst, uint32_t atMs);

// For checking frames over USB: one moment of a transition, drawn into dst.
// kind: 0 on, 1 off, 2 power down.
void render(uint8_t kind, Canvas& frame, lgfx::LovyanGFX& dst, float p);
Canvas* scratch();                   // a full-screen PSRAM sprite, made once

}  // namespace fx
