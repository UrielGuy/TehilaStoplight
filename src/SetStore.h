#pragma once
#include <Arduino.h>
#include <vector>

// A "set" is one text per light (top, middle, bottom). Sets rotate on the
// display every `intervalSec`. Persisted in NVS as JSON.
struct TextSet {
  String text[3];
  uint8_t size[3] = {0, 0, 0};   // max em size per text, 0 = automatic (largest that fits)
  uint8_t colour[3] = {48, 52, 12};   // circle colours, Rgb2 (0bRRGGBB): red, orange, green
  uint8_t ink[3] = {0, 0, 0};         // text / figure colour, Rgb2 (default black)
  String graphic[3];                  // sprite id, empty = show the text
  String anim = "all";           // Animation id, see Animation.cpp
  uint16_t unitMs = 1000;        // one animation time unit
};

class SetStore {
 public:
  static constexpr size_t MAX_SETS = 40;
  static constexpr size_t MAX_TEXT_BYTES = 720;   // 240 chars of Hebrew in UTF-8

  void load();
  void save();

  std::vector<TextSet>& sets() { return _sets; }
  uint32_t intervalSec() const { return _intervalSec; }
  void setIntervalSec(uint32_t s) { _intervalSec = s < 5 ? 5 : s > 86400 ? 86400 : s; }

  String toJson() const;             // {"interval":60,"sets":[{"t":["a","b","c"],"s":[0,36,0],"a":"traffic","p":1000},...]}
 private:
  std::vector<TextSet> _sets;
  uint32_t _intervalSec = 60;
};
