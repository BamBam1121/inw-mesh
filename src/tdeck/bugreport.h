// Problem reports for the T-Deck beta: when it crashes (or hits a real error),
// what happened goes back to the developer by itself, over Wi-Fi.
//
// What a report holds: the firmware version, why it restarted, where it crashed
// (the task, the program counter and backtrace from ESP-IDF's core dump, which the
// release's firmware.elf turns into file and line), memory, uptime, battery, and
// the last log lines before it happened. What it never holds: messages, contact
// or channel names, keys, positions, Wi-Fi names or addresses - log lines that
// could carry one are cut down before they're kept.
//
// Reports wait in the flash store (/rpt, at most 4) until Wi-Fi is up and the
// screen has been left alone a few seconds, then go to squatchmesh.com. On by
// default for the beta; Settings > System turns it off, and Tools > log can send
// the log on purpose ("send log to the developer").
#pragma once
#include <Arduino.h>

namespace report {
  void capture();              // setup(), BEFORE the first log line: keep the last run's log
  void begin();                // setup(), once the flash store is mounted: file a crash report
  void tick();                 // loop(): sends what's waiting
  bool enabled();
  void setEnabled(bool on);
  bool sendLog(const char* why);  // queue the log now (Tools > log); false if the store is full
  uint8_t waiting();
}
