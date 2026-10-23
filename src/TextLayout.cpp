#include "TextLayout.h"
#include <math.h>

// ---------------------------------------------------------------- UTF-8 ----
std::vector<uint32_t> utf8Decode(const std::string& s) {
  std::vector<uint32_t> out;
  for (size_t i = 0; i < s.size();) {
    unsigned char c = s[i];
    uint32_t cp; int n;
    if (c < 0x80)       { cp = c;        n = 1; }
    else if (c < 0xE0)  { cp = c & 0x1F; n = 2; }
    else if (c < 0xF0)  { cp = c & 0x0F; n = 3; }
    else                { cp = c & 0x07; n = 4; }
    for (int k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | (s[i + k] & 0x3F);
    out.push_back(cp);
    i += n;
  }
  return out;
}

std::string utf8Encode(uint32_t cp) {
  std::string o;
  if (cp < 0x80) o += (char)cp;
  else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
  else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
  else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
  return o;
}

// ----------------------------------------------------------------- bidi ----
static bool isHebrew(uint32_t cp) { return cp >= 0x0590 && cp <= 0x05FF; }
static bool isNikud(uint32_t cp)  { return cp >= 0x0591 && cp <= 0x05C7; }
static bool isStrongL(uint32_t cp) {
  return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || (cp >= '0' && cp <= '9') ||
         (cp >= 0x00C0 && cp <= 0x024F);
}
static uint32_t mirror(uint32_t cp) {
  switch (cp) {
    case '(': return ')'; case ')': return '(';
    case '[': return ']'; case ']': return '[';
    case '{': return '}'; case '}': return '{';
    case '<': return '>'; case '>': return '<';
    default: return cp;
  }
}

std::vector<uint32_t> bidiVisual(const std::vector<uint32_t>& in) {
  const size_t n = in.size();
  bool anyR = false;
  for (uint32_t cp : in) if (isHebrew(cp)) anyR = true;
  if (!anyR || n == 0) return in;

  // Classify: 'R', 'L' or 'N' (neutral); resolve neutrals by their neighbours,
  // falling back to the paragraph direction (R).
  std::vector<char> cls(n);
  for (size_t i = 0; i < n; ++i) cls[i] = isHebrew(in[i]) ? 'R' : isStrongL(in[i]) ? 'L' : 'N';
  for (size_t i = 0; i < n; ++i) {
    if (cls[i] != 'N') continue;
    char prev = 'R', next = 'R';
    for (int k = (int)i - 1; k >= 0; --k) if (cls[k] != 'N') { prev = cls[k]; break; }
    for (size_t k = i + 1; k < n; ++k)   if (cls[k] != 'N') { next = cls[k]; break; }
    cls[i] = (prev == next) ? prev : 'R';
  }
  // Split into runs, reverse run order, reverse characters inside R runs.
  std::vector<std::pair<size_t, size_t>> runs;
  for (size_t i = 0; i < n;) {
    size_t j = i;
    while (j < n && cls[j] == cls[i]) ++j;
    runs.push_back({i, j});
    i = j;
  }
  std::vector<uint32_t> out;
  out.reserve(n);
  for (int r = (int)runs.size() - 1; r >= 0; --r) {
    size_t a = runs[r].first, b = runs[r].second;
    if (cls[a] == 'R') for (size_t k = b; k-- > a;) out.push_back(mirror(in[k]));
    else               for (size_t k = a; k < b; ++k) out.push_back(in[k]);
  }
  return out;
}

// --------------------------------------------------------------- layout ----
namespace {

struct LineMetrics {
  std::vector<uint32_t> visual;
  std::vector<int> penX;     // pen position of each visual glyph
  int inkL = 0, inkR = 0;    // ink extent relative to pen origin 0
  int inkTop = 0, inkBot = 0;// ink extent relative to line-box top
  bool empty = true;
  int inkW() const { return empty ? 0 : inkR - inkL; }
};

LineMetrics measure(const Font& f, const std::vector<uint32_t>& logical) {
  LineMetrics m;
  m.visual = bidiVisual(logical);
  int pen = 0;
  for (uint32_t cp : m.visual) {
    const Glyph* g = fontFind(f, cp);
    m.penX.push_back(pen);
    if (g && g->w) {
      int l = pen + g->xOff, r = pen + g->xOff + g->w;
      int t = g->yOff, b = g->yOff + g->h;
      if (m.empty) { m.inkL = l; m.inkR = r; m.inkTop = t; m.inkBot = b; m.empty = false; }
      else {
        if (l < m.inkL) m.inkL = l;
        if (r > m.inkR) m.inkR = r;
        if (t < m.inkTop) m.inkTop = t;
        if (b > m.inkBot) m.inkBot = b;
      }
    }
    if (g) pen += g->adv;
  }
  return m;
}

std::vector<uint32_t> join(const std::vector<std::vector<uint32_t>>& words, size_t a, size_t b) {
  std::vector<uint32_t> out;
  for (size_t i = a; i < b; ++i) {
    if (i > a) out.push_back(' ');
    out.insert(out.end(), words[i].begin(), words[i].end());
  }
  return out;
}

// Half-width of the circle at a vertical band [y0,y1] (relative to centre).
int chordHalf(int r, int y0, int y1) {
  int dy = (abs(y0) > abs(y1)) ? abs(y0) : abs(y1);
  if (dy >= r) return 0;
  return (int)floor(sqrt((double)r * r - (double)dy * dy));
}

// Places a given set of lines (logical order) inside the circle with ink
// centring and checks every line against the exact chord at its own height.
// worstFill = widest line's ink width / chord width there (1.0 = touching).
bool placeLines(const Font& f, const std::vector<std::vector<uint32_t>>& lines,
                int cx, int cy, int r, TextLayout& out, float& worstFill) {
  const int LH = f.lineHeight;
  const int m = lines.size();
  std::vector<LineMetrics> mets;
  int inkTop = 1 << 30, inkBot = -(1 << 30);
  for (int i = 0; i < m; ++i) {
    mets.push_back(measure(f, lines[i]));
    if (!mets[i].empty) {
      if (i * LH + mets[i].inkTop < inkTop) inkTop = i * LH + mets[i].inkTop;
      if (i * LH + mets[i].inkBot > inkBot) inkBot = i * LH + mets[i].inkBot;
    }
  }
  if (inkTop > inkBot) return false;
  const int top = cy - (inkTop + inkBot) / 2;   // block ink centred on cy
  out.glyphs.clear();
  worstFill = 0;
  for (int i = 0; i < m; ++i) {
    const LineMetrics& lm = mets[i];
    if (lm.empty) continue;
    const int lineTop = top + i * LH;
    const int half = chordHalf(r, lineTop + lm.inkTop - cy, lineTop + lm.inkBot - cy);
    if (half <= 0 || lm.inkW() > 2 * half) return false;
    const float fill = (float)lm.inkW() / (2.0f * half);
    if (fill > worstFill) worstFill = fill;
    const int originX = cx - lm.inkW() / 2 - lm.inkL;
    for (size_t k = 0; k < lm.visual.size(); ++k) {
      const Glyph* g = fontFind(f, lm.visual[k]);
      if (!g || !g->w) continue;
      out.glyphs.push_back({&f, g, originX + lm.penX[k] + g->xOff, lineTop + g->yOff});
    }
  }
  out.font = &f;
  out.lines = m;
  return true;
}

// Every way of breaking `words` into at most maxN lines (2^(k-1) patterns for
// k words; k is capped so this stays cheap). Returns the best fitting one for
// this font: fewest lines, then the most balanced (smallest worst fill).
// forced[i] = the user put a line break after word i (always honoured).
bool bestForFont(const Font& f, const std::vector<std::vector<uint32_t>>& words,
                 const std::vector<bool>& forced,
                 int maxN, int cx, int cy, int r, TextLayout& out) {
  const int k = words.size();
  const int MAX_ENUM_WORDS = 14;
  bool found = false;
  uint32_t forcedMask = 0;
  int minLines = 1;
  for (int i = 0; i < k - 1; ++i) if (forced[i]) { forcedMask |= 1u << i; ++minLines; }
  if (minLines > maxN) return false;

  // Cheap upper bound on each word's width (advances) so most break patterns
  // are rejected with integer sums before the exact placement runs.
  std::vector<int> wordAdv(k);
  for (int i = 0; i < k; ++i) {
    int a = 0;
    for (uint32_t cp : words[i]) { const Glyph* g = fontFind(f, cp); if (g) a += g->adv; }
    wordAdv[i] = a;
  }
  const Glyph* sp = fontFind(f, ' ');
  const int spaceAdv = sp ? sp->adv : f.size / 3;
  const int maxLineW = 2 * r;
  int bestLines = 0;
  float bestFill = 0;
  TextLayout cand;

  auto consider = [&](const std::vector<std::vector<uint32_t>>& lines) {
    float fill;
    if (!placeLines(f, lines, cx, cy, r, cand, fill)) return;
    const int n = lines.size();
    if (!found || n < bestLines || (n == bestLines && fill < bestFill)) {
      found = true; bestLines = n; bestFill = fill; out = cand;
    }
  };

  if (k <= MAX_ENUM_WORDS) {
    const uint32_t patterns = 1u << (k - 1);
    for (uint32_t pat = 0; pat < patterns; ++pat) {
      if ((pat & forcedMask) != forcedMask) continue;   // user breaks are mandatory
      int n = 1;
      for (int b = 0; b < k - 1; ++b) if (pat & (1u << b)) ++n;
      if (n > maxN) continue;
      // Prune: any line whose advance sum exceeds the diameter cannot fit.
      // (Advance sums are a safe bound up to a glyph's side bearing, so the
      // exact check below still decides.)
      bool tooWide = false;
      int lw = 0;
      for (int i = 0; i < k; ++i) {
        lw += wordAdv[i];
        const bool brk = (i < k - 1) && (pat & (1u << i));
        if (brk || i == k - 1) { if (lw > maxLineW + 4) { tooWide = true; break; } lw = 0; }
        else lw += spaceAdv;
      }
      if (tooWide) continue;
      std::vector<std::vector<uint32_t>> lines;
      size_t a = 0;
      for (int b = 0; b < k - 1; ++b)
        if (pat & (1u << b)) { lines.push_back(join(words, a, b + 1)); a = b + 1; }
      lines.push_back(join(words, a, k));
      consider(lines);
    }
  } else {
    // Greedy fallback for very long texts: fill each line to the chord at
    // its estimated height.
    for (int n = 1; n <= maxN; ++n) {
      const int LH = f.lineHeight, blockTop = cy - (n * LH) / 2;
      std::vector<std::vector<uint32_t>> lines;
      size_t w = 0;
      bool bad = false;
      while (w < words.size()) {
        if ((int)lines.size() >= n) { bad = true; break; }
        const int li = lines.size();
        const int limit = 2 * chordHalf(r, blockTop + li * LH + LH / 4 - cy, blockTop + (li + 1) * LH - LH / 4 - cy);
        size_t end = w + 1;
        while (end < words.size() && !forced[end - 1] &&
               measure(f, join(words, w, end + 1)).inkW() <= limit) ++end;
        lines.push_back(join(words, w, end));
        w = end;
      }
      if (!bad) consider(lines);
    }
  }
  return found;
}

}  // namespace

TextLayout layoutInCircle(const std::string& utf8, int cx, int cy, int rInner,
                          const Font* const* fonts, int nFonts, int maxLines,
                          int maxFontSize) {
  TextLayout L;
  std::vector<uint32_t> cps;
  for (uint32_t cp : utf8Decode(utf8)) {
    if (isNikud(cp) || cp == 0x200E || cp == 0x200F || cp == 0xFEFF) continue;  // strip marks
    if (cp == '\t' || cp == 0x00A0) cp = ' ';
    if (cp == '\r') continue;
    if (cp < 0x20 && cp != '\n') continue;
    cps.push_back(cp);
  }
  // Split into words on spaces; a newline ends the word and forces a line
  // break after it (several newlines in a row count as one).
  std::vector<std::vector<uint32_t>> words;
  std::vector<bool> forced;
  std::vector<uint32_t> cur;
  auto flush = [&] { if (!cur.empty()) { words.push_back(cur); forced.push_back(false); cur.clear(); } };
  for (uint32_t cp : cps) {
    if (cp == ' ') flush();
    else if (cp == '\n') { flush(); if (!words.empty()) forced.back() = true; }
    else cur.push_back(cp);
  }
  flush();
  if (words.empty()) { L.error = "empty"; return L; }
  forced.back() = false;
  if (nFonts <= 0) { L.error = "no fonts"; return L; }
  for (auto& wd : words)
    for (uint32_t cp : wd)
      if (!fontFind(*fonts[0], cp)) { L.error = "unsupported character: " + utf8Encode(cp); return L; }

  for (int fi = 0; fi < nFonts; ++fi) {
    const Font& f = *fonts[fi];
    if (maxFontSize > 0 && f.size > maxFontSize) continue;
    int maxN = (2 * rInner) / f.lineHeight;
    if (maxN > maxLines) maxN = maxLines;
    if (bestForFont(f, words, forced, maxN, cx, cy, rInner, L)) return L;
  }
  L.error = "too long to fit in the circle";
  return L;
}

void drawLayout(const TextLayout& L, Canvas& c, Rgb2 colour) {
  for (const PlacedGlyph& p : L.glyphs)
    for (int y = 0; y < p.g->h; ++y)
      for (int x = 0; x < p.g->w; ++x)
        if (glyphBit(*p.font, *p.g, x, y)) c.set(p.x + x, p.y + y, colour);
}

void drawLayoutRow(const TextLayout& L, int y, Rgb2* row, int x0, int n, Rgb2 colour) {
  for (const PlacedGlyph& p : L.glyphs) {
    const int gy = y - p.y;
    if (gy < 0 || gy >= p.g->h) continue;
    for (int x = 0; x < p.g->w; ++x) {
      const int i = p.x + x - x0;
      if (i >= 0 && i < n && glyphBit(*p.font, *p.g, x, gy)) row[i] = colour;
    }
  }
}
