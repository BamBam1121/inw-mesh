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

// Asking the repeaters in direct range which regions they flood: a discover to
// find them, then MeshCore's regions request to each in turn - one question in
// the air at a time, as two repeaters answering at once collide. A repeater that
// isn't a contact yet is added first (only a contact's answer can be read), as
// the MeshCore app does. Settings shows it (RegionScanView); USB prints it.
class Scan {
public:
  static constexpr int MAX_NAMES = 16;
  bool start();                          // false: the radio is busy
  bool tick();                           // call often; true when something changed
  bool running() const { return _started && !_done; }
  bool done() const { return _done; }
  bool failed() const { return _failed; }
  bool asking() const { return _asking; }
  // Regions heard, most-served first after sort().
  int  count() const { return _n; }
  const char* name(int i) const { return _names[_order[i]]; }
  int  servedBy(int i) const { return _counts[_order[i]]; }
  int  answered = 0, wholeMesh = 0, silent = 0, added = 0, full = 0;

private:
  void record(const char* list);
  void sort();
  char _names[MAX_NAMES][NAME_LEN + 1];
  int  _counts[MAX_NAMES] = {0}, _order[MAX_NAMES] = {0}, _n = 0;
  uint8_t _next = 0;
  bool _asking = false, _done = false, _failed = false, _started = false;
  uint32_t _gen = 0, _at = 0, _askedAt = 0;
};

}  // namespace regions
