// File systems for the simulator: nothing is stored. Opening fails, so the
// firmware starts every run as a fresh pager and the simulator fills in its data.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "WString.h"
#include "HardwareSerial.h"

#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"

namespace fs {
enum SeekMode { SeekSet = 0, SeekCur = 1, SeekEnd = 2 };
class File : public Stream {
public:
  size_t write(uint8_t) override { return 0; }
  size_t write(const uint8_t*, size_t) override { return 0; }
  int available() override { return 0; }
  int read() override { return -1; }
  size_t read(uint8_t*, size_t) { return 0; }
  bool seek(uint32_t, SeekMode = SeekSet) { return false; }
  size_t position() const { return 0; }
  size_t size() const { return 0; }
  void close() {}
  operator bool() const { return false; }
  const char* name() const { return ""; }
  const char* path() const { return ""; }
  bool isDirectory() const { return false; }
  File openNextFile() { return File(); }
  void rewindDirectory() {}
  time_t getLastWrite() { return 0; }
};
class FS {
public:
  bool begin(bool = false, const char* = "/", uint8_t = 10, const char* = nullptr) { return true; }
  File open(const char*, const char* = FILE_READ, bool = false) { return File(); }
  File open(const String& p, const char* m = FILE_READ, bool c = false) { return open(p.c_str(), m, c); }
  bool exists(const char*) { return false; }
  bool exists(const String&) { return false; }
  bool remove(const char*) { return true; }
  bool remove(const String&) { return true; }
  bool rename(const char*, const char*) { return true; }
  bool mkdir(const char*) { return true; }
  bool rmdir(const char*) { return true; }
  size_t totalBytes() { return 3u << 20; }
  size_t usedBytes() { return 512u << 10; }
  void end() {}
};
}  // namespace fs
using fs::File;
using fs::FS;
