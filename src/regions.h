// Region scopes: a message floods only through the repeaters that carry a region
// (MeshCore's "flood scope"), instead of across the whole mesh. Made to work as
// the MeshCore app does it:
//   - a list of regions you've added (typed in, or picked from what the repeaters
//     in range report - the app's "Discover Regions");
//   - a default region for everything the node floods (the app's Default Region
//     Scope: MeshCore's own prefs.default_scope_*, so the app and this show and
//     set the same one);
//   - a region for one channel, overriding the default (the app's Set Region
//     Scope), or cleared, back to the default.
// Names are shown as the app shows them: plain letters, digits and '-', no '#'.
// A region's key is the first 16 bytes of SHA-256 of "#name" (TransportKeyStore::
// getAutoKeyFor), exact spelling and capitals, the same on every device and
// repeater. Channel choices are kept in /chregion.bin, the list in /regions.bin.
#pragma once
#include <Arduino.h>

namespace regions {

constexpr size_t NAME_LEN = 30;          // MeshCore's default_scope_name[31]
constexpr int LIST_MAX = 16;

// "spo" or "#spo" -> "spo". Letters, digits and '-'; false (with why in err) for
// anything else, which a repeater couldn't match.
bool clean(const char* in, char* out, size_t cap, const char** err = nullptr);
void keyFor(const char* name, uint8_t key[16]);

// The regions you've added, in the order added.
int  list(char names[][NAME_LEN + 1], int max);
void add(const char* name);              // no-op if already there
void remove(const char* name);

// The device default, "" for none (the whole mesh).
const char* defaultName();
void setDefault(const char* name);       // "" clears it

// A channel's own region, "" when it has none (it follows the default). Only the
// key's first 6 bytes are read, so a ConvKey's id will do.
const char* forChannel(const uint8_t* secret16);
void setForChannel(const uint8_t* secret16, const char* name);   // "" clears it
// The region a channel's messages actually go out in ("" = the whole mesh).
const char* effective(const uint8_t* secret16);

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
