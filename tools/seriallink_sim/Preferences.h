#pragma once
// NVS in memory (one process = one boot).
#include "Arduino.h"
#include <map>
#include <vector>

class Preferences {
 public:
  bool begin(const char* ns, bool) { _ns = ns; return true; }
  void end() {}
  size_t getBytesLength(const char* k) { auto it = store().find(key(k)); return it == store().end() ? 0 : it->second.size(); }
  size_t getBytes(const char* k, void* buf, size_t n) {
    auto it = store().find(key(k));
    if (it == store().end()) return 0;
    n = std::min(n, it->second.size());
    memcpy(buf, it->second.data(), n);
    return n;
  }
  size_t putBytes(const char* k, const void* buf, size_t n) { store()[key(k)].assign((const uint8_t*)buf, (const uint8_t*)buf + n); return n; }
  size_t putBool(const char* k, bool v) { uint8_t b = v; return putBytes(k, &b, 1); }
  size_t putUChar(const char* k, uint8_t v) { return putBytes(k, &v, 1); }
  bool getBool(const char* k, bool d = false) { uint8_t b = d; getBytes(k, &b, 1); return b; }
  uint8_t getUChar(const char* k, uint8_t d = 0) { uint8_t b = d; getBytes(k, &b, 1); return b; }
  static std::map<std::string, std::vector<uint8_t>>& store() { static std::map<std::string, std::vector<uint8_t>> s; return s; }
 private:
  std::string key(const char* k) const { return _ns + "/" + k; }
  std::string _ns;
};
