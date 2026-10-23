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
// Beat animations, picked in a light's graphic list like the figures.
// FX_QUARTERS splits the circle in four colours (bg, then quarters[0..2],
// clockwise from the top left) that turn 30 degrees clockwise on every
// beat; FX_PULSE is a disc at 75% of the circle that jumps to full size on
// every beat and shrinks back during the beat; FX_WAVE is a horizontal line
// in the ink colour bent into one sine period that swings on the beat: full
// (left up, right down) on beat 1, 3, 5..., flat mid-beat, full the other way
// on beat 2, 4...; FX_CLOCK is a watch face that starts at 12:00 and
// gains 5 minutes on every beat; FX_ECG is a heart monitor trace scrolling
// right to left, its spike at the centre on every beat (all in the ink colour);
// FX_ECGSWEEP keeps the trace still and shows it through a window sweeping
// left to right that reaches the spike on the beat.
// Then (Face.cpp, RowFx): a beating heart, ripples, an equaliser, chasing
// lights, a bouncing ball, sparkles (on time, not the beat) and two fills
// that complete 100 ms before the frame ends (Beat::frameLen).
enum : uint8_t { FX_NONE = 0, FX_QUARTERS = 1, FX_PULSE = 2, FX_WAVE = 3, FX_CLOCK = 4, FX_ECG = 5,
                 FX_HEART = 6, FX_RIPPLES = 7, FX_EQ = 8, FX_CHASE = 9, FX_BALL = 10, FX_SPARKLE = 11,
                 FX_FILLV = 12, FX_FILLX = 13, FX_ECGSWEEP = 14 };
uint8_t effectById(const std::string& id);   // "fx:quarters", "fx:pulse"... (see Face.cpp), else FX_NONE

// Where the sign is on the beat (the set's tempo). The grid starts with the
// set and again at every frame that starts a sound track, so it follows the
// song; hue jumps and the quarters count the beats since their frame started.
struct Beat {
  uint16_t bpm = 0;       // 0 = no beat: everything holds still
  uint32_t gridMs = 0;    // time since the grid started
  uint32_t frameMs = 0;   // time since this frame started (<= gridMs)
  uint32_t frameLen = 0;  // the frame's length (fills finish 100 ms before it ends; 0 = unknown: filled)
  uint32_t steps() const {   // beats since the frame started
    if (!bpm) return 0;
    const uint32_t start = gridMs >= frameMs ? gridMs - frameMs : 0;
    return (uint32_t)((uint64_t)gridMs * bpm / 60000 - (uint64_t)start * bpm / 60000);
  }
  uint32_t phase() const { return bpm ? (uint32_t)((uint64_t)gridMs * bpm % 60000) : 0; }   // in the current beat, 0..59999
};

// What one light shows: a sprite if set, else the text layout (either may be
// null = just the circle), or a beat animation. bg = circle colour (0 =
// black), fg = ink colour.
struct LightSpec {
  const TextLayout* text = nullptr;
  const Sprite* sprite = nullptr;
  Rgb2 bg = RGB2_RED;
  Rgb2 fg = RGB2_BLACK;
  uint8_t effect = FX_NONE;
  Rgb2 quarters[3] = {rgb2(3, 3, 0), RGB2_GREEN, rgb2(0, 0, 3)};   // FX_QUARTERS: quarters 2-4
  int16_t hueBg = 0, hueFg = 0;   // degrees the circle colour (and the quarters) / the ink turns on every beat
};

// The light's colours at this point of the beat (hue jumps applied).
Rgb2 beatBg(const LightSpec& s, const Beat& b);
Rgb2 beatFg(const LightSpec& s, const Beat& b);
// Changes whenever the beat changes how a light looks (hue jumps, quarters,
// pulse size, wave amplitude, clock hands, ECG scroll): the render loop redraws that light then.
uint32_t beatState(const LightSpec& s, bool lit, const Beat& b);

// Global look of an unlit light: a dim ring (default) or completely dark.
void setOffDark(bool dark);
bool offDark();

// Draw the whole face. litMask bit i = light i is lit; an unlit light is a
// dim ring with nothing inside (or dark, see setOffDark). elapsedMs picks
// each sprite's frame.
void draw(Canvas& c, const LightSpec specs[LIGHTS], uint8_t litMask = 7, uint32_t elapsedMs = 0, const Beat& beat = Beat());
// One light (canvas rows light*128 .. +127), a whole row at a time with
// every pixel written once, so a redraw on the LEDs never flickers.
void drawLight(Canvas& c, const LightSpec& spec, int light, bool lit, uint32_t elapsedMs, const Beat& beat);
// Convenience for callers that only have text.
void draw(Canvas& c, const TextLayout* const layouts[LIGHTS], uint8_t litMask = 7,
          const Rgb2* colours = nullptr);

// Repaint only the sprite boxes of lit lights that carry a sprite (their
// frame for elapsedMs). Everything else on the face is unchanged, so this is
// enough after a full draw as long as the lit mask has not changed.
void drawSpriteBoxes(Canvas& c, const LightSpec specs[LIGHTS], uint8_t litMask, uint32_t elapsedMs, const Beat& beat = Beat());

int spriteCount();
const Sprite& spriteAt(int i);
const Sprite* spriteById(const std::string& id);   // null if unknown or empty
int spriteFrame(const Sprite& s, uint32_t elapsedMs);
}  // namespace Face
