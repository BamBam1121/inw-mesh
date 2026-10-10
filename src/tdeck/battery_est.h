// The T-Deck's battery percentage, and whether it is on USB power, from one pin: it
// has no fuel gauge, and its charger chip tells the processor nothing. Plain C++ with
// no pins in it, so sim/test_battery.cpp can drive it with made-up traces on a PC.
//
// What the pin shows (LilyGo's schematic, and a T-Deck with a cell measured 2026-10-09):
// not the cell, but the board's supply AFTER the part that chooses between USB and the
// cell.
//   - On battery that is the cell, less a few tens of mV for the switch it comes through.
//   - With USB in, the cell is cut off from the supply and the pin shows USB less a
//     diode: 4.4 to 4.7 V, moving 0.1 V with the T-Deck's own draw. The cell cannot be
//     seen at all, charging or full.
// Until then this file took the pin for the cell with a charger pushing it up 0.1-0.2 V.
// So plugging in read as a full cell (straight to 100%), the supply's steadiness read as
// "held at full", and a "calibration against 4.20 V" was learned from the USB supply:
// it scaled every reading down by up to 10%, and on battery the figure then came out far
// too low. That correction is gone, and what earlier firmware stored for it is thrown
// away (battery.h).
//
// So:
//   On battery   the percentage is read off the cell's voltage, with the T-Deck's own
//                draw put back, and moves a percent at a time.
//   On USB       anything over 4.32 V cannot be a cell: it is the USB supply. (A computer
//                on the port says so outright.) The cell is out of sight, so the figure is
//                reckoned: it starts from what the cell last showed and climbs at the
//                rate this cell was last seen to fill, slower near the top. 100 means the
//                reckoning got there, not that anything was measured. The same sum run
//                forward is the time left until full (minutesToFull).
//   Unplugged    the cell is back in sight: the figure glides to the truth (a percent a
//                reading, never a jump), and what the cell gained over the time it was
//                plugged in becomes the rate for next time. That is how a 10,000 mAh cell
//                and a 2,000 mAh one each come to be reckoned right; the first charge of a
//                cell much bigger than the usual one reads fast until it is unplugged.
//   Started on USB with nothing seen yet: it begins from the last figure it stored
//                (battery.h), or the middle if there is none, and says so (known = false).
//
// The T-Deck's own draw moves the cell's voltage by tens of mV (the screen and Wi-Fi going
// off and on), so the firmware says when its draw changes (drawChanged): the voltage is
// followed to its new level, and the size of the step teaches how far its own draw pulls
// this cell down, which is put back before the percentage is read. The first reading
// isn't taken at its word: it comes in the middle of starting up, and a low one used to
// show 19% on a cell at 45%.
//
// Times are kept with a flag beside them, never "now | 1": on an even millisecond that is
// one ahead of now, and the unsigned difference read as 49 days had passed.
#pragma once
#include <stdint.h>

struct BatteryEstimate {
  static constexpr uint32_t PERIOD_MS = 2000;       // one reading per this
  static constexpr uint16_t USB_MV = 4320;          // the pin at or over this: the USB supply, not a cell
  static constexpr uint16_t USB_OFF_MV = 4270;      // under this again (three readings): the cable is out
  static constexpr uint16_t USB_DROP_MV = 80;       // or, on a weak supply, this far under its lowest
  static constexpr float RATE_DEFAULT = 25.0f;      // percent an hour: the usual 2,000 mAh cell at the charger's 0.5 A
  static constexpr float RATE_MIN = 1.0f, RATE_MAX = 60.0f;
  static constexpr float TAPER = 2.67f;             // near the top: percent an hour for each percent still to go (rateAt)
  static constexpr uint32_t LEARN_MIN_MS = 20UL * 60 * 1000;  // a charge shorter than this teaches nothing
  static constexpr uint32_t LEARN_WAIT_MS = 3UL * 60 * 1000;  // after unplugging the cell reads high for a while
  static constexpr uint32_t CATCH_MS = 10UL * 60 * 1000;      // unplugged: the figure may move fast for this long
  static constexpr uint16_t LOAD_MV = 20;           // what the T-Deck's least draw takes off the cell's voltage
  static constexpr uint32_t DOWN_MS = 20000, SETTLE_MS = 5UL * 60 * 1000;
  static constexpr uint32_t QUIET_MS = 16000;       // the step a change in its own draw makes is over in this
  static constexpr uint32_t START_MS = 60000;       // the first readings come while it's still starting up
  static constexpr int START_MV = 25;               // a first reading this far under the next ones was a bad one

  uint16_t mv = 0;            // smoothed pin: the cell on battery, the USB supply when plugged in
  uint16_t cellMv = 0;        // the cell as last seen (stands still while on USB)
  uint8_t percent = 0;        // what's shown
  bool external = false;      // on USB power
  bool full = false;          // on USB power, and reckoned full
  bool known = true;          // false: started on USB, the figure is from memory or a guess
  float rate = RATE_DEFAULT;  // percent an hour this cell gains on USB; persist it (rateChanged says when)
  bool rateChanged = false;
  float drain = 0;            // percent an hour it loses on battery, as its owner uses it; 0: not seen yet. Persist it (drainChanged)
  bool drainChanged = false;

  // Before the first reading: the figure stored when it last ran (battery.h).
  void remember(int pct) { remembered = pct < 0 || pct > 100 ? -1 : pct; }
  // By hand (a USB test command): this is the figure now. On USB the reckoning, and what
  // this charge will teach, start again from here.
  void setPercent(uint32_t now, uint8_t pct) {
    percent = pct > 100 ? 100 : pct;
    pctF = percent;
    known = true;
    full = external && percent >= 100;
    if (external) { sessFrom = now; sessP0 = percent; }
  }

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
    if (mA < draw && !external) { settling = true; settleFrom = now; }   // the figure may come back up a little
    draw = mA;
  }

  void update(uint32_t now, uint16_t pin, bool host) {
    if (pin < 2000) return;                           // nothing sensible on the pin
    const uint16_t raw = pin;
    // The middle of the last three readings: a radio burst is one reading low, so it
    // never reaches the smoothed voltage. Real steps get through a reading later.
    const uint16_t med = median(prev2 ? prev2 : raw, prev1 ? prev1 : raw, raw);
    prev2 = prev1;
    prev1 = raw;
    if (!have) {
      have = true;
      hostWas = host;
      lastAt = stepAt = now;
      mv = raw;
      if (host || raw >= USB_MV) {
        // Started on USB: the cell hasn't been seen. From memory, or the middle.
        known = false;
        plug(now, remembered >= 0 ? remembered : 50, false);
      } else {
        cellMv = raw;
        percent = curve(rested());
        settling = starting = true;                   // it may have just been under load, and starting up is one
        settleFrom = startFrom = batFrom = now;
      }
      return;
    }
    const uint32_t dt = now - lastAt;
    lastAt = now;
    if (quiet && now - quietFrom >= QUIET_MS) {
      quiet = false;
      // The voltage has taken its step: that step over the change in draw is this
      // cell's resistance, wiring and all. Only steps the right way round, of a size a
      // cell could make, and only on battery (on USB the pin isn't the cell).
      if (learning && !external && learnBy) {
        const float r = (float)((int)learnFrom - (int)mv) / learnBy;      // mV per mA
        if (r > 0.01f && r < 0.8f) ohm += (r - ohm) * (ohmKnown ? 0.3f : 1.0f), ohmKnown = true;
      }
      learning = false;
    }
    if (starting && now - startFrom >= START_MS) starting = false;

    if (!external) {
      over = raw >= USB_MV ? over + 1 : 0;
      if (over >= 2 || (host && !hostWas)) {
        // USB went in. What this stretch on battery showed of how fast it runs down is
        // kept for the next one: half way from what was known, so one odd day can't throw it.
        const float seen = runRate(now);
        if (seen > 0) { drain = drain > 0 ? drain + (seen - drain) * 0.5f : seen; drainChanged = true; }
        runOn = false;
        // What the cell showed until now is where the reckoning starts, and
        // it only counts as known if that figure had had time to settle.
        plug(now, percent, known && !starting && !catching);
        mv = med;
      } else {
        if (starting && (int)med >= (int)mv + START_MV) {
          // The first reading was low (taken mid-start, or a bad conversion): begin again
          // from this one. Nothing has been on the screen long enough to jump.
          mv = med;
          percent = curve(rested());
          stepAt = now;
        } else if (quiet) {
          mv = med;                                   // its own draw just changed: follow it to the new level
        } else if (raw < USB_MV) {
          mv = (uint16_t)((mv * 7u + med) / 8u);      // (a first reading at USB level isn't smoothed into the cell)
        }
        cellMv = mv;
        const uint8_t target = curve(rested());
        if (settling && now - settleFrom >= SETTLE_MS) settling = false;
        if (catching && now - catchFrom >= CATCH_MS) catching = false;
        // Just unplugged: the reckoned figure may be far from what the cell now shows.
        // Go there a percent a reading; otherwise a percent every twenty seconds.
        const int gap = (int)target - (int)percent;
        const uint32_t pace = catching && (gap > 3 || gap < -3) ? PERIOD_MS : DOWN_MS;
        if (gap < 0 && now - stepAt >= pace) {
          percent--;
          stepAt = now;
          // How fast it runs down is timed from a moment the figure has just come down a
          // point: then it is level with what the cell shows. (The first minutes on battery
          // are not: until a change in its own draw has shown how far that pulls this cell
          // down, the figure can sit a point or two low, and a count begun there would come
          // out too slow.) With no such change, half an hour in is soon enough.
          if (!runOn && !catching && !starting && (ohmKnown || now - batFrom >= 30UL * 60 * 1000)) {
            runOn = true; runPct = percent; runAt = now;
          } else if (runOn && (int)runPct - (int)percent >= 10) {
            // Ten points timed: that is this unit's rate. Kept (half way from what was
            // known, so one odd afternoon can't throw it), and the count starts again.
            const float seen = runRate(now);
            if (seen > 0) { drain = drain > 0 ? drain + (seen - drain) * 0.5f : seen; drainChanged = true; }
            runPct = percent; runAt = now;
          }
        } else if ((settling || catching) && gap > 2 && now - stepAt >= pace) {
          percent++;
          stepAt = now;
          if (runOn && percent > runPct) { runPct = percent; runAt = now; }   // above where the count began: begin again here
        }
        if (!catching && !starting) known = true;
        // What the charge just ended really added, once the cell has stopped reading high.
        if (owed && now - owedFrom >= LEARN_WAIT_MS && !quiet) { owed = false; learn(target); }
      }
    } else {
      // The supply's level: followed at once when it is far off (just plugged in, the
      // figure is still the cell's), smoothed otherwise.
      const int far = (int)med - (int)mv;
      mv = quiet || far > 100 || far < -100 ? med : (uint16_t)((mv * 7u + med) / 8u);
      if (mv >= USB_OFF_MV && mv < extMin) extMin = mv;    // (not the readings on the way up from the cell's level)
      // The cable is out when the pin is back where a cell can be: under 4.27 V, or on a
      // supply so weak it never got far above that, well under the lowest it has shown.
      const uint16_t off = extMin >= USB_OFF_MV + USB_DROP_MV ? USB_OFF_MV : (uint16_t)(extMin - USB_DROP_MV);
      under = (!host && raw < off) ? under + 1 : 0;
      if (under >= 3) {
        external = full = false;
        reckoned = pctF;
        sessMs = now - sessFrom;
        owed = sessP0 >= 0 && sessMs >= LEARN_MIN_MS;
        owedFrom = now;
        mv = cellMv = med;                            // not raw: that can be a burst's dip
        over = under = 0;
        settling = catching = true;
        settleFrom = catchFrom = stepAt = batFrom = now;
      } else {
        // Out of sight, the cell fills at the rate learned for it, less near the top.
        pctF += rateAt(pctF) * (float)dt / 3600000.0f;
        if (pctF > 100.0f) pctF = 100.0f;
        if ((uint8_t)pctF > percent) percent++;       // a point at a time, however long since the last reading
        full = percent >= 100;
      }
    }
    hostWas = host;
  }

  // Percent an hour the cell gains at `p` percent. Up to a point it is the rate learned
  // for it: the charger gives all it has. Then the charger is holding the cell at 4.2 V
  // and what flows is what the cell will still take, which falls with what is left to
  // fill (to a little past 100, as it never quite stops). A small cell, charged fast for
  // its size, gets there around 93%; a big one, charged slowly, hardly at all. (TAPER
  // comes from a cell's resistance going down as its size goes up, and 8 mV a percent
  // near the top: about 2.7 percent an hour for each percent still to go.)
  float rateAt(float p) const {
    const float top = TAPER * (102.5f - p);
    return rate < top ? rate : top;
  }
  // Minutes until it is reckoned full, at that; 0 when it is not charging. As good as
  // the rate and the figure it started from: within the hour once a charge has taught
  // the rate, a guess before.
  uint16_t minutesToFull() const {
    if (!external || full) return 0;
    float mins = 0;
    for (float p = pctF; p < 100.0f; p += 0.25f) mins += 0.25f / rateAt(p) * 60.0f;
    return mins > 60000.0f ? 60000 : mins < 1.0f ? 1 : (uint16_t)(mins + 0.5f);
  }

  // Minutes it will last on battery at the rate it has been running down; 0 on USB.
  // The rate is this stretch's own once it has lost three points over half an hour
  // (mixed evenly with what earlier stretches showed), else what earlier stretches
  // showed. With neither, it is a guess from the cell's size: the charger's half amp
  // fills this cell at `rate`, and a T-Deck in ordinary use draws about a fifth of
  // that. leftKnown() says which.
  uint16_t minutesLeft(uint32_t now) const {
    if (external || !have) return 0;
    const float live = runRate(now);
    float r = live > 0 && drain > 0 ? (live + drain) * 0.5f : live > 0 ? live : drain > 0 ? drain : 0.2f * rate;
    if (r < 0.05f) r = 0.05f;
    const float mins = (percent > 1 ? percent - 1 : 0) / r * 60.0f;     // it turns itself off at about 1%
    return mins > 60000.0f ? 60000 : (uint16_t)(mins + 0.5f);
  }
  bool leftKnown(uint32_t now) const { return drain > 0 || runRate(now) > 0; }

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
  // The cell's voltage with the T-Deck's own draw put back: what it would read dark.
  uint16_t rested() const { return (uint16_t)(mv + LOAD_MV + (uint16_t)(ohm * draw + 0.5f)); }
  static uint16_t median(uint16_t a, uint16_t b, uint16_t c) {
    if (a > b) { const uint16_t x = a; a = b; b = x; }
    return c <= a ? a : c >= b ? b : c;
  }

  // Percent an hour lost over this stretch on battery; 0 until there is enough to go on.
  float runRate(uint32_t now) const {
    if (!runOn || external) return 0;
    const int lost = (int)runPct - (int)percent;
    const uint32_t ms = now - runAt;
    if (lost < 3 || ms < 30UL * 60 * 1000) return 0;
    return (float)lost / ((float)ms / 3600000.0f);
  }

  void plug(uint32_t now, int from, bool sure) {
    external = true;
    full = false;
    pctF = (float)from;
    percent = (uint8_t)from;
    extMin = 5000;                                    // the supply's level shows over the next readings
    sessFrom = now;
    sessP0 = sure ? from : -1;
    over = under = 0;
    settling = catching = owed = false;
  }

  // A charge has ended and the cell is in sight again at `truth` percent. It began at
  // sessP0 and lasted sessMs: that is this cell's rate, if both ends are below where the
  // charger slows down. Ended full sooner than reckoned: the rate is at least that.
  void learn(int truth) {
    const float hours = (float)sessMs / 3600000.0f;
    const float gain = (float)(truth - sessP0);
    float seen = (gain > 0.5f ? gain : 0.5f) / hours;
    const bool both = sessP0 <= 80 && truth <= 88 && (gain >= 2.0f || hours >= 1.0f);
    const bool least = !both && truth > 88 && reckoned < (float)truth - 5.0f && gain >= 2.0f && seen > rate;
    if (!both && !least) return;
    if (seen < RATE_MIN) seen = RATE_MIN; else if (seen > RATE_MAX) seen = RATE_MAX;
    // Far out (another cell altogether): take it as seen. Close: half way, so one odd
    // reading can't throw it.
    const bool far = seen > 2.0f * rate || seen < 0.5f * rate;
    rate += (seen - rate) * (far ? 1.0f : 0.5f);
    rateChanged = true;
  }

  bool have = false, hostWas = false;
  uint16_t prev1 = 0, prev2 = 0;    // the last two readings, for the median
  uint8_t over = 0, under = 0;      // readings in a row at USB level, and back below it
  int remembered = -1;              // the figure stored when it last ran
  float pctF = 0;                   // on USB: the reckoned figure, with its fraction
  uint16_t extMin = 5000;           // on USB: the lowest the supply has shown
  uint32_t lastAt = 0;
  uint32_t stepAt = 0;              // the shown percent last moved
  uint32_t sessFrom = 0, sessMs = 0;   // the charge under way, or just ended
  int sessP0 = -1;                  // what the cell showed as it began; -1: it wasn't seen
  uint32_t batFrom = 0;             // when this stretch on battery began
  bool runOn = false;               // on battery: a stretch is being timed, from this figure at this time
  uint8_t runPct = 0;
  uint32_t runAt = 0;
  float reckoned = 0;               // the figure as the cable came out
  bool owed = false;                // a charge has ended and hasn't been learned from yet
  uint32_t owedFrom = 0;
  bool settling = false;            // just unplugged, started or let off a load: may still correct upward
  uint32_t settleFrom = 0;
  bool catching = false;            // just unplugged: closing on what the cell shows
  uint32_t catchFrom = 0;
  bool quiet = false, starting = false;      // see drawChanged() and the first readings
  uint32_t quietFrom = 0, startFrom = 0;
  int draw = 0;                     // its own draw above the least, mA (drawChanged)
  float ohm = 0.15f;                // mV its voltage falls per mA of that, learned from the steps
  bool ohmKnown = false, learning = false;
  uint16_t learnFrom = 0;           // the voltage before the last change in draw, and the change
  int learnBy = 0;
};
