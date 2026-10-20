#pragma once
#include "Canvas.h"
#include "SnakeDisplay.h"

// Canvas that writes straight into the LED chain (2-bit levels).
class DisplayCanvas : public Canvas {
 public:
  explicit DisplayCanvas(SnakeDisplay& d) : _d(d) {}
  void set(int x, int y, Rgb2 c) override { _d.writeRow(x, y, &c, 1); }
  void setRow(int x0, int y, const Rgb2* px, int n) override { _d.writeRow(x0, y, px, n); }
 private:
  SnakeDisplay& _d;
};
