#pragma once
#include <cstdint>
// Stand-in for src/Mp3Player.h: a module with three tracks.
class Mp3Player {
 public:
  void setVolume(uint8_t v) { _volume = v; }
  uint8_t volume() const { return _volume; }
  bool present() const { return true; }
  uint16_t trackCount() const { return 3; }
 private:
  uint8_t _volume = 20;
};
