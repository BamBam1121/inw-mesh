// The T-Deck battery estimate (src/tdeck/battery_est.h) against made-up traces of what
// the pin really shows: the cell on battery, the USB supply less a diode when plugged in
// (LilyGo's schematic; measured on a T-Deck 2026-10-09: 4.46-4.57 V on USB).
// Checks that it sees the cable go in and out, never takes the supply for a full cell,
// never jumps, comes back to the truth after unplugging, and learns how fast a cell
// fills: the usual 2,000 mAh one and a 10,000 mAh one.
//   export PATH=~/.platformio/packages/toolchain-gccmingw32/bin:$PATH
//   g++ -O2 -std=gnu++14 -I src/tdeck sim/test_battery.cpp -o test_battery.exe -static
// (Smart App Control blocks new programs on this PC: run it on the laptop.)
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include "battery_est.h"

// Open-circuit voltage for a state of charge, the inverse of the estimate's curve.
static double ocv(double soc) {
  static const double MV[]  = {4180, 4100, 4000, 3920, 3850, 3800, 3750, 3710, 3670, 3620, 3500, 3300};
  static const double PCT[] = { 100,   90,   80,   70,   60,   50,   40,   30,   20,   10,    5,    0};
  if (soc >= 100) return 4180;
  for (int i = 1; i < 12; i++)
    if (soc >= PCT[i]) return MV[i] + (MV[i - 1] - MV[i]) * (soc - PCT[i]) / (PCT[i - 1] - PCT[i]);
  return 3300;
}

struct World {
  double soc = 30, capMah = 2000;   // the cell
  double r = 0.15;                  // ohm: the cell, its lead and the switch it comes through
  double surf = 0;                  // mV it still reads high after a charge
  double vbus = 5.05, cable = 0.25; // the USB supply at no load, and the cable
};

static int failures = 0, shown = 0;
static void check(bool ok, const char* what, double t) {
  if (ok) return;
  failures++;
  if (shown++ < 4) printf("  FAIL at %.1f min: %s\n", t / 60000.0, what);
}

// One stretch: so long, on this power (0 battery, 1 wall charger, 2 computer), drawing this much.
struct Step { double minutes; int power; double loadA; };

struct Limits {
  double battery = 6;       // on battery, 12 minutes after the last change: within this of the truth
  double usb = -1;          // on USB, over the whole stretch: within this (-1: not looked at)
  bool neverEarly100 = true;
  bool timeToFull = false;  // the time it gives as the cable goes in is checked against when the cell is full
};

static void run(const char* name, World& w, BatteryEstimate& e, uint32_t& t, const Step* st, int n, unsigned seed,
                const Limits& lim, bool quiet = false) {
  printf("\n== %s\n", name);
  srand(seed);
  shown = 0;
  bool first = t == 0;
  int lastPct = e.percent, lastExt = e.external, flips = 0, real = 0;
  double lastLoad = -1, worstBat = 0, worstUsb = 0;
  uint32_t changedAt = t, t0 = t, saidAt = 0;
  int lastPower = st[0].power, said = -1;
  for (int k = 0; k < n; k++) {
    const uint32_t end = t + (uint32_t)(st[k].minutes * 60000);
    const int power = st[k].power;
    if (power != lastPower) { real++; changedAt = t; lastPower = power; }
    // What main.cpp reports: its own rough figure, here 20% off the truth.
    if (fabs(st[k].loadA - lastLoad) > 0.02) { if (lastLoad >= 0) e.drawChanged(t, (int)((st[k].loadA - 0.06) * 1000 * 0.8)); else e.drawChanged(t, (int)((st[k].loadA - 0.06) * 1000 * 0.8)); }
    lastLoad = st[k].loadA;
    for (; t < end; t += BatteryEstimate::PERIOD_MS) {
      const double dt = BatteryEstimate::PERIOD_MS / 1000.0;
      double pin;
      if (power) {
        // The charger: 0.5 A until the cell nears full, then less and less. The T-Deck
        // itself runs off USB, and the pin shows that supply less a diode.
        const double v = ocv(w.soc);
        const double rc = 300.0 / w.capMah;                          // ohm: a bigger cell has less
        double chargeI = (4200 - v) / 1000.0 / rc;                   // what the cell takes with 4.2 V held on it,
        if (chargeI > 0.5) chargeI = 0.5;                            // and the charger gives no more than this
        if (chargeI < 0.02) chargeI = 0;
        w.soc += chargeI * 1000 * (dt / 3600.0) / w.capMah * 100;
        if (w.soc > 100) w.soc = 100;
        w.surf = chargeI > 0 ? 35 : w.surf * exp(-dt / 600.0);
        pin = (w.vbus - w.cable * (st[k].loadA + chargeI)) * 1000 - 450;
      } else {
        pin = ocv(w.soc) - st[k].loadA * w.r * 1000 + w.surf;
        w.surf *= exp(-dt / 120.0);
        w.soc -= st[k].loadA * 1000 * (dt / 3600.0) / w.capMah * 100;
        if (w.soc < 0) w.soc = 0;
      }
      pin += rand() % 25 - 12;                                       // ADC noise
      if ((t / 2000) % 29 == 7) pin -= power ? 50 : 110;             // a transmit burst
      e.update(t, (uint16_t)pin, power == 2);
      if (first) { lastPct = e.percent; lastExt = e.external; first = false; }
      const double tm = t;
      if (t - t0 > 60000) check(abs((int)e.percent - lastPct) <= 1, "the figure jumped", tm);
      if (lim.neverEarly100) check(!(e.percent >= 100 && w.soc < 90), "says 100% with the cell under 90%", tm);
      if (lastExt != (int)e.external) {
        flips++;
        if (!quiet) printf("  %6.1f min  %s (true %3.0f%%, shown %3u%%)\n", tm / 60000.0, e.external ? "ON USB" : "on battery", w.soc, e.percent);
        if (e.external && lim.timeToFull) { said = e.minutesToFull(); saidAt = t; }
      }
      if (said >= 0 && power && w.soc >= 99.5) {
        const double took = (t - saidAt) / 60000.0, out = fabs(took - said);
        printf("  time to full: said %d min as the cable went in, took %.0f min (out by %.0f)\n", said, took, out);
        check(out <= (took * 0.12 > 25 ? took * 0.12 : 25), "the time to full was out by more than 12% (or 25 minutes)", tm);
        said = -1;
      }
      const double off = fabs(e.percent - w.soc);
      if (!power && t - changedAt > 12 * 60000 && off > worstBat) worstBat = off;
      if (power && off > worstUsb) worstUsb = off;
      if (!quiet && t % 1800000 == 0)
        printf("  %6.1f min  pin %4.0f  true %3.0f%%  shown %3u%%  %s%s  rate %.1f\n", tm / 60000.0, pin, w.soc, e.percent,
               e.external ? "usb" : "battery", e.full ? " FULL" : "", e.rate);
      lastPct = e.percent;
      lastExt = e.external;
    }
    check(e.external == (power != 0), power ? "the cable going in was missed" : "the cable coming out was missed", t);
  }
  check(flips == real, "it changed its mind about the cable more often than the cable moved", t);
  check(worstBat <= lim.battery, "on battery, further from the truth than allowed", t);
  if (lim.usb >= 0) check(worstUsb <= lim.usb, "on USB, further from the truth than allowed", t);
  printf("  end: true %.0f%%, shown %u%%, rate %.1f%%/h; worst on battery %.0f, on usb %.0f; cable moved %d times, seen %d\n",
         w.soc, e.percent, e.rate, worstBat, worstUsb, real, flips);
}

// On battery all the way down, the same hour of use over and over (twenty minutes of
// screen, forty dark): is the time it says is left, an hour in and later, within a
// quarter of what really was? Only once it says the rate has been seen; before that it
// is a guess from the cell's size, and is printed but not held to anything.
static void runDown(const char* name, double capMah, double soc0, float chargeRate, unsigned seed) {
  printf("\n== %s\n", name);
  srand(seed);
  shown = 0;
  World w; w.soc = soc0; w.capMah = capMah; w.r = 300.0 / capMah + 0.1;
  BatteryEstimate e;
  e.rate = chargeRate;
  struct Said { uint32_t at; int mins; bool known; int pct; };
  Said said[10]; int ns = 0;
  double load = -1, next = 0;
  uint32_t t = 0;
  for (;; t += BatteryEstimate::PERIOD_MS) {
    const double dt = BatteryEstimate::PERIOD_MS / 1000.0, minute = t / 60000.0;
    const double l = fmod(minute, 60.0) < 20.0 ? 0.25 : 0.06;
    if (fabs(l - load) > 0.01) { e.drawChanged(t, (int)((l - 0.06) * 1000 * 0.8)); load = l; }
    double pin = ocv(w.soc) - l * w.r * 1000 + (rand() % 25 - 12);
    if ((t / 2000) % 29 == 7) pin -= 110;
    w.soc -= l * 1000 * (dt / 3600.0) / w.capMah * 100;
    e.update(t, (uint16_t)pin, false);
    if (minute >= next && ns < 10 && w.soc > 10) {
      said[ns++] = {t, (int)e.minutesLeft(t), e.leftKnown(t), (int)e.percent};
      next = next == 0 ? 60 : next * 2;
    }
    if (w.soc <= 1.0) break;
  }
  for (int i = 0; i < ns; i++) {
    const double really = (t - said[i].at) / 60000.0;
    printf("  %6.1f h in, at %3d%%: said %5.1f h left (%s), really %5.1f h\n", said[i].at / 3600000.0, said[i].pct,
           said[i].mins / 60.0, said[i].known ? "seen" : "a guess", really / 60.0);
    if (said[i].known) check(fabs(said[i].mins - really) <= really * 0.25, "the time left was out by more than a quarter", said[i].at);
  }
}

int main() {
  runDown("the usual 2,000 mAh cell from 90%, run flat: the time it says is left", 2000, 90, 25.0f, 31);
  runDown("a 10,000 mAh cell from 90%, run flat (its charge rate known: 5% an hour)", 10000, 90, 5.0f, 32);
  {
    World w; w.soc = 30; BatteryEstimate e; uint32_t t = 0;
    const Step s[] = {{5, 0, 0.12}, {60, 1, 0.12}, {30, 0, 0.12}};
    Limits l; l.usb = 8;
    run("the usual 2,000 mAh cell at 30%: an hour on a wall charger, then unplugged", w, e, t, s, 3, 1, l);
  }
  {
    World w; w.soc = 30; BatteryEstimate e; uint32_t t = 0;
    const Step s[] = {{15, 0, 0.12}, {300, 1, 0.06}, {20, 0, 0.12}};
    Limits l; l.usb = 8; l.timeToFull = true;
    run("the usual cell at 30%, charged to full: is the time it gave right?", w, e, t, s, 3, 2, l);
  }
  {
    World w; w.soc = 50; BatteryEstimate e; uint32_t t = 0;
    const Step s[] = {{5, 0, 0.12}, {90, 2, 0.20}, {15, 0, 0.12}};
    Limits l; l.usb = 10;
    run("at 50%, plugged into a computer for an hour and a half", w, e, t, s, 3, 3, l);
  }
  {
    World w; w.soc = 85; BatteryEstimate e; uint32_t t = 0;
    const Step s[] = {{5, 0, 0.12}, {300, 1, 0.06}, {20, 0, 0.12}};
    Limits l;
    run("at 85%, left on the charger five hours, then unplugged full", w, e, t, s, 3, 7, l);
    check(e.percent >= 95, "a full cell just off the charger doesn't read full", t);
  }
  // The owner's 10,000 mAh cell: the first charge is reckoned at the usual cell's rate
  // (too fast: that one is allowed), and from the second on it is right.
  {
    World w; w.soc = 22; w.capMah = 10000; w.r = 0.08; BatteryEstimate e; uint32_t t = 0;
    const Step a[] = {{10, 0, 0.12}, {240, 1, 0.06}, {20, 0, 0.12}};
    Limits first; first.neverEarly100 = false;
    run("a 10,000 mAh cell at 22%, first charge (four hours)", w, e, t, a, 3, 11, first);
    check(fabs(e.rate - 5.0) < 1.5, "after one charge the rate isn't near 5% an hour", t);
    const Step b[] = {{180, 0, 0.20}, {300, 1, 0.06}, {20, 0, 0.12}};
    Limits second; second.usb = 6;
    run("the same cell, used three hours, charged five", w, e, t, b, 3, 12, second);
    const Step c[] = {{600, 0, 0.15}, {840, 1, 0.06}, {20, 0, 0.12}};
    second.timeToFull = true;
    run("and again: ten hours' use, then charged to full (the time it gave, with the rate it learned)", w, e, t, c, 3, 13, second);
  }
  // Told the rate by hand, as the owner's T-Deck was: right from the first charge.
  {
    World w; w.soc = 22; w.capMah = 10000; w.r = 0.08; BatteryEstimate e; uint32_t t = 0;
    e.rate = 5.0f;
    const Step s[] = {{15, 0, 0.12}, {1080, 1, 0.06}, {20, 0, 0.12}};
    Limits l; l.usb = 6; l.timeToFull = true;
    run("a 10,000 mAh cell with its rate set by hand, charged from 22% to full", w, e, t, s, 3, 14, l);
  }
  {
    World w; w.soc = 70; BatteryEstimate e; uint32_t t = 0;
    const Step s[] = {{30, 1, 0.12}, {20, 0, 0.12}};
    Limits l;
    run("started up on a wall charger with nothing remembered (the cell is at 70%)", w, e, t, s, 2, 5, l);
  }
  {
    World w; w.soc = 60; BatteryEstimate e; uint32_t t = 0;
    e.remember(62);
    const Step s[] = {{45, 1, 0.12}, {20, 0, 0.12}};
    Limits l; l.usb = 12;
    run("started up on a charger remembering 62% (the cell is at 60%)", w, e, t, s, 2, 6, l);
  }
  {
    World w; w.soc = 80; BatteryEstimate e; uint32_t t = 0;
    const Step s[] = {{20, 0, 0.25}, {20, 0, 0.06}, {15, 0, 0.25}, {30, 0, 0.06}, {10, 0, 0.25}, {25, 0, 0.06}};
    Limits l;
    run("at 80%, two hours on battery: the screen on and off, radio bursts", w, e, t, s, 6, 4, l);
  }
  {
    World w; w.soc = 50; w.cable = 0.42; BatteryEstimate e; uint32_t t = 0;
    const Step s[] = {{5, 0, 0.12}, {20, 1, 0.12}, {20, 1, 0.25}, {20, 1, 0.12}, {15, 0, 0.12}};
    Limits l;
    run("a poor cable: the supply only just clears a cell's level, and sags when the screen is on", w, e, t, s, 5, 8, l);
  }
  {
    World w; w.soc = 8; BatteryEstimate e; uint32_t t = 0;
    const Step s[] = {{30, 0, 0.12}};
    Limits l;
    run("nearly empty, on battery", w, e, t, s, 1, 9, l);
  }
  printf("\n%s (%d failures)\n", failures ? "FAILED" : "all good", failures);
  return failures ? 1 : 0;
}
