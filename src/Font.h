#pragma once
#include <stdint.h>
#ifdef ARDUINO
#include <pgmspace.h>
#else
#define PROGMEM
#define pgm_read_byte(p) (*(const uint8_t*)(p))
#endif

// One rasterised glyph. Bitmap rows are padded to whole bytes, MSB first.
// xOff/yOff position the bitmap relative to the pen position on the line-box
// top-left (yOff is measured down from the ascender line).
struct Glyph {
  uint16_t cp;
  uint8_t  w, h;
  int8_t   xOff, yOff;
  uint8_t  adv;
  uint32_t off;
};

struct Font {
  uint8_t  size;        // em size the font was rasterised at
  uint8_t  ascent;      // pixels above the baseline in the line box
  uint8_t  descent;     // pixels below the baseline
  uint8_t  lineHeight;  // ascent + descent
  uint16_t nGlyphs;
  const Glyph*   glyphs;   // sorted by codepoint
  const uint8_t* bitmap;
};

// Binary search; returns nullptr if the codepoint has no glyph.
inline const Glyph* fontFind(const Font& f, uint32_t cp) {
  int lo = 0, hi = (int)f.nGlyphs - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    uint32_t c = f.glyphs[mid].cp;
    if (c == cp) return &f.glyphs[mid];
    if (c < cp) lo = mid + 1; else hi = mid - 1;
  }
  return nullptr;
}

inline bool glyphBit(const Font& f, const Glyph& g, int x, int y) {
  const int stride = (g.w + 7) / 8;
  const uint8_t b = pgm_read_byte(f.bitmap + g.off + y * stride + (x >> 3));
  return (b >> (7 - (x & 7))) & 1;
}
