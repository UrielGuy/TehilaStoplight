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

uint8_t effectById(const std::string& id) { return id == "fx:quarters" ? FX_QUARTERS : id == "fx:pulse" ? FX_PULSE : FX_NONE; }

static int hueDeg(uint32_t steps, int perBeat) { return (int)((int64_t)steps * perBeat % 360); }
Rgb2 beatBg(const LightSpec& s, const Beat& b) { return s.hueBg ? rgb2Hue(s.bg, hueDeg(b.steps(), s.hueBg)) : s.bg; }
Rgb2 beatFg(const LightSpec& s, const Beat& b) { return s.hueFg ? rgb2Hue(s.fg, hueDeg(b.steps(), s.hueFg)) : s.fg; }

// FX_PULSE: full size on the beat, easing back to 75% (reached an eighth of
// a beat before the next one). Doubled pixel units, 96..128.
static int pulseDiameter(const Beat& b) {
  if (!b.bpm) return DIAMETER * 3 / 4;
  const uint64_t rem = 60000 - b.phase();   // 60000 on the beat .. 1 just before the next one
  return DIAMETER * 3 / 4 + (int)(((uint64_t)(DIAMETER / 4) * rem * rem + 1800000000ull) / 3600000000ull);
}

uint32_t beatState(const LightSpec& s, bool lit, const Beat& b) {
  if (!lit || !b.bpm) return 0;
  uint32_t k = (s.hueBg || s.hueFg || s.effect == FX_QUARTERS) ? b.steps() + 1 : 0;
  if (s.effect == FX_PULSE) k = k * 256 + (uint32_t)pulseDiameter(b);
  return k;
}

// Circle tests in doubled units, (2x-127)^2 + (2y-127)^2 <= D^2: the circle
// is centred on the pixel grid (63.5) and the sums stay integer.
void drawLight(Canvas& c, const LightSpec& sp, int light, bool lit, uint32_t elapsedMs, const Beat& beat) {
  const Rgb2 base = lit ? beatBg(sp, beat) : sp.bg;   // 0 = a black circle (text still shows in its ink)
  Rgb2 ink = lit ? beatFg(sp, beat) : sp.fg;
  if (ink == base) ink = (base == RGB2_BLACK) ? rgb2(3, 3, 3) : RGB2_BLACK;   // never invisible
  Rgb2 quarter[4] = {base, base, base, base};          // top left, top right, bottom right, bottom left
  int dia = DIAMETER;
  if (lit && sp.effect == FX_QUARTERS) {
    const uint32_t n = beat.steps();
    const int deg = hueDeg(n, sp.hueBg);
    const Rgb2 cols[4] = {base, rgb2Hue(sp.quarters[0], deg), rgb2Hue(sp.quarters[1], deg), rgb2Hue(sp.quarters[2], deg)};
    for (int q = 0; q < 4; ++q) quarter[q] = cols[(q + 4 - n % 4) % 4];   // every beat each colour moves one quarter clockwise
  } else if (lit && sp.effect == FX_PULSE) {
    dia = pulseDiameter(beat);
  }
  const int full2 = DIAMETER * DIAMETER, r2 = dia * dia, rIn2 = (DIAMETER - 2 * RING) * (DIAMETER - 2 * RING);
  const Rgb2 ring = rgb2Dim(sp.bg);
  const bool ringOn = !lit && !s_offDark;
  const Sprite* s = lit && !sp.effect ? sp.sprite : nullptr;
  const TextLayout* text = (lit && !sp.effect && !s && sp.text && sp.text->ok()) ? sp.text : nullptr;
  const int frame = s ? spriteFrame(*s, elapsedMs) : 0;
  const int sx0 = s ? DIAMETER / 2 - s->w / 2 : 0, sy0 = s ? DIAMETER / 2 - s->h / 2 : 0;   // sprite box in the tile
  const int y0 = light * DIAMETER;
  Rgb2 row[DIAMETER];
  for (int y = 0; y < DIAMETER; ++y) {
    const int dy = 2 * y - (DIAMETER - 1), dy2 = dy * dy;
    for (int x = 0; x < DIAMETER; ++x) {
      const int dx = 2 * x - (DIAMETER - 1), d2 = dx * dx + dy2;
      Rgb2 v = RGB2_BLACK;
      if (lit) { if (d2 <= r2) v = quarter[dy < 0 ? (dx < 0 ? 0 : 1) : (dx < 0 ? 3 : 2)]; }
      else if (ringOn && d2 <= full2 && d2 >= rIn2) v = ring;
      row[x] = v;
    }
    if (s && y >= sy0 && y < sy0 + s->h)
      for (int x = 0; x < s->w; ++x) {
        const int dx = 2 * (sx0 + x) - (DIAMETER - 1);
        if (dx * dx + dy2 <= full2 && s->pixel(frame, x, y - sy0)) row[sx0 + x] = ink;
      }
    if (text) drawLayoutRow(*text, y0 + y, row, 0, DIAMETER, ink);
    c.setRow(0, y0 + y, row, DIAMETER);   // one call per row: much cheaper on the LEDs
  }
}

void draw(Canvas& c, const LightSpec specs[LIGHTS], uint8_t litMask, uint32_t elapsedMs, const Beat& beat) {
  for (int light = 0; light < LIGHTS; ++light) drawLight(c, specs[light], light, (litMask >> light) & 1, elapsedMs, beat);
}

void drawSpriteBoxes(Canvas& c, const LightSpec specs[LIGHTS], uint8_t litMask, uint32_t elapsedMs, const Beat& beat) {
  const float cc = DIAMETER / 2.0f - 0.5f;
  const float r2 = (DIAMETER / 2.0f) * (DIAMETER / 2.0f);
  for (int light = 0; light < LIGHTS; ++light) {
    const LightSpec& sp = specs[light];
    if (!sp.sprite || sp.effect || !((litMask >> light) & 1)) continue;
    Rgb2 base = beatBg(sp, beat);
    Rgb2 ink = beatFg(sp, beat);
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
