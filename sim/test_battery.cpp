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

// ---- the T-Deck's own draw ------------------------------------------------------------------
// The screen lit (with Wi-Fi) draws ~0.25 A, dark ~0.06 A, and a cell goes on
// recovering for minutes after the draw drops. One stretch of use:
struct Use { double minutes; double loadA; int power; };

// tell: the firmware reports its draw changing (loadChanged), as main.cpp does.
// firstOff: how far off the very first reading is (mV), as one taken mid-start.
// Returns how many times it decided a charger had gone in.
static int runUse(const char* name, Cell& c, BatteryEstimate& e, uint32_t& t, const Use* u, int n, unsigned seed,
                  bool tell, double firstOff, bool expectNoPlug, bool quiet = false) {
  printf("\n== %s\n", name);
  srand(seed);
  shown = 0;
  const double rp = 0.12, tau = 150;       // the slow part of the cell's resistance, and how slowly it lets go
  double pol = 0;                          // mV it is still holding back
  double lastLoad = -1;
  int plugs = 0, lastExt = 0, lastPct = 0;
  bool first = true;
  double worst = 0;
  const uint32_t t0 = t;
  for (int k = 0; k < n; k++) {
    const uint32_t end = t + (uint32_t)(u[k].minutes * 60000);
    // What main.cpp reports: its own rough figure, here 20% off the truth.
    if (tell && fabs(u[k].loadA - lastLoad) > 0.02) e.drawChanged(t, (int)((u[k].loadA - 0.06) * 1000 * 0.8));
    lastLoad = u[k].loadA;
    const uint32_t from = t;
    for (; t < end; t += BatteryEstimate::PERIOD_MS) {
      const double dt = BatteryEstimate::PERIOD_MS / 1000.0;
      double chargeI = u[k].power == 1 ? 0.5 : 0;
      double net = chargeI - u[k].loadA;                               // A into the cell
      double term = ocv(c.soc) + net * c.r * 1000 - pol;
      if (term > 4200 && chargeI > 0) {                                // held at 4.2 V: the current falls
        net = (4200 + pol - ocv(c.soc)) / (c.r * 1000);
        if (net < 0.05 - u[k].loadA) net = -u[k].loadA * 0;            // done: it just holds the cell there
        term = 4200;
      }
      pol += (-net * rp * 1000 - pol) * dt / tau;
      c.soc += net * 1000 * (dt / 3600.0) / c.capMah * 100;
      if (c.soc > 100) c.soc = 100;
      if (c.soc < 0) c.soc = 0;
      double pin = term + (rand() % 25 - 12);
      if ((t / 2000) % 29 == 7) pin -= 60;                             // a transmit burst
      if (t == t0 && first) pin += firstOff;
      e.update(t, (uint16_t)pin, u[k].power == 2);
      if (first) { lastPct = e.percent; lastExt = e.external; first = false; }
      const double tm = t;
      // The very start may correct itself once (a bad first reading); after that it never jumps.
      if (t - t0 > 60000) check(abs((int)e.percent - lastPct) <= 1, "percent jumped", tm);
      if (lastExt != (int)e.external) {
        if (e.external) plugs++;
        if (!quiet)
          printf("  %6.1f min  %s (true %3.0f%%, shown %3u%%, draw %.2f A)\n", tm / 60000.0,
                 e.external ? "ON POWER" : "on battery", c.soc, e.percent, u[k].loadA);
      }
      if (expectNoPlug) check(!e.external, "says it's charging with nothing plugged in", tm);
      // On a charger: seen within 20 s of the stretch starting, and never lost.
      if (!expectNoPlug && u[k].power && t - from > 20000 && k > 0 && u[k - 1].power)
        check(e.external, "lost the charger when its own draw changed", tm);
      if (!u[k].power && t - t0 > 120000 && fabs(e.percent - c.soc) > worst) worst = fabs(e.percent - c.soc);
      lastPct = e.percent;
      lastExt = e.external;
    }
  }
  printf("  end: true %.0f%%, shown %u%%, %s, decided \"charger in\" %d time%s, worst on battery %.0f points\n", c.soc, e.percent,
         e.external ? "on power" : "on battery", plugs, plugs == 1 ? "" : "s", worst);
  return plugs;
}

static void drawTests() {
  // A T-Deck in use: lit for a bit, dark for a while, again and again. No charger.
  static const Use DAY[] = {{3, 0.25, 0}, {20, 0.06, 0}, {2, 0.25, 0}, {15, 0.06, 0}, {5, 0.25, 0}, {30, 0.06, 0},
                            {1, 0.25, 0}, {10, 0.06, 0}, {4, 0.25, 0}, {25, 0.06, 0}};
  {
    Cell c; c.soc = 45; c.r = 0.20; BatteryEstimate e; uint32_t t = 0;
    const int plugs = runUse("in use at 45%, a low first reading, nothing plugged in (as reported from a T-Deck)", c, e, t,
                             DAY, 10, 31, true, -230, true);
    check(plugs == 0, "a charger was seen that wasn't there", t);
    check(abs((int)e.percent - (int)c.soc) <= 14, "the percentage is far off after a session of use", t);
  }
  {
    Cell c; c.soc = 70; c.r = 0.28; BatteryEstimate e; uint32_t t = 0;
    runUse("a tired cell (0.28 ohm) at 70%, same use, nothing plugged in", c, e, t, DAY, 10, 32, true, 0, true);
  }
  {
    Cell c; c.soc = 60; c.capMah = 9000; c.r = 0.06; BatteryEstimate e; uint32_t t = 0;
    runUse("three 18650s in parallel at 60%, same use, nothing plugged in", c, e, t, DAY, 10, 33, true, 0, true);
  }
  // What the old code did with the same day, for the record: no reports of its draw.
  {
    Cell c; c.soc = 45; c.r = 0.20; BatteryEstimate e; uint32_t t = 0;
    const int plugs = runUse("the same day WITHOUT being told its draw changed (how beta1 behaved)", c, e, t, DAY, 10, 31,
                             false, 0, false, true);
    printf("  (%d false \"charger in\" - each one a chime; with the reports there are none)\n", plugs);
  }
  // Charging on a wall charger, and woken to look at now and then: it stays "charging".
  {
    Cell c; c.soc = 30; c.r = 0.20; BatteryEstimate e; uint32_t t = 0;
    static const Use U[] = {{5, 0.06, 0}, {10, 0.06, 1}, {1, 0.25, 1}, {10, 0.06, 1}, {2, 0.25, 1}, {20, 0.06, 1}};
    const int plugs = runUse("dark, plugged into a wall charger, woken twice while charging", c, e, t, U, 6, 34, true, 0, false);
    check(plugs == 1 && e.external, "the charger was lost or seen twice", t);
  }
  // Picked up off the charger: unplugged and woken in the same moment.
  {
    Cell c; c.soc = 50; c.r = 0.20; BatteryEstimate e; uint32_t t = 0;
    static const Use U[] = {{2, 0.06, 0}, {30, 0.06, 1}, {3, 0.25, 0}, {10, 0.06, 0}};
    runUse("charged half an hour, then unplugged and woken at once", c, e, t, U, 4, 35, true, 0, false);
    check(!e.external, "still says charging after being unplugged", t);
  }
  // Plugged in while lit, the usual way.
  {
    Cell c; c.soc = 40; c.r = 0.20; BatteryEstimate e; uint32_t t = 0;
    static const Use U[] = {{4, 0.25, 0}, {1, 0.25, 1}, {30, 0.06, 1}};
    const int plugs = runUse("lit, plugged in, then goes dark while charging", c, e, t, U, 3, 36, true, 0, false);
    check(plugs == 1 && e.external, "the charger was lost or seen twice", t);
  }
}

int main() {
  drawTests();
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
