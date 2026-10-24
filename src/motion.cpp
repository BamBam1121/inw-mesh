#include "motion.h"
#include <Wire.h>
#include <Preferences.h>
#include <math.h>
#include <SensorBHI260AP.hpp>
#include <bosch/BoschSensorID.hpp>
// The plain BHI260AP image, with nothing on the sensor's own bus (~114 kB).
#define BOSCH_APP30_SHUTTLE_BHI260_FW
#include <BoschFirmware.h>
#include "board_pins.h"
#include "logstore.h"
#include "power.h"
#include "cpu_sleep.h"

extern LogStore logs;

namespace motion {
namespace {

SensorBHI260AP imu;
bool loaded = false, failed = false, streaming = false;
uint8_t sensorId = 0;                   // the accelerometer this image offers
uint8_t kicks = 0;                      // restarts after it went quiet
uint32_t kickedAt = 0;
// Screen-off saving pauses all motion sampling: walking must never keep the
// host awake. The loaded sensor firmware stays resident for a short alert read.
bool screenPaused = false, pauseFailed = false, darkSaving = false;
const char* pauseStatus = "off";
bool alertReading = false, alertFaceDown = false;
uint32_t alertReadMs = 0;
const char* alertStatus = "not checked";
uint8_t alertSamples = 0;
uint32_t alertStartedAt = 0, alertAcceptedAt = 0;
float alertX = 0, alertY = 0, alertZ = 0;


bool raiseOn = true, holdOn = true, quietOn = false;
uint8_t manMin = 0;
bool saverOn = false;                   // battery saver: only the man-down alarm keeps the sensor

struct Vec { float x, y, z; };
float dot(const Vec& a, const Vec& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec unit(const Vec& a) {
  const float l = sqrtf(dot(a, a));
  return l > 0.2f ? Vec{a.x / l, a.y / l, a.z / l} : Vec{0, 0, 1};
}
float degBetween(const Vec& a, const Vec& b) { return acosf(constrain(dot(a, b), -1.0f, 1.0f)) * 57.29578f; }

// Which way the screen faces along z once LilyGo's axis map is applied: +1 means
// the pager lying face up reads +1 g on z.
constexpr float SCREEN_Z = 1.0f;

// 25 samples a second. `lp` trails the accelerometer by ~0.1 s: the way gravity
// pulls, which is how the pager is being held. A sample's distance from it is how
// hard the pager is being moved, in g.
constexpr float RATE_HZ = 25;
Vec lp = {0, 0, 1};
bool primed = false;
uint32_t lastSample = 0;
float shake = 0, tilt = 0;              // the latest, for debugPrint

// The last 3 s of how it was held: "quite differently a moment ago". A slow lift
// plus a hand settling takes longer than 1.6 s, which missed on a pager.
constexpr int RING = 75;
Vec ring[RING];
int ringN = 0, ringAt = 0;
int steadyN = 0;                        // samples in a row that barely moved

bool raised = false;
// Held up to read: `jitter` is the shake smoothed over ~0.4 s - a hand's tremor
// (0.02-0.07 g) or a table's nothing (under 0.005). heldAt is the last sample that
// looked held; holdFrom when this spell began, or the last key or raise.
float jitter = 0;
uint32_t heldAt = 0, holdFrom = 0;
bool faceTiming = false;                // lying face down, timing since faceSince
uint32_t faceSince = 0;
bool face = false;
uint32_t movedAt = 0;                   // the last movement or input
Vec anchor = {0, 0, 1};                 // how it was held when it last moved
Vec base = {0, 0, 1};                   // how it has been held lately (~2 s), for lean()
float leanX = 0, leanY = 0;
// A shake is back and forth: swings of 0.9 g+ that keep reversing, 4 of them with
// under 0.45 s between each. A single jolt - picked up, set down, a bump - rings the
// filtered reading for a few samples and rebounds once, but never reverses like that
// (the first version, "3 samples over 0.45 g", counted those: too sensitive).
// A hand holding it stays under 0.1 g, walking under ~0.5.
uint32_t swingAt = 0, shakeAt = 0;
uint8_t swingN = 0;
Vec swingDir = {0, 0, 0};
float swingPeak = 0, shakePeak = 0;
bool shaken = false;

void sample(const Vec& a) {
  const uint32_t now = millis();
  if (alertReading) {
    const float norm = dot(a, a);
    const float dx = a.x - alertX, dy = a.y - alertY, dz = a.z - alertZ;
    const bool valid = norm >= 0.85f * 0.85f && norm <= 1.15f * 1.15f;
    alertFaceDown = valid && unit(a).z * SCREEN_Z <= -0.8660254f;
    // Startup samples may be zero, unsettled or belong to an old orientation.
    // Require a NEW consecutive stable run; one bad sample must not poison all
    // later valid samples in the same read. Never reuse a cached quiet result.
    if (now - alertStartedAt < 120 || !valid) alertSamples = 0;
    else if (!alertSamples || dx * dx + dy * dy + dz * dz > 0.08f * 0.08f) {
      alertSamples = 1; alertAcceptedAt = now;
    } else if (now - alertAcceptedAt >= 30 && alertSamples < 3) {
      ++alertSamples; alertAcceptedAt = now;
    }
    alertX = a.x; alertY = a.y; alertZ = a.z;
  }
  if (now - lastSample > 3000) movedAt = now;    // after a gap, count stillness afresh
  lastSample = now;
  kicks = 0;
  if (!primed) { primed = true; lp = a; anchor = base = unit(a); movedAt = now; ringN = ringAt = 0; }
  lp = {lp.x + 0.3f * (a.x - lp.x), lp.y + 0.3f * (a.y - lp.y), lp.z + 0.3f * (a.z - lp.z)};
  const Vec d = {a.x - lp.x, a.y - lp.y, a.z - lp.z};
  shake = sqrtf(dot(d, d));
  const Vec att = unit(lp);

  // Lean: the tip away from the last couple of seconds' average, so a scene moves
  // while the pager moves and settles back when it is held still, at any angle.
  base = {base.x + 0.02f * (att.x - base.x), base.y + 0.02f * (att.y - base.y), base.z + 0.02f * (att.z - base.z)};
  leanX = constrain((att.x - base.x) * 3.0f, -1.0f, 1.0f);
  leanY = constrain((att.y - base.y) * 3.0f, -1.0f, 1.0f);
  tilt = acosf(constrain(att.z * SCREEN_Z, -1.0f, 1.0f)) * 57.29578f;   // 0 face up, 90 on edge, 180 face down
  // Held still in a hand: a hand shakes 0.02-0.07 g here, a table under 0.005.
  steadyN = shake < 0.08f ? steadyN + 1 : 0;
  jitter += 0.1f * (shake - jitter);
  // Held to read, the top edge is up: y reads negative (measured on a pager).
  const bool upright = att.y < 0 && fabsf(att.x) < 0.6f;

  // Held up to be read: at the angle a raise leaves it (a little looser either way),
  // the right way up, and in a hand. Propped on a stand at the same angle it is
  // still, and times out as usual.
  if (tilt >= 10 && tilt <= 85 && upright && jitter > 0.010f) {
    if (!heldAt || now - heldAt > 1500) holdFrom = now;          // a new spell of holding
    heldAt = now;
  }

  if (shake > 0.9f) {
    const Vec dir = unit(d);
    if (!swingN || now - swingAt > 450) {            // a first swing, or the last was too long ago
      swingN = 1; swingDir = dir; swingPeak = shake;
    } else if (dot(dir, swingDir) < -0.3f) {         // it came back the other way: one more swing
      swingN++; swingDir = dir;
      if (shake > swingPeak) swingPeak = shake;
      if (swingN >= 4) {
        shaken = true; shakeAt = now; shakePeak = swingPeak; swingN = 0;
        Serial.printf("[motion] shaken (peak %.1f g)\n", shakePeak);
      }
    } else if (shake > swingPeak) {                  // the same swing, still going
      swingPeak = shake;
    }
    swingAt = now;
  }

  // Man-down: a real jolt, or turned 12 degrees since it last moved. Breathing and
  // a table's hum stay under both.
  if (shake > 0.10f || degBetween(att, anchor) > 12) { movedAt = now; anchor = att; }

  // Face down: flat, screen to the table, still for 2 s. A pocket holds the pager
  // on its edge, so it never counts there. (A flag beside the time, not "now | 1":
  // on an even millisecond that is one ahead of now, and the unsigned difference
  // read as 2 s already passed.)
  if (tilt > 150 && shake < 0.05f) { if (!faceTiming) { faceTiming = true; faceSince = now; } }
  else if (tilt < 135 || shake > 0.15f) faceTiming = false;
  const bool wasFace = face;
  face = faceTiming && now - faceSince > 2000;
  if (face != wasFace) Serial.println(face ? "[motion] face down" : "[motion] face up");

  // Raise to wake: held steady in view (the screen tipped 15-80 degrees back from
  // flat, the right way up and not on its side) for 0.16 s, after being held quite
  // differently (35+ degrees) within the last 3 s: out of a pocket, up off a table,
  // turned over. Walking holds it the same way throughout, putting it down ends
  // flat, and a pocket mostly holds it upside down or on its side.
  ring[ringAt] = att;
  ringAt = (ringAt + 1) % RING;
  if (ringN < RING) ringN++;
  if (steadyN >= 4 && tilt >= 15 && tilt <= 80 && upright) {
    for (int i = 0; i < ringN; i++) {
      if (degBetween(ring[i], att) >= 35) {
        raised = true;
        holdFrom = now;                                           // holding it from here on is new
        ringN = ringAt = 0;
        Serial.printf("[motion] lifted into view (tilt %.0f)\n", tilt);
        break;
      }
    }
  }
}

void onAccel(uint8_t, const uint8_t* data, uint32_t, uint64_t*, void*) {
  bhy2_data_xyz v;
  bhy2_parse_xyz(data, &v);
  const float s = 1.0f / 4096.0f;       // 4096 counts per g at the default +-8 g
  sample({v.x * s, v.y * s, v.z * s});
}

bool load() {
  const uint32_t t0 = millis();
  uint8_t addr = 0;
  for (uint8_t a : {(uint8_t)ADDR_BHI260AP_IMU, (uint8_t)(ADDR_BHI260AP_IMU + 1)}) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { addr = a; break; }
  }
  if (!addr) { failed = true; logs.add(LOG_WARN, "motion sensor not found"); return false; }
  // The upload is ~114 kB. LilyGo runs the bus at 1 MHz for it, then puts it back.
  const uint32_t clk = Wire.getClock();
  Wire.setClock(1000000UL);
  imu.setPins(-1);
  imu.setFirmware(bosch_firmware_image, bosch_firmware_size, bosch_firmware_type);
  imu.setBootFromFlash(false);
  const bool ok = imu.begin(Wire, addr);
  Wire.setClock(clk);
  if (!ok) {
    failed = true;
    logs.add(LOG_ERROR, "motion sensor: firmware didn't load (%s)", imu.getError());
    return false;
  }
  imu.setRemapAxes(SensorRemap::BOTTOM_LAYER_TOP_LEFT_CORNER);   // how LilyGo mounts it
  loaded = true;
  logs.add(LOG_INFO, "motion sensor up in %lums", (unsigned long)(millis() - t0));
  return true;
}

void stream(bool on) {
  if (on == streaming || !loaded) return;
  if (on) {
    // The corrected accelerometer, or the raw one if this image lacks it.
    for (uint8_t id : {(uint8_t)BoschSensorID::ACCEL_CORRECTED, (uint8_t)BoschSensorID::ACCEL_PASSTHROUGH}) {
      if (!imu.onResultEvent(id, onAccel)) continue;
      if (imu.configure(id, RATE_HZ, 0)) {
        sensorId = id;
        streaming = true;
        failed = false; // a successful restart recovers a transient prior failure
        break;
      }
      imu.removeResultEvent(id, onAccel);
    }
    if (!streaming) {
      failed = true;
      logs.add(LOG_ERROR, "motion sensor: no accelerometer (%s)", imu.getError());
      return;
    }
    lastSample = millis();
  } else {
    imu.configure(sensorId, 0, 0);
    imu.removeResultEvent(sensorId, onAccel);
    streaming = false;
  }
  primed = raised = face = false;
  faceTiming = false;
  heldAt = 0;
  jitter = 0;
}

// No event detectors or GPIO8 wake source are armed. Confirm raw ODR is zero
// before allowing long sleep; a failed stop remains visible and blocks sleep.
bool pauseSampling() {
  if (!loaded || !streaming) return true;
  bhy2_virt_sensor_conf cfg{};
  if (!imu.configure(sensorId, 0, 0) ||
      bhy2_get_virt_sensor_cfg(sensorId, &cfg, imu.getDev()) != BHY2_OK || cfg.sample_rate != 0) {
    imu.configure(sensorId, RATE_HZ, 0);
    pauseFailed = true;
    pauseStatus = "sample stop failed; polling";
    return false;
  }
  imu.update(); // drain the final samples; GPIO8 is never a host wake source
  imu.removeResultEvent(sensorId, onAccel);
  streaming = false;
  screenPaused = true;
  raised = shaken = false;
  heldAt = 0;
  pauseStatus = "paused while screen off";
  return true;
}

void apply() {
  screenPaused = false;
  pauseFailed = false;
  pauseStatus = "normal polling";
  // Preserve the existing man-down alarm's sampling and timing.
  const bool want = saverOn ? manMin != 0 : (raiseOn || holdOn || quietOn || manMin);
  if (want && !loaded && !failed) load();
  stream(want);
  if (!want) pauseStatus = "off";
}

void save() {
  Preferences p;
  if (!p.begin("inw-motion", false)) return;
  p.putBool("raise", raiseOn);
  p.putBool("hold", holdOn);
  p.putBool("quiet", quietOn);
  p.putUChar("mandown", manMin);
  p.end();
}

}  // namespace

void begin() {
  Preferences p;
  if (p.begin("inw-motion", true)) {
    raiseOn = p.getBool("raise", true);
    holdOn = p.getBool("hold", true);
    quietOn = p.getBool("quiet", false);
    manMin = p.getUChar("mandown", 0);
    p.end();
  }
  apply();
}

void tick(bool screenOff) {
  if (power::saver() != saverOn) {
    saverOn = power::saver();
    apply();
    logs.add(LOG_INFO, "motion: battery saver %s, sensor %s", saverOn ? "on" : "off", streaming ? "running" : "stopped");
  }
  darkSaving = power::lightSleep() && power::cpuSaving() && screenOff;
  static bool savingWas = false;
  if (darkSaving != savingWas) { pauseFailed = false; savingWas = darkSaving; }
  if (manMin) pauseStatus = "man-down uses polling";
  if (darkSaving && !manMin) {
    raised = shaken = false;
    if (screenPaused) return;
    if (!pauseFailed && pauseSampling()) return;
  } else if (screenPaused) {
    screenPaused = false;
    stream(true);
    pauseStatus = failed ? "accelerometer resume failed" : "normal polling";
  }
  if (!streaming) return;
  static uint32_t polled = 0;
  if (millis() - polled < 40) return;
  polled = millis();
  imu.update();
  // Nothing for 5 s: set the accelerometer going again, a few times, and say so.
  // The time is read after update(), whose samples move lastSample past any
  // earlier reading (and an unsigned difference would wrap).
  const uint32_t now = millis();
  if (now - lastSample > 5000 && now - kickedAt > 5000 && kicks < 3) {
    kickedAt = now;
    kicks++;
    logs.add(LOG_WARN, "motion sensor went quiet, restarting it (%u)", kicks);
    imu.configure(sensorId, RATE_HZ, 0);
  }
}

bool irqWatch() { return false; }
const char* sleepState() { return pauseStatus; }
uint32_t eventCount() { return 0; }

void refreshForAlert(bool screenOff) {
  if (!quietOn || saverOn || manMin) return;
  const bool savingOff = screenOff && power::lightSleep() && power::cpuSaving();
  if (!screenPaused && !savingOff) return;
  // A message can arrive just after the middle button darkens the screen,
  // before tick() has paused the sensor. Pause/drain this path too.
  if (streaming && !pauseSampling()) {
    face = faceTiming = false;
    alertStatus = "sample stop failed"; alertReadMs = 0;
    return;
  }
  imu.update(); // discard stopped-stream FIFO data with its callback removed
  // Never use a cached pose: it may have changed while the host was asleep.
  face = faceTiming = false;
  screenPaused = false;
  stream(true);
  if (!streaming) {
    screenPaused = true;
    pauseStatus = "alert read failed; retry next message";
    alertStatus = "sensor resume failed";
    alertReadMs = 0;
    return;
  }
  alertSamples = 0;
  alertFaceDown = false;
  alertReading = true;
  const uint32_t start = millis();
  alertStartedAt = start; alertAcceptedAt = start;
  // Ignore the first 120 ms and never accept a burst as three fresh readings.
  // A cold sensor restart can exceed 180 ms. Finish as soon as three consecutive
  // stable readings arrive; allow at most 500 ms including settling.
  // This runs only for an alert, at 80 MHz while dark, never on a motion IRQ.
  while (alertSamples < 3 && millis() - start < 500) { imu.update(); delay(5); }
  alertReadMs = millis() - start;
  alertReading = false;
  const bool quiet = alertSamples >= 3 && alertFaceDown;
  alertStatus = quiet ? "fresh face down" : alertSamples >= 3 ? "fresh other orientation" : "no stable reading";
  if (!pauseSampling()) pauseStatus = "sample stop failed; polling";
  face = faceTiming = quiet;
  faceSince = millis() - 2001;
  raised = shaken = false; // an alert read must never become raise-to-wake
}

bool needsFastPolling() { return streaming || (pauseFailed && loaded); }

bool running() { return !failed && (screenPaused || (streaming && millis() - lastSample < 3000)); }

const char* state() {
  if (failed) return "not responding";
  if (screenPaused) return pauseStatus;
  if (running()) return saverOn ? "on for man-down (saver)" : "on";
  if (saverOn && (raiseOn || holdOn || quietOn)) return "paused: battery saver";
  return streaming ? "starting" : "off";
}

bool paused() { return saverOn; }

bool raiseToWake() { return raiseOn; }
bool stayOnHeld() { return holdOn; }
bool quietFaceDown() { return quietOn; }
uint8_t manDownMin() { return manMin; }

// Turning one on after a failure tries the sensor again.
void setRaiseToWake(bool on) { raiseOn = on; if (on) failed = false; save(); apply(); }
void setStayOnHeld(bool on) { holdOn = on; if (on) failed = false; save(); apply(); }
void setQuietFaceDown(bool on) { quietOn = on; if (on) failed = false; save(); apply(); }
void setManDownMin(uint8_t m) { manMin = m; movedAt = millis(); if (m) failed = false; save(); apply(); }

// In battery saver these rest even when the man-down alarm keeps the sensor going.
bool takeRaise() {
  const bool r = raised;
  raised = false;
  return r && raiseOn && !saverOn && !darkSaving &&
    !(power::cpuSaving() && power::lightSleep()) && running();
}

uint32_t heldFor() {
  const uint32_t now = millis();
  if (!holdOn || saverOn || !running() || !heldAt || now - heldAt > 1500) return 0;
  const uint32_t f = now - holdFrom;
  return f ? f : 1;
}

bool takeShake(float& peak) {
  const bool s = shaken && millis() - shakeAt < 1000;   // an old one (screen was off) is dropped
  shaken = false;
  peak = s ? shakePeak : 0;
  return s && !saverOn && running();
}

bool faceDown() { return quietOn && !saverOn && running() && face; }

void noteActivity() {
  movedAt = millis();
  holdFrom = movedAt;                   // a key restarts how long holding keeps the screen on
  if (primed) anchor = unit(lp);
}

uint32_t stillFor() { return running() && primed ? millis() - movedAt : 0; }

bool lean(float& x, float& y) {
  if (saverOn || !running() || !primed) { x = y = 0; return false; }
  x = leanX;
  y = leanY;
  return true;
}

void debugPrint() {
  Serial.printf("[motion] sleep=%s motion_wake=off alert=%s read_ms=%lu\r\n",
                sleepState(), alertStatus, (unsigned long)alertReadMs);
  Serial.printf("[motion] %s  g %.3f %.3f %.3f  tilt %.0f  shake %.3f  jitter %.3f  held %lus  still %lus  face %d  raise %d hold %d quiet %d mandown %u\r\n",
                state(), lp.x, lp.y, lp.z, tilt, shake, jitter, (unsigned long)(heldFor() / 1000),
                (unsigned long)(stillFor() / 1000), face, raiseOn, holdOn, quietOn, manMin);
}

}  // namespace motion
