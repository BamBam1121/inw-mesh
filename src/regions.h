// Region scopes: a message floods only through the repeaters that serve a region
// (MeshCore's "flood scope"), instead of across the whole mesh.
//
// A public region is just a name. Its key is the first 16 bytes of SHA-256 of
// "#name", the same on every device and repeater (TransportKeyStore::
// getAutoKeyFor), so "spokane" and "#spokane" are one region.
//
// The device's default region lives in MeshCore's own prefs (default_scope_name
// and _key), so the phone app shows and sets the same one; with no default,
// messages flood the whole mesh as before. A channel can have its own choice on
// top: another region, or the whole mesh even when there is a default. Those
// are ours, kept in /chregion.bin by channel key.
#pragma once
#include <Arduino.h>

namespace regions {

constexpr size_t NAME_LEN = 30;          // MeshCore's default_scope_name[31]
constexpr const char* WHOLE_MESH = "*";  // a channel's choice: no region at all

// "spokane" or "#spokane" -> "spokane". Letters, digits, '-' and '_'; false
// (with why in err) for anything a repeater couldn't match.
bool clean(const char* in, char* out, size_t cap, const char** err = nullptr);
void keyFor(const char* name, uint8_t key[16]);

// The device default, "" for none.
const char* defaultName();
void setDefault(const char* name);       // "" clears it

// A channel's own choice: "" = the device default, WHOLE_MESH, or a region name.
// Only the key's first 6 bytes are read, so a ConvKey's id will do.
const char* forChannel(const uint8_t* secret16);
void setForChannel(const uint8_t* secret16, const char* choice);
// Where a channel's messages go, for showing: "#spokane" or "whole mesh".
String describe(const uint8_t* secret16);

// Region names this device knows of (the default and every channel's), for
// picking from a list instead of typing. Returns how many.
int known(char names[][NAME_LEN + 1], int max);

void begin();                            // after the store is mounted

}  // namespace regions
