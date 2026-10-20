// Host test of the version 3 animation decoder (src/AnimV3.cpp).
//
//   g++ -std=c++11 -O2 -Wall -Isrc tools/anim_v3_test.cpp src/AnimV3.cpp -o tools/anim_v3_test
//   python3 tools/anim_v3.py --prepare .pio/samples .pio/v3     # writes <name>.v3 + <name>.raw
//   ./tools/anim_v3_test .pio/v3/*.v3
//
// For every <name>.v3 the matching <name>.raw (u16 w, h, n then n raw
// frames, written by --prepare) is the reference.  Checks: every frame in
// sequence, 40 random seeks on a fresh reader, seeks on a running reader
// (forward, backward, same frame), the loop from the last frame to the first,
// and that the decoder reads only the frame's own bytes.  Prints decode time
// per frame (sequential) and the worst seek.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <string>
#include <vector>
#include "AnimV3.h"

struct Mem { std::vector<uint8_t> d; long reads = 0, bytes = 0; };

static int memRead(void* ctx, uint32_t off, uint8_t* buf, uint32_t len) {
  Mem* m = (Mem*)ctx;
  if (off > m->d.size()) return -1;
  uint32_t n = len;
  if (off + n > m->d.size()) n = (uint32_t)m->d.size() - off;
  memcpy(buf, m->d.data() + off, n);
  m->reads++; m->bytes += n;
  return (int)n;
}

struct Sink { std::vector<uint8_t> px; int w, h, rows; };
static void emitRow(void* ctx, int y, const uint8_t* px) {
  Sink* s = (Sink*)ctx;
  if (y >= 0 && y < s->h) { memcpy(&s->px[(size_t)y * s->w], px, s->w); s->rows++; }
}

static double now() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

static bool readFile(const std::string& p, std::vector<uint8_t>& out) {
  FILE* f = fopen(p.c_str(), "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  out.resize(n);
  bool ok = fread(out.data(), 1, n, f) == (size_t)n;
  fclose(f);
  return ok;
}

static V3Scratch sc;             // one decoder scratch (model + working buffers)
static uint8_t prevBuf[128 * 128];   // one per reader: it holds the reader's last frame
static uint8_t prevBuf2[128 * 128];

int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: anim_v3_test file.v3 ...\n"); return 2; }
  int failures = 0;
  double allFrames = 0, allTime = 0;
  printf("%-20s %7s %6s  %9s %9s %9s  %8s %8s  %s\n", "sample", "size", "frames", "avg ms", "max ms", "seek ms", "bytes", "reads/fr", "ok");
  for (int a = 1; a < argc; ++a) {
    std::string v3p = argv[a];
    std::string rawp = v3p.substr(0, v3p.size() - 3) + ".raw";
    Mem mem; std::vector<uint8_t> raw;
    if (!readFile(v3p, mem.d) || !readFile(rawp, raw)) { printf("%s: cannot read %s / %s\n", v3p.c_str(), v3p.c_str(), rawp.c_str()); failures++; continue; }
    const int w = raw[0] | (raw[1] << 8), h = raw[2] | (raw[3] << 8), n = raw[4] | (raw[5] << 8);
    const size_t fsz = (size_t)w * h;
    if (raw.size() != 6 + fsz * n) { printf("%s: raw size mismatch\n", rawp.c_str()); failures++; continue; }
    auto ref = [&](int i) { return raw.data() + 6 + fsz * i; };
    static V3Reader r;
    if (!v3Begin(r, memRead, &mem, prevBuf, sizeof(prevBuf))) { printf("%s: v3Begin failed\n", v3p.c_str()); failures++; continue; }
    if (r.w != w || r.h != h || r.n != n) { printf("%s: header mismatch\n", v3p.c_str()); failures++; continue; }
    Sink s; s.w = w; s.h = h; s.px.assign(fsz, 0);
    std::string err;
    // 1. sequential play, timed
    double tsum = 0, tmax = 0;
    long reads0 = mem.reads, bytes0 = mem.bytes;
    for (int i = 0; i < n && err.empty(); ++i) {
      s.rows = 0;
      double t0 = now();
      bool ok = v3DecodeFrame(r, sc, i, emitRow, &s);
      double dt = (now() - t0) * 1e3;
      tsum += dt; if (dt > tmax) tmax = dt;
      if (!ok || s.rows != h || memcmp(s.px.data(), ref(i), fsz)) { err = "frame " + std::to_string(i) + " wrong (sequential)"; }
    }
    double readsPerFrame = (mem.reads - reads0) / (double)n;
    long bytesRead = mem.bytes - bytes0;
    // 2. loop: last frame then frame 0
    if (err.empty()) {
      s.rows = 0;
      if (!v3DecodeFrame(r, sc, 0, emitRow, &s) || memcmp(s.px.data(), ref(0), fsz)) err = "loop to frame 0 wrong";
    }
    // 3. random seeks on a fresh reader (worst case: start of a group)
    double seekMax = 0;
    srand(12345);
    for (int k = 0; k < 40 && err.empty(); ++k) {
      int i = rand() % n;
      if (k == 0) i = n - 1;
      V3Reader r2;
      v3Begin(r2, memRead, &mem, prevBuf2, sizeof(prevBuf2));
      s.rows = 0;
      double t0 = now();
      bool ok = v3DecodeFrame(r2, sc, i, emitRow, &s);
      double dt = (now() - t0) * 1e3;
      if (dt > seekMax) seekMax = dt;
      if (!ok || memcmp(s.px.data(), ref(i), fsz)) err = "frame " + std::to_string(i) + " wrong (fresh seek)";
    }
    // 4. seeks on the running reader: forward within the group, backward, same frame twice
    for (int k = 0; k < 40 && err.empty(); ++k) {
      int i = rand() % n;
      for (int rep = 0; rep < 2 && err.empty(); ++rep) {
        s.rows = 0;
        if (!v3DecodeFrame(r, sc, i, emitRow, &s) || memcmp(s.px.data(), ref(i), fsz)) err = "frame " + std::to_string(i) + " wrong (running seek)";
      }
      int j = i + 1 + rand() % 3; if (j >= n) j = 0;
      s.rows = 0;
      if (err.empty() && (!v3DecodeFrame(r, sc, j, emitRow, &s) || memcmp(s.px.data(), ref(j), fsz))) err = "frame " + std::to_string(j) + " wrong (running seek +)";
    }
    // 5. a frame must not need bytes beyond its own: truncate the file after frame n-1 and decode it
    if (err.empty()) {
      Mem m2 = mem;
      V3Reader r3;
      v3Begin(r3, memRead, &m2, prevBuf2, sizeof(prevBuf2));
      uint8_t e[6]; uint32_t off; uint8_t kb[4];
      // locate the last frame: key offset of its group plus the preceding frames
      int last = n - 1, key = last - last % r3.K;
      memRead(&m2, r3.keyTableOff + 4u * (key / r3.K), kb, 4);
      off = kb[0] | (kb[1] << 8) | (kb[2] << 16) | ((uint32_t)kb[3] << 24);
      for (int i = key; i < last; ++i) { memRead(&m2, 20 + 6u * i, e, 6); off += e[2] | (e[3] << 8) | (e[4] << 16) | ((uint32_t)e[5] << 24); }
      memRead(&m2, 20 + 6u * last, e, 6);
      uint32_t len = e[2] | (e[3] << 8) | (e[4] << 16) | ((uint32_t)e[5] << 24);
      if (off + len != m2.d.size()) err = "last frame does not end the file";
      m2.d.resize(off + len);
      s.rows = 0;
      if (err.empty() && (!v3DecodeFrame(r3, sc, last, emitRow, &s) || memcmp(s.px.data(), ref(last), fsz))) err = "last frame wrong on truncated file";
    }
    allFrames += n; allTime += tsum;
    printf("%-20s %7zu %6d  %9.3f %9.3f %9.3f  %8ld %8.1f  %s\n", v3p.substr(v3p.find_last_of('/') + 1).c_str(), mem.d.size(), n,
           tsum / n, tmax, seekMax, bytesRead, readsPerFrame, err.empty() ? "OK" : err.c_str());
    if (!err.empty()) failures++;
  }
  printf("\n%s: %.0f frames, %.3f ms per frame on this PC (sizeof V3Reader %zu, V3Scratch %zu, prev %zu)\n",
         failures ? "FAILED" : "ALL OK", allFrames, allFrames ? allTime / allFrames : 0, sizeof(V3Reader), sizeof(V3Scratch), sizeof(prevBuf));
  return failures ? 1 : 0;
}
