#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include "Font.h"
#include "Canvas.h"

// Lays out a UTF-8 string (Latin and/or Hebrew) inside a circle, choosing the
// largest font size that fits, word-wrapping as needed, ordering Hebrew runs
// right-to-left, and centring the ink of each line and of the whole block.

struct PlacedGlyph {
  const Font*  font;
  const Glyph* g;
  int x, y;   // canvas position of the bitmap's top-left
};

struct TextLayout {
  const Font* font = nullptr;
  int lines = 0;
  std::vector<PlacedGlyph> glyphs;
  std::string error;                 // non-empty when the text does not fit
  bool ok() const { return font != nullptr && error.empty(); }
};

std::vector<uint32_t> utf8Decode(const std::string& s);
std::string utf8Encode(uint32_t cp);
// Reorders a logical-order line for display: Hebrew runs reversed, the run
// order reversed when the line is right-to-left. Brackets are mirrored.
std::vector<uint32_t> bidiVisual(const std::vector<uint32_t>& logical);

// cx, cy: circle centre; rInner: radius the ink must stay within.
// maxFontSize caps the em size (0 = no cap): the largest size that fits and
// is <= the cap is used.
TextLayout layoutInCircle(const std::string& utf8, int cx, int cy, int rInner,
                          const Font* const* fonts, int nFonts, int maxLines = 3,
                          int maxFontSize = 0);

void drawLayout(const TextLayout& L, Canvas& c, Rgb2 colour);
// The ink of canvas row y, written into row[0..n) which holds canvas
// columns x0..x0+n (for drawing a whole row at once).
void drawLayoutRow(const TextLayout& L, int y, Rgb2* row, int x0, int n, Rgb2 colour);
