#include "Face.h"
#include "fonts/StopFonts.h"
#include "sprites/Sprites.h"

namespace Face {

TextLayout layoutFor(int light, const std::string& utf8, int maxFontSize) {
  return layoutInCircle(utf8, centreX(light), centreY(light), R_INNER, STOP_FONTS, STOP_FONT_COUNT, MAX_LINES, maxFontSize);
}

int fontSizeCount() { return STOP_FONT_COUNT; }
int fontSize(int index) { return (index >= 0 && index < STOP_FONT_COUNT) ? STOP_FONTS[index]->size : 0; }

int spriteCount() { return SPRITE_COUNT; }
const Sprite& spriteAt(int i) { return *SPRITES[(i < 0 || i >= SPRITE_COUNT) ? 0 : i]; }
const Sprite* spriteById(const std::string& id) {
  if (id.empty()) return nullptr;
  for (int i = 0; i < SPRITE_COUNT; ++i) if (id == SPRITES[i]->id) return SPRITES[i];
  return nullptr;
}
int spriteFrame(const Sprite& s, uint32_t elapsedMs) {
  return s.nFrames <= 1 ? 0 : (int)((elapsedMs / (s.frameMs ? s.frameMs : 1)) % s.nFrames);
}

static bool s_offDark = false;
void setOffDark(bool dark) { s_offDark = dark; }
bool offDark() { return s_offDark; }

static void drawSprite(Canvas& c, const Sprite& s, int frame, int light, Rgb2 ink) {
  const int x0 = centreX(light) - s.w / 2, y0 = centreY(light) - s.h / 2;
  for (int y = 0; y < s.h; ++y)
    for (int x = 0; x < s.w; ++x)
      if (s.pixel(frame, x, y)) c.set(x0 + x, y0 + y, ink);
}

void draw(Canvas& c, const LightSpec specs[LIGHTS], uint8_t litMask, uint32_t elapsedMs) {
  const float cc = DIAMETER / 2.0f - 0.5f;          // 63.5: circle centred on the pixel grid
  const float r2 = (DIAMETER / 2.0f) * (DIAMETER / 2.0f);
  const float rIn2 = (DIAMETER / 2.0f - RING) * (DIAMETER / 2.0f - RING);
  for (int light = 0; light < LIGHTS; ++light) {
    const LightSpec& sp = specs[light];
    const bool lit = (litMask >> light) & 1;
    Rgb2 base = sp.bg;                       // 0 = a black circle (text still shows in its ink)
    Rgb2 ink = sp.fg;
    if (ink == base) ink = (base == RGB2_BLACK) ? rgb2(3, 3, 3) : RGB2_BLACK;   // never invisible
    const Rgb2 colour = lit ? base : rgb2Dim(base);
    const int y0 = light * DIAMETER;
    Rgb2 row[DIAMETER];
    for (int y = 0; y < DIAMETER; ++y) {
      const float dy = y - cc;
      for (int x = 0; x < DIAMETER; ++x) {
        const float dx = x - cc;
        const float d2 = dx * dx + dy * dy;
        row[x] = (d2 <= r2 && (lit || (!s_offDark && d2 >= rIn2))) ? colour : RGB2_BLACK;
      }
      c.setRow(0, y0 + y, row, DIAMETER);   // one call per row: much cheaper on the LEDs
    }
    if (!lit) continue;
    if (sp.sprite) drawSprite(c, *sp.sprite, spriteFrame(*sp.sprite, elapsedMs), light, ink);
    else if (sp.text && sp.text->ok()) drawLayout(*sp.text, c, ink);
  }
}

void drawSpriteBoxes(Canvas& c, const LightSpec specs[LIGHTS], uint8_t litMask, uint32_t elapsedMs) {
  const float cc = DIAMETER / 2.0f - 0.5f;
  const float r2 = (DIAMETER / 2.0f) * (DIAMETER / 2.0f);
  for (int light = 0; light < LIGHTS; ++light) {
    const LightSpec& sp = specs[light];
    if (!sp.sprite || !((litMask >> light) & 1)) continue;
    Rgb2 base = sp.bg;
    Rgb2 ink = sp.fg;
    if (ink == base) ink = (base == RGB2_BLACK) ? rgb2(3, 3, 3) : RGB2_BLACK;
    const Sprite& s = *sp.sprite;
    const int frame = spriteFrame(s, elapsedMs);
    const int x0 = centreX(light) - s.w / 2, y0 = centreY(light) - s.h / 2, yBase = light * DIAMETER;
    for (int y = 0; y < s.h; ++y) {
      const float dy = (y0 + y - yBase) - cc;
      for (int x = 0; x < s.w; ++x) {
        const float dx = (x0 + x) - cc;
        const bool inside = dx * dx + dy * dy <= r2;
        c.set(x0 + x, y0 + y, s.pixel(frame, x, y) && inside ? ink : inside ? base : RGB2_BLACK);
      }
    }
  }
}

void draw(Canvas& c, const TextLayout* const layouts[LIGHTS], uint8_t litMask, const Rgb2* colours) {
  LightSpec specs[LIGHTS];
  for (int i = 0; i < LIGHTS; ++i) { specs[i].text = layouts[i]; specs[i].bg = colours ? colours[i] : defaultColour(i); }
  draw(c, specs, litMask, 0);
}

}  // namespace Face
