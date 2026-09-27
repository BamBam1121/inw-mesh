// Arduino's String, on std::string, for the simulator. The members the firmware uses.
#pragma once
#include <string>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

class String {
public:
  String() {}
  String(const char* s) : _s(s ? s : "") {}
  String(const std::string& s) : _s(s) {}
  String(char c) : _s(1, c) {}
  String(int v, unsigned char base = 10) { num((long)v, base); }
  String(unsigned int v, unsigned char base = 10) { unum((unsigned long)v, base); }
  String(long v, unsigned char base = 10) { num(v, base); }
  String(unsigned long v, unsigned char base = 10) { unum(v, base); }
  String(unsigned char v, unsigned char base = 10) { unum(v, base); }
  String(float v, unsigned int decimals = 2) { dbl(v, decimals); }
  String(double v, unsigned int decimals = 2) { dbl(v, decimals); }

  const char* c_str() const { return _s.c_str(); }
  unsigned int length() const { return (unsigned int)_s.size(); }
  bool isEmpty() const { return _s.empty(); }
  char charAt(unsigned int i) const { return i < _s.size() ? _s[i] : 0; }
  char operator[](unsigned int i) const { return charAt(i); }
  char& operator[](unsigned int i) { return _s[i]; }
  void setCharAt(unsigned int i, char c) { if (i < _s.size()) _s[i] = c; }
  void reserve(unsigned int n) { _s.reserve(n); }

  String& operator=(const char* s) { _s = s ? s : ""; return *this; }
  String& operator+=(const String& o) { _s += o._s; return *this; }
  String& operator+=(const char* s) { if (s) _s += s; return *this; }
  String& operator+=(char c) { _s += c; return *this; }
  String& operator+=(int v) { return *this += String(v); }
  String& operator+=(unsigned int v) { return *this += String(v); }
  String& operator+=(long v) { return *this += String(v); }
  String& operator+=(unsigned long v) { return *this += String(v); }
  bool concat(const String& o) { _s += o._s; return true; }
  bool concat(const char* s) { if (s) _s += s; return true; }
  bool concat(char c) { _s += c; return true; }

  bool operator==(const String& o) const { return _s == o._s; }
  bool operator==(const char* s) const { return _s == (s ? s : ""); }
  bool operator!=(const String& o) const { return _s != o._s; }
  bool operator!=(const char* s) const { return !(*this == s); }
  bool operator<(const String& o) const { return _s < o._s; }
  bool equals(const String& o) const { return _s == o._s; }
  bool equalsIgnoreCase(const String& o) const {
    if (_s.size() != o._s.size()) return false;
    for (size_t i = 0; i < _s.size(); i++) if (tolower((unsigned char)_s[i]) != tolower((unsigned char)o._s[i])) return false;
    return true;
  }
  int compareTo(const String& o) const { return _s.compare(o._s); }

  int indexOf(char c, unsigned int from = 0) const { size_t p = _s.find(c, from); return p == std::string::npos ? -1 : (int)p; }
  int indexOf(const String& s, unsigned int from = 0) const { size_t p = _s.find(s._s, from); return p == std::string::npos ? -1 : (int)p; }
  int indexOf(const char* s, unsigned int from = 0) const { return indexOf(String(s), from); }
  int lastIndexOf(char c) const { size_t p = _s.rfind(c); return p == std::string::npos ? -1 : (int)p; }
  int lastIndexOf(const String& s) const { size_t p = _s.rfind(s._s); return p == std::string::npos ? -1 : (int)p; }
  bool startsWith(const String& p) const { return _s.compare(0, p._s.size(), p._s) == 0; }
  bool endsWith(const String& p) const { return _s.size() >= p._s.size() && _s.compare(_s.size() - p._s.size(), p._s.size(), p._s) == 0; }
  String substring(unsigned int from) const { return from < _s.size() ? String(_s.substr(from)) : String(); }
  String substring(unsigned int from, unsigned int to) const {
    if (from > to) std::swap(from, to);
    if (from >= _s.size()) return String();
    return String(_s.substr(from, to - from));
  }
  void remove(unsigned int index) { if (index < _s.size()) _s.erase(index); }
  void remove(unsigned int index, unsigned int count) { if (index < _s.size()) _s.erase(index, count); }
  void replace(const String& a, const String& b) {
    if (a._s.empty()) return;
    size_t p = 0;
    while ((p = _s.find(a._s, p)) != std::string::npos) { _s.replace(p, a._s.size(), b._s); p += b._s.size(); }
  }
  void replace(char a, char b) { for (auto& c : _s) if (c == a) c = b; }
  void toLowerCase() { for (auto& c : _s) c = (char)tolower((unsigned char)c); }
  void toUpperCase() { for (auto& c : _s) c = (char)toupper((unsigned char)c); }
  void trim() {
    size_t a = 0, b = _s.size();
    while (a < b && isspace((unsigned char)_s[a])) a++;
    while (b > a && isspace((unsigned char)_s[b - 1])) b--;
    _s = _s.substr(a, b - a);
  }
  long toInt() const { return atol(_s.c_str()); }
  float toFloat() const { return (float)atof(_s.c_str()); }
  double toDouble() const { return atof(_s.c_str()); }
  void toCharArray(char* buf, unsigned int n) const { if (!n) return; strncpy(buf, _s.c_str(), n - 1); buf[n - 1] = 0; }
  void getBytes(unsigned char* buf, unsigned int n) const { toCharArray((char*)buf, n); }

  friend String operator+(const String& a, const String& b) { return String(a._s + b._s); }
  friend String operator+(const String& a, const char* b) { return String(a._s + (b ? b : "")); }
  friend String operator+(const char* a, const String& b) { return String(std::string(a ? a : "") + b._s); }
  friend String operator+(const String& a, char c) { return String(a._s + c); }
  friend String operator+(const String& a, int v) { return a + String(v); }
  friend String operator+(const String& a, unsigned int v) { return a + String(v); }
  friend String operator+(const String& a, long v) { return a + String(v); }
  friend String operator+(const String& a, unsigned long v) { return a + String(v); }
  friend String operator+(const String& a, float v) { return a + String(v); }
  friend String operator+(const String& a, double v) { return a + String(v); }

private:
  void num(long v, unsigned char base) {
    char b[40];
    if (base == 10) snprintf(b, sizeof(b), "%ld", v); else { unum((unsigned long)v, base); return; }
    _s = b;
  }
  void unum(unsigned long v, unsigned char base) {
    char b[40]; int i = 38; b[39] = 0;
    if (!v) { _s = "0"; return; }
    while (v && i >= 0) { int d = (int)(v % base); b[i--] = (char)(d < 10 ? '0' + d : 'a' + d - 10); v /= base; }
    _s = &b[i + 1];
  }
  void dbl(double v, unsigned int dec) { char b[48]; snprintf(b, sizeof(b), "%.*f", dec, v); _s = b; }
  std::string _s;
};

typedef String StringSumHelper;
