// Just enough of the Arduino core to compile src/SerialLink.cpp and
// src/SetStore.cpp on the PC (tools/seriallink_test.py). Not the real thing:
// String is a thin wrapper over std::string.
#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include "esp_heap_caps.h"

typedef uint8_t byte;
inline unsigned long millis() {
  using namespace std::chrono;
  static const auto t0 = steady_clock::now();
  return (unsigned long)duration_cast<milliseconds>(steady_clock::now() - t0).count();
}
inline void delay(unsigned long ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
using std::max;
using std::min;

class String {
 public:
  String() {}
  String(const char* s) { if (s) _s = s; }
  String(const std::string& s) : _s(s) {}
  String(const String&) = default;
  String& operator=(const String&) = default;
  String& operator=(const char* s) { _s = s ? s : ""; return *this; }
  explicit String(char c) : _s(1, c) {}
  explicit String(int v) : _s(std::to_string(v)) {}
  explicit String(unsigned v) : _s(std::to_string(v)) {}
  explicit String(long v) : _s(std::to_string(v)) {}
  explicit String(unsigned long v) : _s(std::to_string(v)) {}
  explicit String(long long v) : _s(std::to_string(v)) {}
  explicit String(unsigned long long v) : _s(std::to_string(v)) {}
  explicit String(double v, int dec = 2) { char b[64]; snprintf(b, sizeof b, "%.*f", dec, v); _s = b; }
  explicit String(float v, int dec = 2) : String((double)v, dec) {}
  size_t length() const { return _s.size(); }
  const char* c_str() const { return _s.c_str(); }
  bool isEmpty() const { return _s.empty(); }
  char operator[](size_t i) const { return i < _s.size() ? _s[i] : 0; }
  bool startsWith(const String& p) const { return _s.compare(0, p._s.size(), p._s) == 0; }
  bool endsWith(const String& p) const { return _s.size() >= p._s.size() && _s.compare(_s.size() - p._s.size(), p._s.size(), p._s) == 0; }
  String substring(size_t from, size_t to = std::string::npos) const {
    if (to > _s.size()) to = _s.size();
    if (from >= to) return String();
    return String(_s.substr(from, to - from));
  }
  int indexOf(char c, size_t from = 0) const { auto p = _s.find(c, from); return p == std::string::npos ? -1 : (int)p; }
  int indexOf(const String& t, size_t from = 0) const { auto p = _s.find(t._s, from); return p == std::string::npos ? -1 : (int)p; }
  int lastIndexOf(char c) const { auto p = _s.rfind(c); return p == std::string::npos ? -1 : (int)p; }
  long toInt() const { return atol(_s.c_str()); }
  void trim() {
    size_t a = 0, b = _s.size();
    while (a < b && isspace((unsigned char)_s[a])) ++a;
    while (b > a && isspace((unsigned char)_s[b - 1])) --b;
    _s = _s.substr(a, b - a);
  }
  bool concat(const char* s) { if (s) _s += s; return true; }
  String& operator+=(const String& o) { _s += o._s; return *this; }
  String& operator+=(const char* o) { if (o) _s += o; return *this; }
  String& operator+=(char c) { _s += c; return *this; }
  bool operator==(const String& o) const { return _s == o._s; }
  bool operator==(const char* o) const { return _s == (o ? o : ""); }
  bool operator!=(const String& o) const { return _s != o._s; }
  bool operator!=(const char* o) const { return !(*this == o); }
  bool operator<(const String& o) const { return _s < o._s; }
  const std::string& str() const { return _s; }
 private:
  std::string _s;
};
inline String operator+(const String& a, const String& b) { String r(a); r += b; return r; }
inline String operator+(const String& a, const char* b) { String r(a); r += b; return r; }
inline String operator+(const char* a, const String& b) { String r(a); r += b; return r; }
inline String operator+(const String& a, char b) { String r(a); r += b; return r; }
// Arduino's String + number appends the number (StringSumHelper).
inline String operator+(const String& a, int b) { return a + String(b); }
inline String operator+(const String& a, unsigned b) { return a + String(b); }
inline String operator+(const String& a, long b) { return a + String(b); }
inline String operator+(const String& a, unsigned long b) { return a + String(b); }
inline String operator+(const String& a, uint16_t b) { return a + String((unsigned)b); }
inline String operator+(const String& a, int16_t b) { return a + String((int)b); }
inline String operator+(const String& a, uint8_t b) { return a + String((unsigned)b); }

class Stream {
 public:
  virtual ~Stream() {}
  virtual int available() = 0;
  virtual int read() = 0;
  virtual size_t readBytes(uint8_t* buf, size_t n) = 0;
  virtual size_t write(const uint8_t* buf, size_t n) = 0;
  virtual void flush() {}
  size_t print(const String& s) { return write((const uint8_t*)s.c_str(), s.length()); }
};

struct EspClass {
  uint32_t getFreeHeap() { return 70000; }
  uint32_t getMinFreeHeap() { return 60000; }
  uint32_t getMaxAllocHeap() { return 40000; }
  uint32_t getPsramSize() { return 8 << 20; }
  uint32_t getFreePsram() { return 7 << 20; }
  [[noreturn]] void restart() { fprintf(stderr, "[sim] RESTART\n"); fflush(stdout); exit(3); }
};
extern EspClass ESP;
