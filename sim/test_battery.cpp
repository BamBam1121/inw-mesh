// The T-Deck battery estimate (src/tdeck/battery_est.h) against made-up voltage
// traces: a 2000 mAh cell, a 0.5 A charger, radio bursts, ADC noise, and units whose
// divider reads high or low. Prints what the screen would show and checks it never
// jumps, never says 100 early, never misses a plug, and calibrates itself.
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

struct Cell {
  double soc = 30;          // %
  double capMah = 2000, r = 0.28;
  double loadA = 0.12;      // the T-Deck, screen on
};

static int failures = 0, shown = 0;      // the first few of a scenario's failures are printed
static void check(bool ok, const char* what, double t) {
  if (ok) return;
  failures++;
  if (shown++ < 3) printf("  FAIL at %.1f min: %s\n", t / 60000.0, what);
}

// power 0 = none, 1 = wall charger, 2 = computer.
struct Phase { double minutes; int power; };

// adc: what the pin reads per true volt (1.05 = this unit reads 5% high).
static void run(const char* name, Cell& c, BatteryEstimate& e, uint32_t& t, const Phase* ph, int nph,
                unsigned seed, double adc, bool quiet = false) {
  printf("\n== %s\n", name);
  srand(seed);
  shown = 0;
  int lastPct = e.percent, lastExt = e.external, flips = 0;
  bool first = t == 0;
  double worst = 0;
  for (int p = 0; p < nph; p++) {
    const uint32_t end = t + (uint32_t)(ph[p].minutes * 60000);
    const int power = ph[p].power;
    bool terminated = false;
    for (; t < end; t += BatteryEstimate::PERIOD_MS) {
      // Charger: 0.5 A until the terminal reaches 4.2 V, then hold 4.2 V while the
      // current falls; stop below 50 mA. The load comes out of whatever it gives.
      double term;
      if (power && !terminated) {
        const double v = ocv(c.soc);                                 // mV
        double chargeI = 0.5;                                        // A; the lift is I * R in mV
        if (v + chargeI * c.r * 1000 > 4200) chargeI = (4200 - v) / (c.r * 1000);
        if (chargeI < 0.05) { terminated = true; chargeI = 0; }
        term = terminated ? 4200 : v + chargeI * c.r * 1000;         // done: it still holds 4.2 V
        c.soc += chargeI * 1000 * (BatteryEstimate::PERIOD_MS / 3600000.0) / c.capMah * 100;
      } else if (power) {
        term = 4200;
      } else {
        term = ocv(c.soc) - c.loadA * c.r * 1000;
        c.soc -= c.loadA * 1000 * (BatteryEstimate::PERIOD_MS / 3600000.0) / c.capMah * 100;
      }
      if (c.soc > 100) c.soc = 100;
      double pin = term * adc + (rand() % 25 - 12);                 // the unit's error, and ADC noise
      if ((t / 2000) % 29 == 7) pin -= power ? 50 : 120;           // a transmit burst
      e.update(t, (uint16_t)pin, power == 2);
      if (first) { lastPct = e.percent; lastExt = e.external; first = false; }
#ifdef BATT_DEBUG
      if (seed == 21 && power && t % 900000 == 0)
        printf("    dbg %6.1f pin %4.0f mv %4u flatRef %4u lastRise %d holding %d full %d lift %u pct %u\n", t / 60000.0, pin,
               e.mv, e.flatRef, e.lastRise, e.holding, e.full, e.lift, e.percent);
#endif
      const double tm = t;
      check(abs((int)e.percent - lastPct) <= 1, "percent jumped", tm);
      // Before its first full charge a unit that reads high can't know; after, it must.
      if (fabs(e.cal - 1 / adc) < 0.006) check(!(e.percent >= 100 && c.soc < 95), "says 100% but the cell is under 95%", tm);
      if (lastExt != (int)e.external) {
        flips++;
        if (!quiet)
          printf("  %6.1f min  %s (true %3.0f%%, shown %3u%%)\n", tm / 60000.0, e.external ? "ON POWER" : "on battery",
                 c.soc, e.percent);
      }
      if (!power && t % 60000 == 0 && fabs(e.percent - c.soc) > worst) worst = fabs(e.percent - c.soc);
      if (!quiet && t % 1800000 == 0)
        printf("  %6.1f min  reads %4.0f  true %3.0f%%  shown %3u%%  %s%s  cal %.4f\n", tm / 60000.0, pin, c.soc,
               e.percent, e.external ? "charging" : "battery", e.full ? " FULL" : "", e.cal);
      lastPct = e.percent;
      lastExt = e.external;
    }
    check(e.external == (power != 0), power ? "charger not seen" : "unplug not seen", t);
  }
  check(flips <= 2 * nph, "charger state flickered", t);
  printf("  end: true %.0f%%, shown %u%%, cal %.4f (ideal %.4f), worst on battery %.0f points, %d changes\n", c.soc,
         e.percent, e.cal, 1.0 / adc, worst, flips);
}

int main() {
  {
    Cell c; c.soc = 30; BatteryEstimate e; uint32_t t = 0;
    const Phase p[] = {{10, 0}, {300, 1}, {20, 0}};
    run("30% on battery, wall charger to full, unplugged", c, e, t, p, 3, 1, 1.0);
  }
  {
    Cell c; c.soc = 30; BatteryEstimate e; uint32_t t = 0;
    const Phase p[] = {{5, 0}, {60, 1}, {30, 0}};
    run("30%, charged an hour, unplugged mid-charge", c, e, t, p, 3, 2, 1.0);
  }
  {
    Cell c; c.soc = 50; BatteryEstimate e; uint32_t t = 0;
    const Phase p[] = {{5, 0}, {90, 2}, {10, 0}};
    run("50%, plugged into a computer", c, e, t, p, 3, 3, 1.0);
  }
  {
    Cell c; c.soc = 45; BatteryEstimate e; uint32_t t = 0;
    const Phase p[] = {{60, 1}, {10, 0}};
    run("came up already on a wall charger at 45%", c, e, t, p, 2, 5, 1.0);
  }
  {
    Cell c; c.soc = 80; BatteryEstimate e; uint32_t t = 0;
    const Phase p[] = {{120, 0}};
    run("80%, two hours on battery with radio bursts", c, e, t, p, 1, 4, 1.0);
  }
  // A unit that reads 5% high: it used to look plugged in and at 100% for good.
  {
    Cell c; c.soc = 60; BatteryEstimate e; uint32_t t = 0;
    for (int cycle = 1; cycle <= 4; cycle++) {
      const Phase p[] = {{240, 0}, {330, 1}};
      char name[64]; snprintf(name, sizeof(name), "reads 5%% high, day %d: use, then charge", cycle);
      run(name, c, e, t, p, 2, 10 + cycle, 1.05, true);
      if (cycle == 2) check(fabs(e.cal - 1 / 1.05) < 0.01, "not calibrated after two charges (5% high)", t);
    }
  }
  // And one that reads 4% low.
  {
    Cell c; c.soc = 60; BatteryEstimate e; uint32_t t = 0;
    for (int cycle = 1; cycle <= 4; cycle++) {
      const Phase p[] = {{240, 0}, {330, 1}};
      char name[64]; snprintf(name, sizeof(name), "reads 4%% low, day %d: use, then charge", cycle);
      run(name, c, e, t, p, 2, 20 + cycle, 0.96, cycle != 4);
      if (cycle == 2) check(fabs(e.cal - 1 / 0.96) < 0.01, "not calibrated after two charges (4% low)", t);
    }
  }
  printf("\n%s (%d failures)\n", failures ? "FAILED" : "all good", failures);
  return failures ? 1 : 0;
}
