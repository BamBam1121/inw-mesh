// Updates over Wi-Fi from the project's release site.
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
    char version[16] = "";
    char notes[120] = "";
    uint32_t size = 0;
    char error[48] = "";
  };

  bool supported();            // false on the older one-slot partition table
  Info check();                // blocking, a second or two; needs Wi-Fi
  const char* install(const Info& info);   // blocking with a progress screen; reboots on success
  void tick();                 // from loop: the once-per-boot automatic check
}
