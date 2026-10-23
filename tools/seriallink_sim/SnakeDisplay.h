#pragma once
// Stand-in for src/SnakeDisplay.h: only what SerialLink uses.
class SnakeDisplay {
 public:
  bool colourRotated() const { return _colourRotated; }
  static void setRotated(bool r) { s_rotated = r; }
  static bool rotated() { return s_rotated; }
  bool _colourRotated = true;
  static bool s_rotated;
};
