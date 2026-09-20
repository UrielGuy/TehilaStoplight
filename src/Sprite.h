#pragma once
#include <stdint.h>
#include "Font.h"   // PROGMEM / pgm_read_byte shims

// A 1-bpp animated pictogram: nFrames frames of w x h, rows padded to bytes,
// MSB first, frames consecutive. frameMs = time per frame.
struct Sprite {
  const char* id;
  const char* name;
  uint16_t w, h;
  uint8_t  nFrames;
  uint16_t frameMs;
  const uint8_t* data;
  bool pixel(int frame, int x, int y) const {
    const int stride = (w + 7) / 8;
    const uint8_t b = pgm_read_byte(data + (size_t)frame * stride * h + y * stride + (x >> 3));
    return (b >> (7 - (x & 7))) & 1;
  }
};
