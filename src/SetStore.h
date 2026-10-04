#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

// What one light shows. bg/ink are Rgb2 (0bRRGGBB, 0 = black);
// COLOUR_RANDOM = pick a random non-black colour each time it is shown.
static const uint8_t COLOUR_RANDOM = 64;

struct LightDef {
  String text;
  String kind = "short";     // "short" | "long" (which text box the page shows) | "off" (light unlit)
  String graphic;            // sprite id, empty = text
  uint8_t size = 0;          // max font em size, 0 = automatic
  uint8_t bg = 0;
  uint8_t ink = 0;
  bool on = true;            // false when kind == "off": the light stays dark (dim ring)
};

struct KeyFrame {
  LightDef l[3];
  uint32_t ms = 1000;
  int16_t track = 0;          // audio when the frame starts: 0 = leave as is, -1 = stop, n = play track n
  uint8_t mask() const { return (l[0].on ? 1 : 0) | (l[1].on ? 2 : 0) | (l[2].on ? 4 : 0); }
};

// A section of frames repeated `count` times (1-based, inclusive, as shown
// on the page). Sections must not overlap.
struct Loop { uint16_t from = 1, to = 1, count = 1; };

// A set is a keyframe sequence with optional loop sections; it plays once
// through (loops expanded), then the sign moves on to the next set.
struct TextSet {
  String name;
  std::vector<KeyFrame> frames;
  std::vector<Loop> loops;
  // Frame indices (0-based) in play order, loops expanded.
  std::vector<uint16_t> playlist() const;
};

void lightsToJson(JsonObject o, const LightDef l[3]);
void lightsFromJson(JsonObjectConst o, LightDef l[3]);
void frameFromJson(JsonObjectConst o, KeyFrame& f);
void setToJson(JsonObject o, const TextSet& s);
void setFromJson(JsonObjectConst o, TextSet& s);

class SetStore {
 public:
  static constexpr size_t MAX_SETS = 40;
  static constexpr size_t MAX_FRAMES = 64;
  static constexpr size_t MAX_LOOPS = 16;
  static constexpr size_t MAX_TEXT_BYTES = 720;
  static constexpr size_t MAX_JSON_BYTES = 32000;   // whole store, NVS blob + heap budget

  void load();
  bool save();                         // false if over MAX_JSON_BYTES (nothing written)
  std::vector<TextSet>& sets() { return _sets; }
  String toJson() const;
 private:
  std::vector<TextSet> _sets;
};
