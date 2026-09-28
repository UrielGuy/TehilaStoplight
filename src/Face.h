#pragma once
#include <string>
#include "Canvas.h"
#include "TextLayout.h"
#include "Sprite.h"

// The stop-light face: three 128 px circles (red, orange, green) stacked on
// the 128x384 canvas, each with optional black text.
namespace Face {
constexpr int LIGHTS   = 3;
constexpr int DIAMETER = 128;
constexpr int MARGIN   = 3;                       // keeps ink off the circle edge
constexpr int R_INNER  = DIAMETER / 2 - MARGIN;   // 61
constexpr int MAX_LINES = 8;

inline int centreX(int)      { return DIAMETER / 2; }
inline int centreY(int light){ return light * DIAMETER + DIAMETER / 2; }
// Default colours: a regular traffic light.
inline Rgb2 defaultColour(int light) { return light == 0 ? RGB2_RED : light == 1 ? RGB2_ORANGE : RGB2_GREEN; }
constexpr int RING = 3;                            // unlit light: ring thickness (2 px vanished in the phone preview)

// Lay out text for one light; error set if it does not fit. maxFontSize
// (em size, 0 = automatic) caps the size: the largest fitting size under the
// cap is used.
TextLayout layoutFor(int light, const std::string& utf8, int maxFontSize = 0);
// Available em sizes, largest first.
int fontSizeCount();
int fontSize(int index);
// What one light shows: a sprite if set, else the text layout (either may be
// null = just the circle). bg = circle colour (0 = black), fg = ink colour.
struct LightSpec {
  const TextLayout* text = nullptr;
  const Sprite* sprite = nullptr;
  Rgb2 bg = RGB2_RED;
  Rgb2 fg = RGB2_BLACK;
};

// Draw the whole face. litMask bit i = light i is lit; an unlit light is a
// dim ring with nothing inside. elapsedMs picks each sprite's frame.
void draw(Canvas& c, const LightSpec specs[LIGHTS], uint8_t litMask = 7, uint32_t elapsedMs = 0);
// Convenience for callers that only have text.
void draw(Canvas& c, const TextLayout* const layouts[LIGHTS], uint8_t litMask = 7,
          const Rgb2* colours = nullptr);

// Repaint only the sprite boxes of lit lights that carry a sprite (their
// frame for elapsedMs). Everything else on the face is unchanged, so this is
// enough after a full draw as long as the lit mask has not changed.
void drawSpriteBoxes(Canvas& c, const LightSpec specs[LIGHTS], uint8_t litMask, uint32_t elapsedMs);

int spriteCount();
const Sprite& spriteAt(int i);
const Sprite* spriteById(const std::string& id);   // null if unknown or empty
int spriteFrame(const Sprite& s, uint32_t elapsedMs);
}  // namespace Face
