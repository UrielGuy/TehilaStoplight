#pragma once
// LittleFS on a PC folder: "/a/x.anm" is <SL_ROOT>/a/x.anm.
#include "Arduino.h"
#include <cstdio>

std::string simPath(const char* p);

class File {
 public:
  File() {}
  explicit File(FILE* f) : _f(f) {}
  explicit operator bool() const { return _f != nullptr; }
  size_t write(const uint8_t* b, size_t n) { return _f ? fwrite(b, 1, n, _f) : 0; }
  int read(uint8_t* b, size_t n) { return _f ? (int)fread(b, 1, n, _f) : -1; }
  bool seek(uint32_t pos) { return _f && fseek(_f, pos, SEEK_SET) == 0; }
  void close() { if (_f) fclose(_f); _f = nullptr; }
 private:
  FILE* _f = nullptr;
};

class SimFS {
 public:
  File open(const String& path, const char* mode) { return File(fopen(simPath(path.c_str()).c_str(), *mode == 'w' ? "wb" : "rb")); }
  bool remove(const String& path) { return ::remove(simPath(path.c_str()).c_str()) == 0; }
  bool rename(const String& a, const String& b) { return ::rename(simPath(a.c_str()).c_str(), simPath(b.c_str()).c_str()) == 0; }
  bool exists(const String& path) { FILE* f = fopen(simPath(path.c_str()).c_str(), "rb"); if (f) fclose(f); return f != nullptr; }
};
extern SimFS LittleFS;
