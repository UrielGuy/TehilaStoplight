// The board side of the "#ANIM" serial protocol on the PC: the real
// src/SerialLink.cpp and src/SetStore.cpp (plus Face/TextLayout for the text
// checks), with the stand-ins in this folder. stdin/stdout are the serial
// port; uploaded animations go to <SL_ROOT>/a/. Built and driven by
// tools/seriallink_test.py.
#include <unistd.h>
#include <deque>
#include <mutex>
#include <thread>
#include <dirent.h>
#include <sys/stat.h>
#include "Arduino.h"
#include "LittleFS.h"
#include "AnimStore.h"
#include "StopLightApp.h"
#include "SerialLink.h"

EspClass ESP;
SimFS LittleFS;
bool SnakeDisplay::s_rotated = true;
SetStore store;
SnakeDisplay display;
Mp3Player mp3;
AnimPerf animPerf;

static std::string root() { const char* r = getenv("SL_ROOT"); return r ? r : "."; }
std::string simPath(const char* p) { return root() + p; }

namespace AnimStore {
volatile int dbgRenderStage = 0, dbgDecStage[2] = {0, 0};
volatile uint32_t dbgRenderBeat = 0;
static const size_t TOTAL = 0x4B0000;
String pathFor(const String& id) { return "/a/" + id + ".anm"; }
String sanitizeId(const String& raw) {   // as src/AnimStore.cpp
  String s = raw;
  const int slash = s.lastIndexOf('/'); if (slash >= 0) s = s.substring(slash + 1);
  if (s.endsWith(".anm")) s = s.substring(0, s.length() - 4);
  String out;
  for (size_t i = 0; i < s.length() && out.length() < 24; ++i) {
    char ch = tolower(s[i]);
    if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_') out += ch;
    else if (ch == ' ' || ch == '.') out += '-';
  }
  return out.length() ? out : String("anim");
}
bool exists(const String& id) { return LittleFS.exists(pathFor(id)); }
bool remove(const String& id) { return LittleFS.remove(pathFor(id)); }
void closeAll() {}
std::vector<Entry> list() {
  std::vector<Entry> out;
  DIR* d = opendir((root() + "/a").c_str());
  if (!d) return out;
  while (dirent* e = readdir(d)) {
    std::string n = e->d_name;
    if (n.size() < 5 || n.substr(n.size() - 4) != ".anm" || n[0] == '.') continue;
    FILE* f = fopen((root() + "/a/" + n).c_str(), "rb");
    if (!f) continue;
    uint8_t h[12] = {0};
    fread(h, 1, 12, f);
    fseek(f, 0, SEEK_END);
    Entry en;
    en.id = String(n.substr(0, n.size() - 4));
    en.w = h[4] | h[5] << 8; en.h = h[6] | h[7] << 8; en.frames = h[8] | h[9] << 8;
    en.bytes = ftell(f); en.ms = 1000;
    fclose(f);
    out.push_back(en);
  }
  closedir(d);
  return out;
}
size_t totalBytes() { return TOTAL; }
size_t freeBytes() {
  if (getenv("SL_FREE")) return atol(getenv("SL_FREE"));
  size_t used = 0;
  for (const auto& e : list()) used += (e.bytes + 4095) / 4096 * 4096;
  return used > TOTAL ? 0 : TOTAL - used;
}
bool validate(const String& path, String& err) {
  FILE* f = fopen(simPath(path.c_str()).c_str(), "rb");
  uint8_t h[4] = {0};
  if (f) { fread(h, 1, 4, f); fclose(f); }
  if (memcmp(h, "SAN", 3) != 0 || h[3] < 1 || h[3] > 3) { err = "not an animation file"; return false; }
  return true;
}
bool load(const String&, AnimInfo&) { return false; }
BenchResult bench(const AnimInfo&, int) { BenchResult b{}; b.ok = false; return b; }
uint32_t internalFallbacks() { return 0; }
}  // namespace AnimStore

// stdin/stdout as the serial port; a reader thread keeps available() non-blocking.
class StdioStream : public Stream {
 public:
  StdioStream() { std::thread([this] { reader(); }).detach(); }
  int available() override { std::lock_guard<std::mutex> g(_m); return (int)_q.size(); }
  int read() override {
    std::lock_guard<std::mutex> g(_m);
    if (_q.empty()) return -1;
    const int c = _q.front(); _q.pop_front(); return c;
  }
  size_t readBytes(uint8_t* buf, size_t n) override {
    std::lock_guard<std::mutex> g(_m);
    size_t k = 0;
    while (k < n && !_q.empty()) { buf[k++] = _q.front(); _q.pop_front(); }
    return k;
  }
  size_t write(const uint8_t* buf, size_t n) override { fwrite(buf, 1, n, stdout); fflush(stdout); return n; }
  bool closed() { std::lock_guard<std::mutex> g(_m); return _eof && _q.empty(); }
 private:
  void reader() {
    uint8_t b[4096];
    for (;;) {
      const ssize_t n = ::read(0, b, sizeof b);
      std::lock_guard<std::mutex> g(_m);
      if (n <= 0) { _eof = true; return; }
      _q.insert(_q.end(), b, b + n);
    }
  }
  std::mutex _m;
  std::deque<uint8_t> _q;
  bool _eof = false;
};

int main() {
  mkdir((root() + "/a").c_str(), 0755);
  store.load();
  StdioStream io;
  StopLightApp app;
  SerialLink link(io, app);
  fprintf(stdout, "sim board up\n");   // a log line: the PC side must skip it
  fflush(stdout);
  while (!io.closed()) {
    link.poll();
    if (app.lockDepth() != 0) { fprintf(stderr, "[sim] LOCK LEAK %d\n", app.lockDepth()); return 2; }
    std::this_thread::sleep_for(std::chrono::microseconds(500));
  }
  return 0;
}
