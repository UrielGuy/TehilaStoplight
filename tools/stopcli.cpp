// Host-side command line around the text engine, used by tools/local_server.py.
//   stopcli sizes                              -> JSON list of em sizes, largest first
//   stopcli check <light 0-2> <text> [maxSize] -> JSON {ok,size,lines,maxFit} | {ok:false,error,maxFit}
//   stopcli sprites                            -> JSON list of graphics
//   stopcli preview key=value ...              -> BMP on stdout (same bytes as /api/preview.bmp)
//   stopcli rle key=value ...                  -> RLE stream on stdout (same bytes as /api/preview.rle)
//   stopcli ppm key=value ...                  -> PPM on stdout
//   stopcli ppmfull / raw key=value ...        -> PPM / one 0bRRGGBB byte per pixel, all 64 colours (the
//                                                 LEDs' view: the preview canvas keeps only 16 colours)
//     keys as in the web API: t0..t2 text, s0..s2 size cap, c0..c2 circle colour (0bRRGGBB),
//     f0..f2 ink colour, g0..g2 graphic id, m lit mask, k elapsed ms (sprite frame),
//     o=1 unlit lights dark instead of a dim ring, g0..g2 = fx:quarters, fx:pulse... beat animations (Face.cpp FX_IDS)
//     with q0..q2 = "a,b,c" (quarters 2-4), hc0..hc2 / hf0..hf2 hue jump degrees per beat,
//     bpm tempo, b time on the beat grid (default k), fl the frame's length (fills)
// Build: g++ -std=c++11 -O2 -Isrc tools/stopcli.cpp src/TextLayout.cpp src/Face.cpp src/Preview.cpp -o tools/stopcli
#include <cstdio>
#include <cstring>
#include <string>
#include "Face.h"
#include "Preview.h"

// Every pixel's colour, no palette limit (what the LEDs show).
struct RawCanvas : Canvas {
  Rgb2 px[BitmapCanvas::H][BitmapCanvas::W] = {};
  void set(int x, int y, Rgb2 c) override { if (x >= 0 && x < BitmapCanvas::W && y >= 0 && y < BitmapCanvas::H) px[y][x] = c; }
};
static RawCanvas s_raw;

static std::string jsonEscape(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\n') o += "\\n";
    else if ((unsigned char)c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
    else o += c;
  }
  return o;
}

int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: stopcli check|preview|ppm ...\n"); return 2; }
  const std::string cmd = argv[1];
  if (cmd == "sizes") {
    printf("[");
    for (int i = 0; i < Face::fontSizeCount(); ++i) printf("%s%d", i ? "," : "", Face::fontSize(i));
    printf("]\n");
    return 0;
  }
  if (cmd == "check") {
    if (argc < 4) return 2;
    const int light = atoi(argv[2]), maxSize = argc > 4 ? atoi(argv[4]) : 0;
    TextLayout L = Face::layoutFor(light, argv[3], maxSize);
    TextLayout A = maxSize ? Face::layoutFor(light, argv[3], 0) : L;
    const int maxFit = A.ok() ? A.font->size : 0;
    if (L.ok()) printf("{\"ok\":true,\"size\":%d,\"lines\":%d,\"maxFit\":%d}\n", L.font->size, L.lines, maxFit);
    else printf("{\"ok\":false,\"error\":\"%s\",\"maxFit\":%d}\n", jsonEscape(L.error).c_str(), maxFit);
    return 0;
  }
  if (cmd == "sprites") {
    printf("[");
    for (int i = 0; i < Face::spriteCount(); ++i) {
      const Sprite& s = Face::spriteAt(i);
      printf("%s{\"id\":\"%s\",\"name\":\"%s\",\"frames\":%d,\"ms\":%d}", i ? "," : "", s.id, s.name, s.nFrames, s.frameMs);
    }
    printf("]\n");
    return 0;
  }
  if (cmd == "preview" || cmd == "ppm" || cmd == "rle" || cmd == "ppmfull" || cmd == "raw") {
    std::string kv[64];   // simple key -> value map by name
    auto get = [&](const std::string& key) -> const char* {
      for (int i = 2; i < argc; ++i) {
        const char* eq = strchr(argv[i], '=');
        if (eq && std::string(argv[i], eq - argv[i]) == key) return eq + 1;
      }
      return "";
    };
    TextLayout L[3];
    Face::LightSpec specs[3];
    for (int i = 0; i < 3; ++i) {
      const std::string n = std::to_string(i);
      specs[i].sprite = Face::spriteById(get("g" + n));
      specs[i].effect = Face::effectById(get("g" + n));
      if (!specs[i].sprite && !specs[i].effect && std::string(get("k" + n)) != "off") { L[i] = Face::layoutFor(i, get("t" + n), atoi(get("s" + n))); specs[i].text = &L[i]; }
      specs[i].bg = *get("c" + n) ? (atoi(get("c" + n)) & 63) : Face::defaultColour(i);
      specs[i].fg = atoi(get("f" + n)) & 63;
      specs[i].hueBg = atoi(get("hc" + n)) % 360;
      specs[i].hueFg = atoi(get("hf" + n)) % 360;
      const char* q = get("q" + n);
      for (int k = 0; k < 3 && *q; ++k) {
        specs[i].quarters[k] = atoi(q) & 63;
        q = strchr(q, ',');
        if (!q) break;
        ++q;
      }
    }
    Face::Beat beat;
    beat.bpm = atoi(get("bpm"));
    beat.frameMs = (uint32_t)atol(get("k"));
    beat.gridMs = *get("b") ? (uint32_t)atol(get("b")) : beat.frameMs;
    beat.frameLen = (uint32_t)atol(get("fl"));
    const uint8_t mask = *get("m") ? (atoi(get("m")) & 7) : 7;
    Face::setOffDark(atoi(get("o")) != 0);           // o=1: unlit lights completely dark
    if (cmd == "raw" || cmd == "ppmfull") {
      Face::draw(s_raw, specs, mask, (uint32_t)atol(get("k")), beat);
      static const uint8_t L[4] = {0, 140, 195, 255};
      if (cmd == "ppmfull") printf("P6\n%d %d\n255\n", BitmapCanvas::W, BitmapCanvas::H);
      for (int y = 0; y < BitmapCanvas::H; ++y)
        for (int x = 0; x < BitmapCanvas::W; ++x) {
          const Rgb2 v = s_raw.px[y][x];
          uint8_t px[3] = {L[rgb2R(v)], L[rgb2G(v)], L[rgb2B(v)]};
          if (v == RGB2_BLACK) px[0] = px[1] = px[2] = 12;
          if (cmd == "raw") fwrite(&v, 1, 1, stdout); else fwrite(px, 1, 3, stdout);
        }
      return 0;
    }
    BitmapCanvas c;
    Face::draw(c, specs, mask, (uint32_t)atol(get("k")), beat);
    if (cmd == "rle") {
      c.rleStream([](const uint8_t* p, size_t n, void*) { fwrite(p, 1, n, stdout); }, nullptr);
      return 0;
    }
    if (cmd == "preview") {
      uint8_t hdr[BitmapCanvas::BMP_HEADER], row[BitmapCanvas::ROW_BYTES];
      c.bmpHeader(hdr);
      fwrite(hdr, 1, sizeof hdr, stdout);
      for (int y = BitmapCanvas::H - 1; y >= 0; --y) { c.bmpRow(y, row); fwrite(row, 1, sizeof row, stdout); }
    } else {
      static const uint8_t L[4] = {0, 140, 195, 255};
      printf("P6\n%d %d\n255\n", BitmapCanvas::W, BitmapCanvas::H);
      for (int y = 0; y < BitmapCanvas::H; ++y)
        for (int x = 0; x < BitmapCanvas::W; ++x) {
          const Rgb2 v = c.colourAt(x, y);
          uint8_t px[3] = {L[rgb2R(v)], L[rgb2G(v)], L[rgb2B(v)]};
          if (v == RGB2_BLACK) px[0] = px[1] = px[2] = 12;
          fwrite(px, 1, 3, stdout);
        }
    }
    return 0;
  }
  fprintf(stderr, "unknown command\n");
  return 2;
}
