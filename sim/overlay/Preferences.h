// Preferences for the simulator: in memory, gone when it exits.
#pragma once
#include <map>
#include <string>
#include <vector>
#include "WString.h"
class Preferences {
public:
  bool begin(const char* ns, bool = false) { _ns = ns; return true; }
  void end() {}
  bool clear() { store()[_ns].clear(); return true; }
  bool remove(const char* k) { store()[_ns].erase(k); return true; }
  bool isKey(const char* k) { return store()[_ns].count(k) > 0; }
  size_t putBytes(const char* k, const void* v, size_t n) { auto& b = store()[_ns][k]; b.assign((const uint8_t*)v, (const uint8_t*)v + n); return n; }
  size_t getBytes(const char* k, void* v, size_t n) { auto it = store()[_ns].find(k); if (it == store()[_ns].end()) return 0; size_t m = it->second.size() < n ? it->second.size() : n; memcpy(v, it->second.data(), m); return m; }
  size_t getBytesLength(const char* k) { auto it = store()[_ns].find(k); return it == store()[_ns].end() ? 0 : it->second.size(); }
  template <typename T> size_t put(const char* k, T v) { return putBytes(k, &v, sizeof(v)); }
  template <typename T> T get(const char* k, T d) { T v; return getBytes(k, &v, sizeof(v)) == sizeof(v) ? v : d; }
  size_t putUChar(const char* k, uint8_t v) { return put(k, v); }
  uint8_t getUChar(const char* k, uint8_t d = 0) { return get(k, d); }
  size_t putUShort(const char* k, uint16_t v) { return put(k, v); }
  uint16_t getUShort(const char* k, uint16_t d = 0) { return get(k, d); }
  size_t putUInt(const char* k, uint32_t v) { return put(k, v); }
  uint32_t getUInt(const char* k, uint32_t d = 0) { return get(k, d); }
  size_t putInt(const char* k, int32_t v) { return put(k, v); }
  int32_t getInt(const char* k, int32_t d = 0) { return get(k, d); }
  size_t putULong(const char* k, uint32_t v) { return put(k, v); }
  uint32_t getULong(const char* k, uint32_t d = 0) { return get(k, d); }
  size_t putFloat(const char* k, float v) { return put(k, v); }
  float getFloat(const char* k, float d = 0) { return get(k, d); }
  size_t putBool(const char* k, bool v) { return put(k, v); }
  bool getBool(const char* k, bool d = false) { return get(k, d); }
  size_t putString(const char* k, const char* v) { return putBytes(k, v, strlen(v) + 1); }
  size_t putString(const char* k, const String& v) { return putString(k, v.c_str()); }
  String getString(const char* k, const String& d = String()) { auto it = store()[_ns].find(k); return it == store()[_ns].end() ? d : String((const char*)it->second.data()); }
  size_t freeEntries() { return 400; }
private:
  static std::map<std::string, std::map<std::string, std::vector<uint8_t>>>& store() { static std::map<std::string, std::map<std::string, std::vector<uint8_t>>> s; return s; }
  std::string _ns;
};
