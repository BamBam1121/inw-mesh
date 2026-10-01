// The T-Deck's battery percentage and charger state, from the cell voltage alone:
// it has no charger chip or fuel gauge to ask. Plain C++ with no pins in it, so
// sim/test_battery.cpp can drive it with made-up voltage traces on a PC.
//
// Plugged in: a charger lifts the cell's voltage at once, by its current times the
// cell's resistance (0.1-0.2 V), and unplugging drops it as fast. A radio burst only
// ever sags it, and briefly. So a step up that holds for two readings is a plug.
// While charging the voltage never falls, so a step down that holds, or a slow
// fall below the highest it reached, is an unplug. A computer on the USB port says
// so outright. Came up already on a charger: the voltage keeps climbing for minutes,
// which a cell on its own never does.
//
// The percentage shown never jumps (it used to go from 30 to 100 on plugging in).
// On battery it follows the discharge curve down a percent at a time, and up only
// while the cell settles after unplugging or starting. Charging, it reads the curve
// at the voltage less the lift the charger added, and climbs at most a percent every
// 30 s. When the voltage stops climbing the charger is holding it at full while its
// current tapers off (~40 minutes); the lift is counted down to nothing over that
// time, and then it says 100.
//
// Calibration: at the end of that taper the charger holds the cell at 4.20 V, so
// what the pin reads then shows how far this unit's divider and ADC are out. Each
// full charge moves the correction 80% of the way there (at most 4% a charge,
// within +-10% overall). Nothing depends on absolute voltages being right until it
// has: a unit that reads 5% high used to look plugged in and 100% for good.
//
// The T-Deck's own draw moves the voltage too, and by as much as a small charger
// does: when the screen goes dark (and Wi-Fi and the GPS with it) the cell comes up
// by tens of mV at once and goes on recovering for minutes; waking it sags the same
// way. Read as a charger and an unplugging, that gave a charging icon, a plug-in
// chime and a climbing percentage with nothing plugged in - round and round, since
// Wi-Fi stays on "while it charges". So the firmware says when its draw changes
// (drawChanged): the step that follows isn't a plug or an unplug, and after the draw
// drops the cell may climb for a while without it meaning anything. Those steps also
// show how far its own draw pulls this cell down (a tired cell far more than three
// 18650s side by side), so the percentage is read off the voltage with that put back:
// it used to sink while the screen was lit and never come back. The first
// reading isn't taken at its word either: it comes in the middle of starting up, and
// a low one used to show 19% on a cell at 45% and then pass for a charger when the
// next readings came in higher.
//
// Times are kept with a flag beside them, never "now | 1": on an even millisecond
// that is one ahead of now, and the unsigned difference read as 49 days had passed.
#pragma once
#include <stdint.h>

struct BatteryEstimate {
  static constexpr uint32_t PERIOD_MS = 2000;       // one reading per this
  static constexpr int STEP_MV = 60;                // a plug or an unplug moves it this much at once
  static constexpr int FALL_MV = 40;                // charging never lets it fall this far
  static constexpr int RISE_MV = 40;                // on battery it never climbs this far...
  static constexpr uint32_t RISE_MS = 5UL * 60 * 1000;   // ...and keeps climbing this long
  static constexpr uint16_t HOST_LIFT_MV = 150;     // assumed when the plug-in wasn't seen
  static constexpr int KNEE_MV = 8;                 // on power: climbing at least this in one window,
  static constexpr int FLAT_MV = 5;                 // then no more than this in the next,
  static constexpr uint32_t FLAT_MS = 15UL * 60 * 1000;  // windows this long: held at full
  static constexpr uint32_t TAPER_MS = 40UL * 60 * 1000; // the charger's current dies away
  static constexpr uint16_t CHARGED_MV = 4200;      // what it holds the cell at when done
  static constexpr uint16_t LOAD_MV = 20;           // what the T-Deck's own draw (~0.12 A) takes off
  static constexpr uint32_t UP_MS = 30000, DOWN_MS = 20000, SETTLE_MS = 5UL * 60 * 1000;
  static constexpr uint32_t QUIET_MS = 16000;       // the step a change in its own draw makes is over in this
  static constexpr uint32_t EASE_MS = 10UL * 60 * 1000;   // after the draw drops the cell may go on climbing this long
  static constexpr uint32_t START_MS = 60000;       // the first readings come while it's still starting up
  static constexpr int START_MV = 25;               // a first reading this far under the next ones was a bad one

  uint16_t mv = 0;            // smoothed, calibrated cell voltage
  uint8_t percent = 0;        // what's shown
  bool external = false;      // on a charger or a computer
  bool full = false;          // charged, and still plugged in
  float cal = 1.0f;           // reading -> volts; persist it (calChanged says when)
  bool calChanged = false;

  // The T-Deck's own draw just changed: the screen went dark or lit, Wi-Fi or the GPS
  // went off or on. mA: roughly what it now draws above its least (dark, the radio
  // alone). Only the changes matter, so the figures needn't be right.
  void drawChanged(uint32_t now, int mA) {
    if (mA < 0) mA = 0;
    if (!have || mA == draw) { draw = mA; return; }
    learnFrom = mv;
    learnBy = mA - draw;
    learning = !external;
    quiet = true;
    quietFrom = now;
    if (mA < draw) {
      easing = true;
      easeFrom = now;
      if (!external) { settling = true; settleFrom = now; }   // the figure may come back up a little
    }
    draw = mA;
  }

  void update(uint32_t now, uint16_t pin, bool host) {
    if (pin < 2000) return;                           // nothing sensible on the pin
    const uint16_t raw = (uint16_t)(pin * cal + 0.5f);
    // The middle of the last three readings: a radio burst is one reading low, so it
    // never reaches the smoothed voltage, where it once passed for a charger that
    // had stopped climbing. Real steps get through a reading later.
    const uint16_t med = median(prev2 ? prev2 : raw, prev1 ? prev1 : raw, raw);
    prev2 = prev1;
    prev1 = raw;
    if (!have) {
      have = true;
      hostWas = host;
      mv = peak = low = raw;
      lowAt = now;
      external = host;
      lift = external ? HOST_LIFT_MV : 0;
      percent = external ? cap(curve(sub(rested(), lift))) : curve(rested());
      stepAt = now;
      settling = true;                                // it may have just been under load
      settleFrom = now;
      starting = easing = true;                       // and starting up is a load that then goes
      startFrom = easeFrom = now;
      return;
    }
    if (quiet && now - quietFrom >= QUIET_MS) {
      quiet = false;
      // The voltage has taken its step: that step over the change in draw is this
      // cell's resistance, wiring and all. Only steps the right way round, of a size a
      // cell could make, and not while a charger muddies it.
      if (learning && !external && learnBy) {
        const float r = (float)((int)learnFrom - (int)mv) / learnBy;      // mV per mA
        if (r > 0.01f && r < 0.8f) ohm += (r - ohm) * (ohmKnown ? 0.3f : 1.0f), ohmKnown = true;
      }
      learning = false;
    }
    if (easing && now - easeFrom >= EASE_MS) easing = false;
    if (starting && now - startFrom >= START_MS) starting = false;
    const int d = (int)raw - (int)mv;
    bool smooth = true;
    if (!external) {
      if (starting && (int)med >= (int)mv + START_MV) {
        // The first reading was low (taken mid-start, or a bad conversion): begin again
        // from this one. Nothing has been on the screen long enough to jump.
        mv = low = med;
        lowAt = now;
        percent = curve(rested());
        stepAt = now;
        ups = 0;
        smooth = false;
      } else if (quiet) {
        // Its own draw just changed: follow the voltage to its new level. Only a step
        // twice a charger's least can be a plug in the middle of that.
        if (d >= 2 * STEP_MV) { if (++ups == 1) before = mv; }
        else { ups = 0; mv = med; }
        smooth = false;
      } else if (starting) {
        ups = 0;                                      // still settling from the start: no plug yet
      }
      // A step up is held against the level before it, not smoothed into it, so a
      // 65 mV lift still reads as 65 on the second reading.
      else if (d >= STEP_MV) { if (++ups == 1) before = mv; smooth = false; } else ups = 0;
      const bool climbing = (int)mv >= (int)low + RISE_MV && now - lowAt >= RISE_MS;
      if (ups >= 2 || (host && !hostWas) || climbing) {
        external = true;
        lift = ups >= 2 ? clampLift(med - before) : HOST_LIFT_MV;
        // Found out late (it came up on a charger): what's shown was read off a lifted
        // voltage, so let it come down to the real figure.
        pullDown = ups < 2;
        if (ups >= 2) mv = med;                       // the middle reading: never a burst's dip
        peak = mv;
        ups = downs = 0;
        holding = settling = false;
        flatRef = startMv = 0;                        // the first window only sets the level
        flatFrom = now;
        lastRise = prevRise = 0;
        smooth = false;
      }
    } else {
      if (d <= -STEP_MV) { ++downs; smooth = false; }
      else {
        downs = 0;
        // Its own draw just changed (the screen woke): the sag that follows is not the
        // charger going, so the level it mustn't fall below moves down with it.
        if (quiet) { mv = med; peak = mv; smooth = false; }
      }
      const bool hostGone = hostWas && !host;
      if (downs >= 3 || hostGone || (!quiet && (int)mv < (int)peak - FALL_MV)) {
        external = full = holding = pullDown = false;
        mv = low = med;                               // not raw: that can be a burst's dip
        lowAt = now;
        ups = downs = 0;
        settling = true;
        settleFrom = now;
        calSum = 0; calN = 0; calDone = false;
        smooth = false;
      }
    }
    hostWas = host;
    if (smooth) mv = (uint16_t)((mv * 7u + med) / 8u);

    if (external) {
      if (mv > peak) peak = mv;
      // Held at full: it climbed at least KNEE_MV in one window and then no more than
      // FLAT_MV in the next. Judged by the change, not a voltage, so it works on a
      // unit that reads high or low; mid-charge the charger never stops pushing.
      if (!holding && now - flatFrom >= FLAT_MS) {
        const int rise = flatRef ? (int)mv - (int)flatRef : 0;
        // Climbed in one of the last two windows, or since the charge began: a knee
        // can straddle a window edge and show as two small climbs. (A plateau far
        // below full is a charger that can't keep up, not a full cell.)
        const bool climbed = lastRise >= KNEE_MV || prevRise >= KNEE_MV || (startMv && (int)mv >= (int)startMv + 20);
        if (flatRef && rise <= FLAT_MV && climbed && mv > 3850) { holding = true; holdFrom = flatFrom; }
        if (!flatRef) startMv = mv;                   // settled after plugging in
        prevRise = lastRise;
        lastRise = flatRef ? rise : 0;
        flatRef = mv;
        flatFrom = now;
      }
      const uint32_t held = holding ? now - holdFrom : 0;
      full = holding && held >= TAPER_MS;
      const uint16_t liftNow = full ? 0 : (uint16_t)(lift * (1.0f - (float)held / TAPER_MS));
      const uint8_t target = full ? 100 : cap(curve(sub(rested(), liftNow)));
      if (target > percent && now - stepAt >= UP_MS) { percent++; stepAt = now; }
      else if (pullDown && percent > target + 2 && now - stepAt >= DOWN_MS) { percent--; stepAt = now; }
      else if (pullDown && percent <= target + 2) pullDown = false;
      // Full and still on the charger: the pin now reads CHARGED_MV. Two minutes of
      // readings, once per charge.
      if (full && !calDone) {
        calSum += pin;
        if (++calN >= 60) {
          const float want = (float)CHARGED_MV * calN / calSum;      // what cal should be
          if (want > 0.9f && want < 1.1f) {
            // Most of the way there, and no more than 4% a charge: a charger that
            // stopped short once can't throw it far, and a real error closes in two.
            float step = (want - cal) * 0.8f;
            if (step > 0.04f) step = 0.04f; else if (step < -0.04f) step = -0.04f;
            float next = cal + step;
            if (next < 0.9f) next = 0.9f; else if (next > 1.1f) next = 1.1f;
            // Everything remembered in volts moves with it, or the new scale would read
            // as a step and pass for unplugging.
            rescale(next / cal);
            cal = next;
            calChanged = true;
          }
          calDone = true;
        }
      }
    } else {
      // While it recovers from a drop in its own draw the lowest follows it up, so
      // that climb never counts toward "came up on a charger".
      if (easing || mv < low) { low = mv; lowAt = now; }
      const uint8_t target = curve(rested());
      if (settling && now - settleFrom >= SETTLE_MS) settling = false;
      if (target < percent && now - stepAt >= DOWN_MS) { percent--; stepAt = now; }
      else if (settling && target > percent + 2 && now - stepAt >= DOWN_MS) { percent++; stepAt = now; }
    }
  }

  // Resting Li-ion cell, lightly loaded.
  static uint8_t curve(uint16_t mv) {
    static const uint16_t MV[]  = {4180, 4100, 4000, 3920, 3850, 3800, 3750, 3710, 3670, 3620, 3500, 3300};
    static const uint8_t  PCT[] = { 100,   90,   80,   70,   60,   50,   40,   30,   20,   10,    5,    0};
    if (mv >= MV[0]) return 100;
    for (int i = 1; i < 12; i++)
      if (mv >= MV[i]) return PCT[i] + (uint8_t)((uint32_t)(PCT[i - 1] - PCT[i]) * (mv - MV[i]) / (MV[i - 1] - MV[i]));
    return 0;
  }

private:
  // The voltage with the T-Deck's own draw put back: what the cell would read dark.
  uint16_t rested() const { return (uint16_t)(mv + LOAD_MV + (uint16_t)(ohm * draw + 0.5f)); }
  void rescale(float k) {
    uint16_t* v[] = {&mv, &peak, &low, &before, &flatRef, &startMv, &prev1, &prev2};
    for (uint16_t* p : v) if (*p) *p = (uint16_t)(*p * k + 0.5f);
  }
  static uint16_t median(uint16_t a, uint16_t b, uint16_t c) {
    if (a > b) { const uint16_t x = a; a = b; b = x; }
    return c <= a ? a : c >= b ? b : c;
  }
  static uint16_t sub(uint16_t a, uint16_t b) { return a > b ? a - b : 0; }
  static uint8_t cap(uint8_t p) { return p > 99 ? 99 : p; }         // 100 only once it's shown it's full
  static uint16_t clampLift(int v) { return (uint16_t)(v < 40 ? 40 : v > 250 ? 250 : v); }

  bool have = false, hostWas = false;
  uint16_t prev1 = 0, prev2 = 0;    // the last two readings, for the median
  uint8_t ups = 0, downs = 0;       // readings in a row past a step
  bool pullDown = false;            // on power, shown too high: may come down to the real figure
  int lastRise = 0, prevRise = 0;   // the last two windows' climbs
  uint16_t startMv = 0;             // on power: the level once it had settled
  uint16_t before = 0;              // smoothed voltage before a step up
  uint16_t lift = 0;                // what the charger adds
  uint16_t peak = 0;                // highest smoothed voltage while on power
  uint16_t low = 0;                 // lowest while on battery, and when
  uint32_t lowAt = 0;
  uint32_t stepAt = 0;              // the shown percent last moved
  bool settling = false;            // just unplugged or started: may still correct upward
  uint32_t settleFrom = 0;
  bool quiet = false, easing = false, starting = false;   // see drawChanged() and the first readings
  uint32_t quietFrom = 0, easeFrom = 0, startFrom = 0;
  int draw = 0;                     // its own draw above the least, mA (drawChanged)
  float ohm = 0.15f;                // mV its voltage falls per mA of that, learned from the steps
  bool ohmKnown = false, learning = false;
  uint16_t learnFrom = 0;           // the voltage before the last change in draw, and the change
  int learnBy = 0;
  uint16_t flatRef = 0;             // on power: the voltage a FLAT_MS window started at
  uint32_t flatFrom = 0;
  bool holding = false;             // on power, held at full, the current tapering off
  uint32_t holdFrom = 0;
  uint32_t calSum = 0;              // pin readings while full, this charge
  uint16_t calN = 0;
  bool calDone = false;
};
