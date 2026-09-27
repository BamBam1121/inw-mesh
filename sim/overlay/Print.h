// Arduino's Print, for the simulator: everything funnels into write().
#pragma once
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

class String;

class Print {
public:
  virtual ~Print() {}
  virtual size_t write(uint8_t c) = 0;
  virtual size_t write(const uint8_t* b, size_t n) { size_t k = 0; while (n--) k += write(*b++); return k; }
  size_t write(const char* s) { return s ? write((const uint8_t*)s, strlen(s)) : 0; }
  size_t write(const char* b, size_t n) { return write((const uint8_t*)b, n); }

  size_t print(const char* s) { return write(s); }
  size_t print(char c) { return write((uint8_t)c); }
  size_t print(int v, int base = 10) { return pnum((long)v, base); }
  size_t print(unsigned v, int base = 10) { return punum((unsigned long)v, base); }
  size_t print(long v, int base = 10) { return pnum(v, base); }
  size_t print(unsigned long v, int base = 10) { return punum(v, base); }
  size_t print(double v, int d = 2) { char b[48]; snprintf(b, sizeof(b), "%.*f", d, v); return write(b); }
  size_t print(const String& s);
  size_t println() { return write("\r\n"); }
  template <typename T> size_t println(T v) { size_t n = print(v); return n + println(); }
  size_t println(double v, int d) { size_t n = print(v, d); return n + println(); }
  size_t printf(const char* fmt, ...) {
    char b[512];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    if (n < 0) return 0;
    return write((const uint8_t*)b, (size_t)(n < (int)sizeof(b) ? n : (int)sizeof(b) - 1));
  }
  virtual void flush() {}

private:
  size_t pnum(long v, int base) { if (base == 10) { char b[24]; snprintf(b, sizeof(b), "%ld", v); return write(b); } return punum((unsigned long)v, base); }
  size_t punum(unsigned long v, int base) {
    char b[40]; int i = 38; b[39] = 0;
    if (!v) return write("0");
    while (v && i >= 0) { int d = (int)(v % (unsigned long)base); b[i--] = (char)(d < 10 ? '0' + d : 'A' + d - 10); v /= (unsigned long)base; }
    return write(&b[i + 1]);
  }
};
