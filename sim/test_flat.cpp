// The T-Deck's flat-battery rule (src/tdeck/flat_hold.h) against what happened on real
// units and against made-up cells: it must stop the restart loop, must never keep a
// healthy T-Deck from starting, and must let a charging one start by itself.
//   export PATH=~/.platformio/packages/toolchain-gccmingw32/bin:$PATH
//   g++ -O2 -std=gnu++14 -I src/tdeck sim/test_flat.cpp -o test_flat.exe -static
// (Smart App Control blocks new programs on this PC: run it on the laptop.)
#include <cstdio>
#include "flat_hold.h"

static int failures = 0;
static void check(bool ok, const char* what) {
  printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) failures++;
}

// One life of a T-Deck from a given start. Each start measures the cell at rest; if it
// goes on, the radio and Wi-Fi sag it by `sag` mV and under 2600 mV the chip resets.
struct Unit {
  double mv;                 // the cell at rest
  double chargeMvPerMin;     // a charger lifting it (0: not plugged in)
  double sag;                // what starting everything costs
  bool waiting = false, brownout = false;
  int starts = 0, brownouts = 0, minutes = 0;
  bool running = false;

  void run(int forMinutes) {
    for (; minutes < forMinutes && !running; ) {
      if (FlatHold::hold((uint16_t)mv, waiting, brownout)) {      // says so and sleeps
        waiting = true; brownout = false;
        minutes += FlatHold::LOOK_EVERY_S / 60;
        mv += chargeMvPerMin * (FlatHold::LOOK_EVERY_S / 60);
        continue;
      }
      waiting = false;
      starts++;
      if (mv - sag < 2600) { brownouts++; brownout = true; mv -= 15; minutes++; mv += chargeMvPerMin; continue; }
      running = true;
    }
  }
};

int main() {
  printf("the starts in the reports (2026-09-30)\n");
  check(FlatHold::hold(2519, false, false), "2519 mV after deep sleep: does not start");
  check(FlatHold::hold(2386, false, true), "2386 mV after a brownout: does not start");
  check(FlatHold::hold(2375, false, true), "2375 mV after a brownout: does not start");
  check(FlatHold::hold(2740, false, false), "2740 mV after deep sleep: does not start");

  printf("a healthy T-Deck always starts\n");
  bool all = true;
  for (int mv = 3300; mv <= 4400; mv += 10) all = all && !FlatHold::hold(mv, false, false);
  check(all, "every reading from 3.30 V (0%) to 4.40 V starts from the switch");
  check(!FlatHold::hold(3620, false, true), "10% (3.62 V) starts even straight after a brownout");
  check(!FlatHold::hold(0, false, false) && !FlatHold::hold(900, true, true), "no cell (USB alone, reads under 1.5 V) always starts");

  printf("the edges\n");
  check(FlatHold::hold(3299, false, false) && !FlatHold::hold(3300, false, false), "a fresh start needs 3.30 V");
  check(FlatHold::hold(3499, true, false) && !FlatHold::hold(3500, true, false), "once waiting it needs 3.50 V");
  check(FlatHold::hold(3599, false, true) && !FlatHold::hold(3600, false, true), "after a brownout it needs 3.60 V");
  check(!FlatHold::hold(3520, true, true), "waiting counts before the brownout: 3.52 V starts");

  printf("whole lives\n");
  { Unit u{2519, 0, 400}; u.run(24 * 60);
    check(u.starts == 0 && u.brownouts == 0, "flat and not plugged in: a day with no start and no brownout (it looped before)"); }
  { Unit u{2519, 12, 400}; u.run(24 * 60);
    printf("        (plugged in at 2.52 V: started after %d min at %.0f mV)\n", u.minutes, u.mv);
    check(u.running && u.brownouts == 0 && u.mv >= 3500, "flat, then on a charger: starts by itself, once, with no brownout"); }
  { Unit u{3340, 0, 900}; u.run(24 * 60);
    check(u.brownouts == 1 && u.starts == 1 && !u.running, "a tired cell that reads 3.34 V but sags: one brownout, then it waits (no loop)"); }
  { Unit u{3340, 12, 900}; u.run(24 * 60);
    check(u.running && u.brownouts <= 2, "the same cell on a charger: starts once it has charged"); }
  { Unit u{3800, 0, 400}; u.run(60);
    check(u.running && u.starts == 1 && u.minutes == 0, "a half-full cell: starts at once"); }

  printf("\n%s\n", failures ? "FAILED" : "ALL PASS");
  return failures ? 1 : 0;
}
