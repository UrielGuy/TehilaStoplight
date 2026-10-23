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
// The colour turned `deg` degrees around the colour wheel (positive: red ->
// yellow -> green -> cyan -> blue -> magenta), keeping its brightest and
// darkest levels; greys and black stay as they are. Integer maths so the
// page's copy (WebPage.h, hueTurn) gives the same colours: 60/d is exact for
// the level differences d = 1..3, the middle level is rounded half up.
inline Rgb2 rgb2Hue(Rgb2 c, int deg) {
  const int r = rgb2R(c), g = rgb2G(c), b = rgb2B(c);
  const int mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b), d = mx - mn;
  if (!d || deg % 360 == 0) return c;
  int h = mx == r ? (g >= b ? 60 * (g - b) / d : 360 - 60 * (b - g) / d) : mx == g ? 120 + 60 * (b - r) / d : 240 + 60 * (r - g) / d;
  h = ((h + deg) % 360 + 360) % 360;
  const int up = mn + ((h % 60) * d + 30) / 60, down = mn + ((60 - h % 60) * d + 30) / 60;
  switch (h / 60) {
    case 0: return rgb2(mx, up, mn);
    case 1: return rgb2(down, mx, mn);
    case 2: return rgb2(mn, mx, up);
    case 3: return rgb2(mn, down, mx);
    case 4: return rgb2(up, mn, mx);
    default: return rgb2(mx, mn, down);
  }
}

struct Canvas {
  virtual ~Canvas() {}
  virtual void set(int x, int y, Rgb2 colour) = 0;
  // n pixels of row y starting at x0. Override where a whole row is much
  // cheaper than n single pixels (the LED display).
  virtual void setRow(int x0, int y, const Rgb2* px, int n) { for (int i = 0; i < n; ++i) set(x0 + i, y, px[i]); }
};
