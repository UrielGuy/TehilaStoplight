#pragma once
#include "Canvas.h"
#include "SnakeDisplay.h"

// Canvas that writes straight into the LED chain (2-bit levels).
class DisplayCanvas : public Canvas {
 public:
  explicit DisplayCanvas(SnakeDisplay& d) : _d(d) {}
  void set(int x, int y, Rgb2 c) override { _d.setPixel2(x, y, rgb2R(c), rgb2G(c), rgb2B(c)); }
 private:
  SnakeDisplay& _d;
};
