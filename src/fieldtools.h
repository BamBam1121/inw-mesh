// Field tools: range test, SOS beacon and breadcrumb trail. Each runs in the
// background from the main loop, so they keep going with the screen off, in a
// pocket or on a dash. Tools -> field opens their controls.
#pragma once
#include <Arduino.h>

namespace field {
  void tick();                 // call from loop()
  bool wantsGps();             // any of them running: keep the GPS on regardless of settings

  // Range test: a zero-hop discover every few seconds; every repeater that answers
  // is logged with both-way signal and our GPS position to /inw/range on the SD card.
  bool rangeOn();

  // SOS: a message with our position to one channel now and every 5 minutes until
  // stopped. Five quick presses of the side button start it after a countdown.
  bool sosOn();
  void sosArm();               // the countdown screen: sends unless cancelled
  void sosNoteButton();        // main.cpp: every side-button press

  // Breadcrumb trail: where we've been, drawn on the map, with the way back.
  struct TrailPt { float lat, lon; };
  bool trailOn();
  const TrailPt* trail(size_t& n);         // points recorded (kept after stopping)

  void openMenu();
}
