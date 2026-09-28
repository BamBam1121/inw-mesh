// The touch gestures (src/touch.h) against made-up finger traces: taps with a
// wobble, slow drags, flicks that glide on, a finger that catches a glide, and
// screens that don't glide. Prints what came out and checks it.
//   g++ -O2 -std=gnu++14 -I sim/stub -I src sim/test_touch.cpp -o test_touch.exe -static
// (Smart App Control blocks new programs on this PC: run it on the laptop.)
#include <cstdio>
#include "touch.h"

static int fails = 0;
static void check(bool ok, const char* what) { printf("  %s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) fails++; }

struct Out { int taps = 0, ups = 0, longs = 0, swipes = 0, drags = 0, coastDrags = 0, coastDy = 0; uint32_t coastEnd = 0; char dir = 0; };

// One sample every `step` ms, as the loop polls the panel.
static void run(Gestures& g, Out& o, uint32_t& t, bool down, int x, int y) {
  TouchEvent e;
  while (g.feed(down, x, y, t, e)) {
    switch (e.type) {
      case TouchEvent::Tap: o.taps++; break;
      case TouchEvent::Up: o.ups++; break;
      case TouchEvent::LongPress: o.longs++; break;
      case TouchEvent::Swipe: o.swipes++; o.dir = e.dir; break;
      case TouchEvent::Drag: if (e.coast) { o.coastDrags++; o.coastDy += e.dy; o.coastEnd = t; } else o.drags++; break;
      default: break;
    }
  }
}
static void idle(Gestures& g, Out& o, uint32_t& t, uint32_t ms, uint32_t step = 16) {
  for (uint32_t i = 0; i < ms; i += step) { t += step; run(g, o, t, false, -1, -1); }
}
// A straight stroke from (x0,y0) to (x1,y1) over `ms`, then held `hold` ms, then lifted.
static void stroke(Gestures& g, Out& o, uint32_t& t, int x0, int y0, int x1, int y1, uint32_t ms, uint32_t hold = 0, uint32_t step = 16) {
  run(g, o, t, true, x0, y0);
  for (uint32_t e = step; e <= ms; e += step) { t += step; run(g, o, t, true, x0 + (x1 - x0) * (int)e / (int)ms, y0 + (y1 - y0) * (int)e / (int)ms); }
  for (uint32_t e = 0; e < hold; e += step) { t += step; run(g, o, t, true, x1, y1); }
  t += step; run(g, o, t, false, -1, -1);
}

int main() {
  uint32_t t = 1000;
  { puts("a tap that wobbles 12 px is still a tap");
    Gestures g; Out o; stroke(g, o, t, 100, 100, 108, 112, 80); idle(g, o, t, 300);
    check(o.taps == 1 && o.drags == 0 && o.coastDrags == 0, "one tap, nothing moved"); }
  { puts("a flick up glides on, slows, and stops by itself");
    Gestures g; Out o; stroke(g, o, t, 160, 200, 160, 80, 90); idle(g, o, t, 4000);
    printf("  glided %d px in %d steps, done %u ms after the lift\n", o.coastDy, o.coastDrags, o.coastEnd ? o.coastEnd - (t - 4000) : 0);
    check(o.taps == 0 && o.swipes == 1 && o.dir == 'U', "Up then a Swipe U, no tap");
    check(o.coastDy < -150 && o.coastDy > -1400, "glides the way the finger went, a sane distance");
    check(!g.coasting() && o.coastEnd && o.coastEnd - (t - 4000) < 2500, "comes to rest inside 2.5 s"); }
  { puts("a flick down glides down");
    Gestures g; Out o; stroke(g, o, t, 160, 60, 160, 180, 100); idle(g, o, t, 3000);
    check(o.coastDy > 150, "positive glide"); }
  { puts("a slow drag that stops before the lift: no glide");
    Gestures g; Out o; stroke(g, o, t, 160, 200, 160, 120, 600, 200); idle(g, o, t, 1500);
    check(o.drags > 0 && o.coastDrags == 0 && o.taps == 0, "dragged, no glide, no tap"); }
  { puts("a finger on a gliding list stops it and opens nothing");
    Gestures g; Out o; stroke(g, o, t, 160, 200, 160, 60, 80); idle(g, o, t, 150);
    const int before = o.coastDrags;
    check(g.coasting() && before > 0, "gliding before the catch");
    stroke(g, o, t, 150, 120, 152, 121, 60); idle(g, o, t, 1000);
    check(o.coastDrags == before, "no glide after the catch");
    check(o.taps == 0 && o.longs == 0, "the catch is not a tap");
    stroke(g, o, t, 150, 120, 150, 120, 50); idle(g, o, t, 300);
    check(o.taps == 1, "the next touch taps normally"); }
  { puts("a list that doesn't glide (map, lock face)");
    Gestures g; Out o; g.allowCoast(false); stroke(g, o, t, 160, 200, 160, 60, 80); idle(g, o, t, 2000);
    check(o.coastDrags == 0, "no glide"); }
  { puts("a sideways flick doesn't glide (back swipe)");
    Gestures g; Out o; stroke(g, o, t, 10, 120, 200, 130, 100); idle(g, o, t, 2000);
    check(o.swipes == 1 && o.dir == 'R' && o.coastDrags == 0, "Swipe R, no glide"); }
  { puts("a new screen ends a glide");
    Gestures g; Out o; stroke(g, o, t, 160, 200, 160, 60, 80); idle(g, o, t, 100); g.stopCoast();
    const int n = o.coastDrags; idle(g, o, t, 1500);
    check(o.coastDrags == n, "nothing after stopCoast"); }
  { puts("a long press still fires");
    Gestures g; Out o; stroke(g, o, t, 100, 100, 101, 101, 32, 800); idle(g, o, t, 200);
    check(o.longs == 1 && o.taps == 0, "long press, no tap"); }
  { puts("a slow loop (40 ms between samples) flicks the same");
    Gestures g; Out o; stroke(g, o, t, 160, 200, 160, 80, 120, 0, 40); idle(g, o, t, 3000, 40);
    printf("  glided %d px\n", o.coastDy);
    check(o.coastDy < -150, "glides"); }
  printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
