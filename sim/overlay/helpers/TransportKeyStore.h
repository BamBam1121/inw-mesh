// Simulator stand-in for MeshCore's TransportKeyStore: region keys aren't
// needed to draw screens, so every name gets the same all-zero key.
#pragma once
#include <stdint.h>
#include <string.h>

struct TransportKey {
  uint8_t key[16];
  bool isNull() const { for (int i = 0; i < 16; i++) if (key[i]) return false; return true; }
};

class TransportKeyStore {
public:
  void getAutoKeyFor(uint16_t, const char*, TransportKey& dest) { memset(dest.key, 0, sizeof(dest.key)); }
};
