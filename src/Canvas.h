#pragma once
#include <stdint.h>

// Colours are 2 bits per channel, packed 0bRRGGBB (0..63), exactly what the
// panel can show. 0 is black.
typedef uint8_t Rgb2;
constexpr Rgb2 rgb2(uint8_t r, uint8_t g, uint8_t b) { return ((r & 3) << 4) | ((g & 3) << 2) | (b & 3); }
inline uint8_t rgb2R(Rgb2 c) { return (c >> 4) & 3; }
inline uint8_t rgb2G(Rgb2 c) { return (c >> 2) & 3; }
inline uint8_t rgb2B(Rgb2 c) { return c & 3; }
constexpr Rgb2 RGB2_BLACK  = 0;
constexpr Rgb2 RGB2_RED    = rgb2(3, 0, 0);
constexpr Rgb2 RGB2_ORANGE = rgb2(3, 1, 0);
constexpr Rgb2 RGB2_GREEN  = rgb2(0, 3, 0);
// Dim version for an unlit light's ring: every non-zero channel at level 1.
inline Rgb2 rgb2Dim(Rgb2 c) { return rgb2(rgb2R(c) ? 1 : 0, rgb2G(c) ? 1 : 0, rgb2B(c) ? 1 : 0); }
// 8-bit value of a 2-bit level, for previews.
inline uint8_t level8(uint8_t l) { return (uint8_t)(l * 85); }

struct Canvas {
  virtual ~Canvas() {}
  virtual void set(int x, int y, Rgb2 colour) = 0;
  // n pixels of row y starting at x0. Override where a whole row is much
  // cheaper than n single pixels (the LED display).
  virtual void setRow(int x0, int y, const Rgb2* px, int n) { for (int i = 0; i < n; ++i) set(x0 + i, y, px[i]); }
};
