// The pager's motion sensor, a Bosch BHI260AP: an accelerometer and gyroscope with
// no magnetometer, so no compass. It runs nothing until its firmware has been loaded
// over I2C (about a second), so it is only started while something uses it:
//   raise to wake        lifting the pager into view lights the screen (the lock face)
//   stay on while held   the screen doesn't time out while it's held up to be read
//   quiet when face down lying screen-down and still, messages make no sound
//   man-down alarm       no movement for a set time: chirps, then the SOS countdown
// The settings live in their own NVS namespace, as the SOS channel does, so the
// settings blob keeps the layout the T-Deck build shares.

#pragma once
#include <Arduino.h>

namespace motion {
  void begin();                // setup(): loads the sensor's firmware if anything is on
  void tick(bool screenOff);   // loop(): stream while awake; pause while screen-off saving
  bool irqWatch();             // always false: no motion wake source
  const char* sleepState();
  uint32_t eventCount();
  void refreshForAlert(bool screenOff = true);       // fresh orientation before quiet-face-down decision
  bool needsFastPolling();     // sampling, stop failure or man-down
  bool running();              // samples are coming in
  const char* state();         // "on", "off" or "not responding", for the menus and the log
  // Battery saver pauses raise to wake, quiet when face down and the lean, and stops
  // the sensor - unless the man-down alarm is on, which keeps it for itself.
  bool paused();

  bool raiseToWake();          void setRaiseToWake(bool on);
  bool stayOnHeld();           void setStayOnHeld(bool on);
  bool quietFaceDown();        void setQuietFaceDown(bool on);
  uint8_t manDownMin();        void setManDownMin(uint8_t minutes);   // 0 = off

  bool takeRaise();            // lifted into view since the last call (raise to wake on)
  // Held up to be read right now - turned to the face as a raise leaves it, and in a
  // hand, which is never perfectly still where a table or a stand is - for how many
  // ms since this spell of holding began or the last key restarted it. 0 when it
  // isn't (or stay on while held is off). The main loop keeps the screen on with it.
  uint32_t heldFor();
  // Shaken in the last second - really shaken: 4 swings of 0.9 g+ back and forth,
  // under 0.45 s apart, so picking it up or setting it down never counts - with the
  // hardest swing in g. For the lock screen's sasquatch.
  bool takeShake(float& peak);
  bool faceDown();             // quiet when face down is on and it lies screen-down, still
  void noteActivity();         // a key, the wheel or the side button: someone is there
  uint32_t stillFor();         // ms without movement or input; 0 while it isn't running
  // How far the pager has just been tipped, for scenes that lean with it: x along the
  // screen, y up it, about -1..1 (1 = some 20 degrees), easing back to 0 over a couple
  // of seconds once it is held still at any angle. false (and 0, 0) while not running.
  bool lean(float& x, float& y);

  void debugPrint();           // USB "motion" (dev builds): what it sees right now
}
