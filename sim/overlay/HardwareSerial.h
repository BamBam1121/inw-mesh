// Serial for the simulator: printed to the console, reads nothing.
#pragma once
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include "Print.h"

class Stream : public Print {
public:
  virtual int available() { return 0; }
  virtual int read() { return -1; }
  virtual int peek() { return -1; }
  size_t readBytes(uint8_t*, size_t) { return 0; }
  size_t readBytes(char*, size_t) { return 0; }
  void setTimeout(unsigned long) {}
};

class HardwareSerial : public Stream {
public:
  size_t write(uint8_t c) override { return fputc(c, stdout) == EOF ? 0 : 1; }
  size_t write(const uint8_t* b, size_t n) override { return fwrite(b, 1, n, stdout); }
  using Stream::read;
  size_t read(uint8_t*, size_t) { return 0; }
  void begin(unsigned long, uint32_t = 0, int8_t = -1, int8_t = -1) {}
  void end() {}
  operator bool() const { return true; }
  void setRxBufferSize(size_t) {}
  void flush() override { fflush(stdout); }
};

#define SERIAL_8N1 0x800001c
extern HardwareSerial Serial;
extern HardwareSerial Serial1;
