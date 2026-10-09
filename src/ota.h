// Updates over Wi-Fi from the project's release site.
//
// Two channels: stable (ota.json / firmware.bin, tagged releases only) and beta
// (ota-beta.json / firmware-beta.bin, every build of main), picked by the
// "beta updates" setting.
//
// ota.json on the site names the latest version with the firmware's size,
// SHA-256 and an Ed25519 signature of that hash, made by the release build with
// a key only the project holds. The pager checks the signature against the key
// built into it, downloads into the spare app slot, checks the hash, and only
// then switches over. A failed or interrupted download leaves the running
// firmware as it was.

#pragma once
#include <Arduino.h>

namespace ota {
  struct Info {
    bool ok = false;           // the check itself worked
    bool newer = false;
    bool beta = false;         // which channel this came from
    char version[16] = "";
    char notes[120] = "";
    uint32_t size = 0;
    char error[48] = "";
  };

  bool supported();            // false on the older one-slot partition table
  bool replacesOther();        // the slot an update goes into holds someone else's firmware (a multi-boot setup)
  bool checking();             // the automatic check is out on the network right now (it runs in a task)
  Info check();                // blocking, a second or two; needs Wi-Fi (the channel Settings picks)
  Info check(bool beta);       // one channel: beta or official releases
  const char* install(const Info& info);   // blocking with a progress screen; reboots on success
  void tick();                 // from loop: the automatic check (and install, below)

  // Updates install by themselves: checked on Wi-Fi when it connects and every 6
  // hours, installed once it's idle - screen off and untouched 2 minutes, charging
  // or 30%+, no SOS or phone sync going. On by default; Settings > System turns it
  // off. The pager takes only official releases this way (a beta still asks); the
  // T-Deck, whose builds are all betas for now, takes each one.
  bool autoInstall();
  void setAutoInstall(bool on);
  void announce();             // setup(): after a restart into a new version, say so
}
