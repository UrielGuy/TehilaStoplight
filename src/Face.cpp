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

static const char* const FX_IDS[] = {"", "fx:quarters", "fx:pulse", "fx:wave", "fx:clock", "fx:ecg", "fx:heart",
                                     "fx:ripples", "fx:eq", "fx:chase", "fx:ball", "fx:sparkle", "fx:fillv", "fx:fillx", "fx:ecgsweep"};
uint8_t effectById(const std::string& id) {
  if (id.compare(0, 3, "fx:") != 0) return FX_NONE;
  for (uint8_t i = 1; i < sizeof(FX_IDS) / sizeof(FX_IDS[0]); ++i) if (id == FX_IDS[i]) return i;
  return FX_NONE;
}

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

// A quarter sine wave, SIN_Q[i] = 1024 * sin(i * 90 / 64 degrees): sin256()
// takes 256 steps per turn. Integer, so the page and the Python port (which
// carry the same table) draw the same pixels.
static const int16_t SIN_Q[65] = {
  0, 25, 50, 75, 100, 125, 150, 175, 200, 224, 249, 273, 297, 321, 345, 369, 392, 415, 438, 460, 483, 505,
  526, 548, 569, 590, 610, 630, 650, 669, 688, 706, 724, 742, 759, 775, 792, 807, 822, 837, 851, 865, 878,
  891, 903, 915, 926, 936, 946, 955, 964, 972, 980, 987, 993, 999, 1004, 1009, 1013, 1016, 1019, 1021, 1023,
  1024, 1024};
static int sin256(int n) {
  n &= 255;
  const int i = n & 63;
  switch (n >> 6) {
    case 0: return SIN_Q[i];
    case 1: return SIN_Q[64 - i];
    case 2: return -SIN_Q[i];
    default: return -SIN_Q[64 - i];
  }
}
static int roundDiv1024(int v) { return v < 0 ? -((-v + 512) >> 10) : (v + 512) >> 10; }   // halves away from 0

// FX_WAVE: a line across the circle bent into one sine period. Its amplitude
// in px (+ = left half up, right half down) is full on every beat, flat in
// the middle of it and swings through to the other side (cos over the beat);
// the sign flips every beat, so beat 1, 3, 5... start left up and the swing
// stays continuous over the beat boundaries.
constexpr int WAVE_AMP = 26;                      // full amplitude, px (~40% of the radius)
static const int8_t WAVE_BRUSH[3] = {6, 5, 4};    // brush: half height in doubled units 0, 1, 2 columns off (6 px line)
static int waveAmp(const Beat& b) {
  if (!b.bpm) return 0;
  const int c = sin256(64 + (int)((uint64_t)b.phase() * 128 / 60000));   // cos(pi * phase), 128 steps a beat
  const int a = (WAVE_AMP * (c < 0 ? -c : c) + 512) >> 10;
  return ((c < 0) != ((b.steps() & 1) != 0)) ? -a : a;
}

// FX_CLOCK: a watch face. It shows 12:00 when the frame starts and gains 5
// minutes on every beat: the minute hand jumps 30 degrees, the hour hand 2.5
// (12 beats an hour). Each jump eases out over the first eighth of the beat,
// like a quartz tick. Angles in half degrees clockwise from 12 o'clock.
constexpr uint32_t CLK_TICK = 7500;   // the tick's length, in beat phase (60000 a beat)
static void clockHands(const Beat& b, int& hour, int& minute) {
  const uint32_t n = b.steps();
  hour = (int)(n % 144) * 5;
  minute = (int)(n % 12) * 60;
  if (n && b.phase() < CLK_TICK) {   // still on its way from the last position
    const uint64_t rem = CLK_TICK - b.phase(), q = (uint64_t)CLK_TICK * CLK_TICK;
    hour = (hour + 720 - (int)((5 * rem * rem + q / 2) / q)) % 720;
    minute = (minute + 720 - (int)((60 * rem * rem + q / 2) / q)) % 720;
  }
}
// sin() of a half-degree angle, 1024 = 1: SIN_Q interpolated.
static int roundDiv(int64_t v, int64_t d) { return (int)(v < 0 ? -((-v + d / 2) / d) : (v + d / 2) / d); }
static int sin720(int a) {
  const int t = ((a % 720) + 720) % 720 * 256, i = t / 720, s0 = sin256(i);
  return s0 + roundDiv((int64_t)(sin256(i + 1) - s0) * (t % 720), 720);
}

// FX_ECG: a heart monitor trace, one PQRST complex per beat over the 128
// columns, px above the baseline (mid-height); the R spike peaks on columns
// 63 and 64. FX_ECG scrolls it right to left one table length per beat, so
// the spike is at the centre on every beat. FX_ECGSWEEP keeps it still and
// shows ECG_WINDOW columns ending at a leading edge that sweeps left to right
// one circle width per beat (wrapping), on the spike's column on the beat; the
// window's oldest ECG_FADE columns are in the dim ink. bpm 0: all of it.
static const int8_t ECG[128] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 3, 4, 5, 6, 6,
  6, 6, 5, 4, 3, 3, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, -2, -4, -4, -1, 5, 15, 25, 35,
  35, 25, 15, 5, -3, -8, -9, -6, -2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 3, 4, 6, 7, 8, 9, 9, 10, 10, 10, 10,
  9, 9, 8, 7, 6, 4, 3, 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr int ECG_THICK = 3;   // doubled units around the trace's runs: 4 px on the flat, 3 px wide on the spike
constexpr int ECG_WINDOW = 64, ECG_FADE = 16;
static int ecgScroll(const Beat& b) { return b.bpm ? (int)((uint64_t)b.phase() * 128 / 60000) : 0; }   // px moved since the beat

static bool timeFx(uint8_t e);
static uint32_t rowFxState(const LightSpec& s, const Beat& b);

uint32_t beatState(const LightSpec& s, bool lit, const Beat& b) {
  if (!lit || (!b.bpm && !timeFx(s.effect))) return 0;
  uint32_t k = (s.hueBg || s.hueFg || s.effect == FX_QUARTERS) ? b.steps() + 1 : 0;
  if (s.effect == FX_PULSE) k = k * 256 + (uint32_t)pulseDiameter(b);
  if (s.effect == FX_WAVE) k = k * 256 + (uint32_t)(waveAmp(b) + 128);
  if (s.effect == FX_CLOCK) { int h, m; clockHands(b, h, m); k = k * 518400u + (uint32_t)(h * 720 + m); }
  if (s.effect == FX_ECG || s.effect == FX_ECGSWEEP) k = k * 256 + (uint32_t)ecgScroll(b);
  if (s.effect >= FX_HEART) k = k * 0x9e3779b1u + rowFxState(s, b);
  return k;
}

// Which twelfth of the circle a pixel lies in, 0..11 clockwise from 12
// o'clock (doubled units: dx and dy are odd and never 0, and the 30 degree
// boundaries are irrational, so no pixel lies on one).
static int sector(int dx, int dy) {
  const int ax2 = dx * dx, ay2 = dy * dy;
  const int fromV = 3 * ax2 < ay2 ? 0 : ax2 < 3 * ay2 ? 1 : 2;   // NE / SW: 30 degree steps from the vertical
  const int fromH = 3 * ay2 < ax2 ? 0 : ay2 < 3 * ax2 ? 1 : 2;   // SE / NW: from the horizontal
  if (dx > 0) return dy < 0 ? fromV : 3 + fromH;
  return dy > 0 ? 6 + fromV : 9 + fromH;
}

// FX_CLOCK geometry in doubled units (the circle's radius is 128): a bezel
// ring, hour marks as radial bars (12, 3, 6 and 9 longer and wider), hands
// tapering from the centre (half width W0) to a round tip (W1), optionally
// with a round-ended tail behind the centre (TAIL < 0; none now), a hub with
// a pin in the face colour.
constexpr int CLK_BEZEL = 122;                                         // ink from here to the edge
constexpr int CLK_MARK_OUT = 114, CLK_MARK_IN = 98, CLK_MARK_W = 3;    // marks 1, 2, 4, 5, 7...
constexpr int CLK_MAIN_IN = 84, CLK_MAIN_W = 5;                        // marks 12, 3, 6, 9
constexpr int CLK_HOUR_TAIL = 0, CLK_HOUR_LEN = 64, CLK_HOUR_W0 = 9, CLK_HOUR_W1 = 5;
constexpr int CLK_MIN_TAIL = 0, CLK_MIN_LEN = 102, CLK_MIN_W0 = 7, CLK_MIN_W1 = 4;
constexpr int CLK_HUB = 14, CLK_PIN = 4;
struct ClockFace { int ux[12], uy[12], hx, hy, mx, my; };   // directions (1024 = 1) of the marks and the hands

// A straight mark along direction (ux, uy) from radius `from` to `to`, half width w.
static bool onMark(int dx, int dy, int ux, int uy, int from, int to, int w) {
  const int64_t along = (int64_t)dx * ux + (int64_t)dy * uy, across = (int64_t)dx * uy - (int64_t)dy * ux;
  return along >= ((int64_t)from << 10) && along <= ((int64_t)to << 10) && across * across <= ((int64_t)w * w << 20);
}
// A hand pointing along (ux, uy), see above.
static bool onHand(int dx, int dy, int ux, int uy, int tail, int len, int w0, int w1) {
  const int64_t along = (int64_t)dx * ux + (int64_t)dy * uy, across = (int64_t)dx * uy - (int64_t)dy * ux;
  const int64_t ac = across < 0 ? -across : across, t = (int64_t)tail << 10, l = (int64_t)len << 10;
  if (along < t) return (along - t) * (along - t) + across * across <= ((int64_t)w0 * w0 << 20);
  if (along <= 0) return ac <= ((int64_t)w0 << 10);
  if (along <= l) return ac * len <= ((int64_t)w0 * len << 10) + (int64_t)(w1 - w0) * along;
  return (along - l) * (along - l) + across * across <= ((int64_t)w1 * w1 << 20);
}

static bool clockInk(const ClockFace& f, int dx, int dy, int d2) {
  if (d2 >= CLK_BEZEL * CLK_BEZEL) return true;
  if (d2 <= CLK_HUB * CLK_HUB) return d2 > CLK_PIN * CLK_PIN;
  if (d2 >= CLK_MAIN_IN * CLK_MAIN_IN && d2 <= CLK_MARK_OUT * CLK_MARK_OUT) {
    const int d = sector(dx, dy);   // the marks at both ends of this twelfth
    for (int k = d; k <= d + 1; ++k) {
      const int m = k % 12, main = m % 3 == 0;
      if (onMark(dx, dy, f.ux[m], f.uy[m], main ? CLK_MAIN_IN : CLK_MARK_IN, CLK_MARK_OUT, main ? CLK_MAIN_W : CLK_MARK_W)) return true;
    }
  }
  const int reach = CLK_HOUR_LEN + CLK_HOUR_W0 + 2;   // beyond this the hour hand cannot be (saves time on the LEDs)
  return onHand(dx, dy, f.mx, f.my, CLK_MIN_TAIL, CLK_MIN_LEN, CLK_MIN_W0, CLK_MIN_W1) ||
         (d2 <= reach * reach && onHand(dx, dy, f.hx, f.hy, CLK_HOUR_TAIL, CLK_HOUR_LEN, CLK_HOUR_W0, CLK_HOUR_W1));
}

// ---------------------------------------------------------------------------
// Beat animations painted a whole row at a time (FX_HEART and later): each
// one prepares its state once per draw (RowFx) and then paints rows from it.
// All integer, mirrored pixel for pixel by the page (paintBeat in WebPage.h).

static uint32_t mix32(uint32_t x) {   // integer hash (lowbias32)
  x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
  return x;
}

// FX_HEART: a square standing on its corner (half diagonal H, centre C below
// the middle) with two discs on its upper sides, doubled units at full size.
// Scale z/256: full on the beat, easing back to 80% like the pulse.
constexpr int HEART_H = 94, HEART_C = 10, HEART_LX = 47, HEART_LY = HEART_C - 47, HEART_R2 = HEART_H * HEART_H / 2;
static int heartScale(const Beat& b) {
  if (!b.bpm) return 205;
  const uint64_t rem = 60000 - b.phase();
  return 205 + (int)((51 * rem * rem + 1800000000ull) / 3600000000ull);
}

// FX_RIPPLES: a ring starts at the centre on every beat and reaches the rim
// in 2.5 beats (RIPPLE_LIFE, 1/256 beats; speed easing from 1.5x to 0.5x the
// average), thinning from 5 to 3 px; the
// last stretch in the dim ink. bpm 0 shows two rings standing still.
constexpr int RIPPLE_LIFE = 640;

// FX_EQ: 7 bars of LED segments in fixed rainbow colours on black (the rim in
// the circle colour). On every beat each bar jumps to a height from a hash of
// the beat and the bar, then sinks back to one segment; a peak segment stays
// at the jump height. Hue jumps turn the bar colours with the circle colour.
constexpr int EQ_BARS = 7, EQ_X0 = 10, EQ_PITCH = 16, EQ_W = 12;   // bars: columns EQ_X0 + 16 b .. +11
constexpr int EQ_FLOOR = 108, EQ_SEG = 8, EQ_SEGH = 6, EQ_SEGS = 12, EQ_RIM = 4;   // segment s: rows 107 - 8 s - 5 .. 107 - 8 s
static const Rgb2 EQ_COLOURS[EQ_BARS] = {rgb2(3, 0, 0), rgb2(3, 2, 0), rgb2(3, 3, 0), rgb2(0, 3, 0), rgb2(0, 3, 3), rgb2(0, 0, 3), rgb2(3, 0, 3)};

// FX_CHASE: 12 bulbs around the rim (centres at radius CHASE_R, doubled), one
// more lit per beat clockwise from 12 o'clock, then all off for a beat.
constexpr int CHASE_R = 100, CHASE_BULB = 16;

// FX_BALL: a ball (radius BALL_R) bouncing on a floor line. It is on the
// floor within BALL_SQ_PHASE of the beat, squashed (wider, flatter) by up to
// BALL_SQUASH right on the beat, and flies a parabola up to BALL_HIGH (mid-
// beat) in between. Doubled units.
constexpr int BALL_R = 22, BALL_FLOOR = 97, BALL_HIGH = 128, BALL_SQUASH = 6, BALL_SQ_PHASE = 3000;

// FX_SPARKLE: SPARKS twinkles on the circle colour, not tied to the beat:
// slot i comes back every P ms (560..1000) at a new place and lives 320..760
// ms of it: a dim cross, a cross with dim arms, the full 4-point star (or a
// round blob), a dim cross again. Each in a colour of SPARK_COLOURS (never
// the circle colour; the ink's hue jumps turn them); every light has its own
// pattern. 40 ms steps.
constexpr int SPARKS = 20, SPARK_MS = 40, SPARK_REACH = 6;
static const Rgb2 SPARK_COLOURS[7] = {rgb2(3, 3, 3), rgb2(3, 3, 0), rgb2(0, 3, 3), rgb2(3, 0, 3), rgb2(3, 2, 0), rgb2(0, 3, 0), rgb2(2, 3, 3)};

// FX_FILLV / FX_FILLX: 2 px lines in a shuffled order. With N beat-grid
// moments in [frame start, frame length - 100 ms] (a beat right on the frame
// start counts), the k-th of them shows ceil(L k / N) of the L lines at once,
// so the last one completes the fill (with more beats than lines: 1 + (L - 1)
// (k - 1) / (N - 1), one line or none per beat, the last on the last beat);
// with no beat in there everything appears at length - 100 ms. FILLV: 64 vertical lines, FILLX: 64 vertical
// and 64 horizontal ones mixed.
constexpr int FILL_LEAD_MS = 100;

static bool rowFx(uint8_t e) { return e >= FX_HEART && e <= FX_FILLX; }
static bool timeFx(uint8_t e) { return e == FX_SPARKLE || e == FX_FILLV || e == FX_FILLX; }   // move without a beat too

struct Spark { int16_t x, y; uint8_t stage, star; Rgb2 col, dim; };
struct RowFx {
  uint8_t e = FX_NONE;
  Rgb2 base = 0, ink = 0, dim = 0;
  int z = 256;                                          // heart
  int rings = 0, ringLo2[3], ringHi2[3]; Rgb2 ringCol[3];   // ripples
  int8_t colBar[DIAMETER]; uint8_t eqH[EQ_BARS], eqPeak[EQ_BARS]; Rgb2 eqCol[EQ_BARS];   // equaliser
  int bx[12], by[12], lit = 0;                          // chase
  int cy = 0, rx = BALL_R, ry = BALL_R;                 // ball
  int sparks = 0; Spark sp[SPARKS];                     // sparkle
  uint8_t colOn[64], rowOn[64];                         // fills: 2 px strips shown
  uint32_t key = 0;                                     // beatState
};

// Beats on the grid at frame time 0..t ms inclusive (a beat happens at the
// first whole ms of its beat; the grid's own start is beat 0).
static int64_t beatsUpTo(const Beat& b, uint64_t start, int64_t t) {
  const int64_t before = start ? (int64_t)((start - 1) * b.bpm / 60000) : -1;
  return (int64_t)((start + (uint64_t)t) * b.bpm / 60000) - before;
}
static int fillShown(const Beat& b, int n) {
  const int64_t end = (int64_t)b.frameLen - FILL_LEAD_MS;
  if (!b.bpm || end < 0) return (int64_t)b.frameMs >= end ? n : 0;
  const uint64_t start = b.gridMs >= b.frameMs ? b.gridMs - b.frameMs : 0;
  const int64_t beats = beatsUpTo(b, start, end);   // the beats that fill it
  if (!beats) return (int64_t)b.frameMs >= end ? n : 0;
  int64_t k = beatsUpTo(b, start, b.frameMs);
  if (k > beats) k = beats;
  if (beats <= n) return (int)((k * n + beats - 1) / beats);
  return k ? (int)(1 + (k - 1) * (n - 1) / (beats - 1)) : 0;
}

static void rowFxPrepare(RowFx& f, const LightSpec& sp, const Beat& b, Rgb2 base, Rgb2 ink, int light) {
  f.e = sp.effect; f.base = base; f.ink = ink;
  f.dim = rgb2Dim(ink);
  const uint32_t n = b.steps();
  switch (f.e) {
    case FX_HEART:
      f.z = heartScale(b);
      f.key = (uint32_t)f.z;
      break;
    case FX_RIPPLES: {
      const int pq = b.bpm ? (int)((uint64_t)b.phase() * 256 / 60000) : 128, st = b.bpm ? (int)(n < 2 ? n : 2) : 2;
      for (int j = 0; j <= st; ++j) {
        const int a = j * 256 + pq;
        if (a >= RIPPLE_LIFE) continue;
        const int r = 128 * a * (3 * RIPPLE_LIFE - a) / (2 * RIPPLE_LIFE * RIPPLE_LIFE), w = 5 - 3 * a / RIPPLE_LIFE, lo = r > w ? r - w : 0;
        f.ringLo2[f.rings] = lo * lo; f.ringHi2[f.rings] = (r + w) * (r + w);
        f.ringCol[f.rings] = a >= 448 && f.dim != base ? f.dim : ink;
        ++f.rings;
      }
      f.key = (uint32_t)(st * 256 + pq);
      break;
    }
    case FX_EQ: {
      const uint64_t rem = b.bpm ? 60000 - b.phase() : 60000;
      const int deg = hueDeg(n, sp.hueBg);
      f.key = n + 1;
      for (int i = 0; i < EQ_BARS; ++i) {
        const int peak = 3 + (int)(mix32(n * EQ_BARS + i + 0x5eed) % 10);   // 3..12 segments
        f.eqPeak[i] = (uint8_t)peak;
        f.eqH[i] = (uint8_t)(1 + (int)(((uint64_t)(peak - 1) * rem * rem + 1800000000ull) / 3600000000ull));
        f.eqCol[i] = deg ? rgb2Hue(EQ_COLOURS[i], deg) : EQ_COLOURS[i];
        f.key = f.key * 16 + f.eqH[i];
      }
      for (int x = 0; x < DIAMETER; ++x) {
        const int t = x - EQ_X0;
        f.colBar[x] = (int8_t)(t >= 0 && t < EQ_BARS * EQ_PITCH && t % EQ_PITCH < EQ_W ? t / EQ_PITCH : -1);
      }
      break;
    }
    case FX_CHASE:
      for (int k = 0; k < 12; ++k) {
        f.bx[k] = roundDiv((int64_t)CHASE_R * sin720(k * 60), 1024);
        f.by[k] = -roundDiv((int64_t)CHASE_R * sin720(k * 60 + 180), 1024);
      }
      f.lit = (int)((n + 1) % 13);
      if (f.dim == ink) f.dim = rgb2Dim(base);   // ink with no bright level: unlit bulbs in the dark circle colour
      f.key = (uint32_t)f.lit;
      break;
    case FX_BALL: {
      const uint32_t u = b.bpm ? b.phase() : 0, d = u < 60000 - u ? u : 60000 - u;
      const uint64_t span = 60000 - 2 * BALL_SQ_PHASE;   // the flight
      const int h = d < BALL_SQ_PHASE ? 0 : (int)((uint64_t)4 * BALL_HIGH * (u - BALL_SQ_PHASE) * (60000 - BALL_SQ_PHASE - u) / (span * span));
      const int sq = b.bpm && d < BALL_SQ_PHASE ? (int)((BALL_SQUASH * (BALL_SQ_PHASE - d) + BALL_SQ_PHASE / 2) / BALL_SQ_PHASE) : 0;
      f.rx = BALL_R + sq; f.ry = BALL_R - sq;
      f.cy = BALL_FLOOR - f.ry - h;
      f.key = (uint32_t)((f.cy + 512) * 16 + sq);
      break;
    }
    case FX_SPARKLE: {
      const uint32_t t = b.frameMs / SPARK_MS, seed = (uint32_t)light * 0x9e3779b9u;
      const int deg = hueDeg(n, sp.hueFg);
      f.key = t * 360 + (uint32_t)deg + 1;   // redrawn every 40 ms step (the pattern differs per light)
      for (int i = 0; i < SPARKS; ++i) {
        const uint32_t period = 14 + mix32(i * 977u + 1 + seed) % 12, tt = t + mix32(i * 131u + 7 + seed) % period, g = tt / period, tau = tt % period;
        const uint32_t h = mix32(i * 7919u + g * 104729u + 12345u + seed), h2 = mix32(h);
        uint32_t life = 8 + h % 12;
        if (life > period - 2) life = period - 2;
        if (tau >= life) continue;
        const int a = (int)((h >> 4) % 720), u1 = (int)(h2 & 1023), u2 = (int)((h2 >> 10) & 1023), rho = 54 * (u1 > u2 ? u1 : u2) / 1023;
        Spark& s = f.sp[f.sparks++];
        s.x = (int16_t)(63 + roundDiv((int64_t)rho * sin720(a), 1024));
        s.y = (int16_t)(63 - roundDiv((int64_t)rho * sin720(a + 180), 1024));
        s.stage = (uint8_t)(tau * 4 / life);
        s.star = (uint8_t)((h2 >> 20) & 1);
        int ci = (int)((h2 >> 21) % 7);
        Rgb2 col = deg ? rgb2Hue(SPARK_COLOURS[ci], deg) : SPARK_COLOURS[ci];
        while (col == base) { ci = (ci + 1) % 7; col = deg ? rgb2Hue(SPARK_COLOURS[ci], deg) : SPARK_COLOURS[ci]; }
        s.col = col;
        s.dim = rgb2Dim(col) != base ? rgb2Dim(col) : col;
      }
      break;
    }
    case FX_FILLV:
    case FX_FILLX: {
      const int lines = f.e == FX_FILLV ? 64 : 128, shown = fillShown(b, lines);
      uint8_t order[128];   // a fixed shuffle (Fisher-Yates over mix32)
      for (int i = 0; i < lines; ++i) order[i] = (uint8_t)i;
      for (int i = lines - 1; i > 0; --i) {
        const int j = (int)(mix32((uint32_t)(lines * 1000 + i)) % (uint32_t)(i + 1));
        const uint8_t t = order[i]; order[i] = order[j]; order[j] = t;
      }
      for (int i = 0; i < 64; ++i) f.colOn[i] = f.rowOn[i] = 0;
      for (int i = 0; i < shown; ++i) { if (order[i] < 64) f.colOn[order[i]] = 1; else f.rowOn[order[i] - 64] = 1; }
      f.key = (uint32_t)shown + 1;
      break;
    }
    default: break;
  }
}

static void rowFxRow(const RowFx& f, int y, Rgb2* row) {
  const int dy = 2 * y - (DIAMETER - 1), dy2 = dy * dy, full2 = DIAMETER * DIAMETER;
  switch (f.e) {
    case FX_HEART: {
      const int64_t z = f.z, py = (int64_t)dy * 256, a = py - HEART_C * z, aa = a < 0 ? -a : a;
      const int64_t ly = py - HEART_LY * z, ly2 = ly * ly, hz = HEART_H * z, lx = HEART_LX * z, r2 = HEART_R2 * z * z;
      const Rgb2 col = f.base != RGB2_BLACK ? f.base : f.ink;   // a black circle shows its heart in the ink
      for (int x = 0; x < DIAMETER; ++x) {
        const int dx = 2 * x - (DIAMETER - 1);
        const int64_t ax = (int64_t)(dx < 0 ? -dx : dx) * 256, ex = ax - lx;
        row[x] = dx * dx + dy2 <= full2 && (ax + aa <= hz || ex * ex + ly2 <= r2) ? col : RGB2_BLACK;
      }
      break;
    }
    case FX_RIPPLES:
      for (int x = 0; x < DIAMETER; ++x) {
        const int dx = 2 * x - (DIAMETER - 1), d2 = dx * dx + dy2;
        Rgb2 v = d2 <= full2 ? f.base : RGB2_BLACK;
        for (int j = 0; j < f.rings; ++j) if (d2 >= f.ringLo2[j] && d2 <= f.ringHi2[j]) { v = f.ringCol[j]; break; }
        row[x] = d2 <= full2 ? v : RGB2_BLACK;
      }
      break;
    case FX_EQ: {
      const int u = EQ_FLOOR - 1 - y, seg = u >= 0 && u % EQ_SEG < EQ_SEGH && u / EQ_SEG < EQ_SEGS ? u / EQ_SEG : -1;
      const int rim2 = (DIAMETER - 2 * EQ_RIM) * (DIAMETER - 2 * EQ_RIM);
      for (int x = 0; x < DIAMETER; ++x) {
        const int dx = 2 * x - (DIAMETER - 1), d2 = dx * dx + dy2, bar = f.colBar[x];
        Rgb2 v = RGB2_BLACK;
        if (d2 > full2) v = RGB2_BLACK;
        else if (d2 >= rim2) v = f.base;
        else if (bar >= 0 && seg >= 0 && (seg < f.eqH[bar] || seg == f.eqPeak[bar] - 1)) v = f.eqCol[bar];
        row[x] = v;
      }
      break;
    }
    case FX_CHASE: {
      const int lo = CHASE_R - CHASE_BULB, hi = CHASE_R + CHASE_BULB;
      for (int x = 0; x < DIAMETER; ++x) {
        const int dx = 2 * x - (DIAMETER - 1), d2 = dx * dx + dy2;
        Rgb2 v = d2 <= full2 ? f.base : RGB2_BLACK;
        if (d2 >= lo * lo && d2 <= hi * hi) {
          const int d = sector(dx, dy);   // the bulbs at both ends of this twelfth
          for (int k = d; k <= d + 1; ++k) {
            const int m = k % 12, ex = dx - f.bx[m], ey = dy - f.by[m];
            if (ex * ex + ey * ey <= CHASE_BULB * CHASE_BULB) { v = m < f.lit ? f.ink : f.dim; break; }
          }
        }
        row[x] = v;
      }
      break;
    }
    case FX_BALL: {
      const int ey = dy - f.cy, rx2 = f.rx * f.rx, ry2 = f.ry * f.ry, lim = rx2 * ry2, eyTerm = ey * ey * rx2;
      const bool floor = dy >= BALL_FLOOR && dy <= BALL_FLOOR + 2;
      for (int x = 0; x < DIAMETER; ++x) {
        const int dx = 2 * x - (DIAMETER - 1), d2 = dx * dx + dy2;
        row[x] = d2 > full2 ? RGB2_BLACK : floor || dx * dx * ry2 + eyTerm <= lim ? f.ink : f.base;
      }
      break;
    }
    case FX_SPARKLE:
      for (int x = 0; x < DIAMETER; ++x) {
        const int dx = 2 * x - (DIAMETER - 1);
        row[x] = dx * dx + dy2 <= full2 ? f.base : RGB2_BLACK;
      }
      for (int i = 0; i < f.sparks; ++i) {   // in slot order: a later one covers an earlier one
        const Spark& s = f.sp[i];
        const int oy = y - s.y, ay = oy < 0 ? -oy : oy;
        if (ay > SPARK_REACH) continue;
        for (int ox = -SPARK_REACH; ox <= SPARK_REACH; ++ox) {
          const int ax = ox < 0 ? -ox : ox, x = s.x + ox, axis = ax == 0 || ay == 0, r = ax + ay;
          if (x < 0 || x >= DIAMETER) continue;
          int lvl = 0;   // 0 nothing, 1 dim, 2 full
          if (s.stage == 0 || s.stage == 3) lvl = axis && r <= 1 ? 1 : 0;
          else if (s.stage == 1) lvl = r == 0 ? 2 : axis && r <= 2 ? 1 : 0;
          else if (s.star) lvl = ax <= 1 && ay <= 1 ? 2 : axis ? (r <= 4 ? 2 : r <= 6 ? 1 : 0) : 0;
          else lvl = ax * ax + ay * ay <= 5 ? 2 : ax * ax + ay * ay <= 8 ? 1 : 0;
          if (!lvl) continue;
          const int dx = 2 * x - (DIAMETER - 1);
          if (dx * dx + dy2 <= full2) row[x] = lvl == 2 ? s.col : s.dim;
        }
      }
      break;
    case FX_FILLV:
    case FX_FILLX: {
      const bool rowOn = f.rowOn[y >> 1];
      for (int x = 0; x < DIAMETER; ++x) {
        const int dx = 2 * x - (DIAMETER - 1);
        row[x] = dx * dx + dy2 > full2 ? RGB2_BLACK : rowOn || f.colOn[x >> 1] ? f.ink : f.base;
      }
      break;
    }
    default: break;
  }
}

static uint32_t rowFxState(const LightSpec& s, const Beat& b) {
  RowFx f;
  rowFxPrepare(f, s, b, beatBg(s, b), beatFg(s, b), 0);   // no key depends on the light
  return f.key;
}

// Circle tests in doubled units, (2x-127)^2 + (2y-127)^2 <= D^2: the circle
// is centred on the pixel grid (63.5) and the sums stay integer.
void drawLight(Canvas& c, const LightSpec& sp, int light, bool lit, uint32_t elapsedMs, const Beat& beat) {
  const Rgb2 base = lit ? beatBg(sp, beat) : sp.bg;   // 0 = a black circle (text still shows in its ink)
  Rgb2 ink = lit ? beatFg(sp, beat) : sp.fg;
  if (ink == base) ink = (base == RGB2_BLACK) ? rgb2(3, 3, 3) : RGB2_BLACK;   // never invisible
  if (lit && rowFx(sp.effect)) {
    RowFx f;
    rowFxPrepare(f, sp, beat, base, ink, light);
    Rgb2 row[DIAMETER];
    for (int y = 0; y < DIAMETER; ++y) { rowFxRow(f, y, row); c.setRow(0, light * DIAMETER + y, row, DIAMETER); }
    return;
  }
  Rgb2 twelfth[12];   // see sector()
  bool split = false, wave = false, clock = false;
  ClockFace face = ClockFace();
  int16_t lineLo[DIAMETER], lineHi[DIAMETER];   // FX_WAVE: the line's rows per column (doubled dy, inclusive)
  Rgb2 lineCol[DIAMETER];                         // and its colour there (the circle colour: hidden)
  int dia = DIAMETER;
  if (lit && sp.effect == FX_QUARTERS) {
    const uint32_t n = beat.steps();
    const int deg = hueDeg(n, sp.hueBg);
    const Rgb2 cols[4] = {base, rgb2Hue(sp.quarters[0], deg), rgb2Hue(sp.quarters[1], deg), rgb2Hue(sp.quarters[2], deg)};
    // Every beat the colours turn 30 degrees clockwise: on beat 0 colour 0 fills
    // the top left quarter (twelfths 9-11), on beat 1 twelfths 10, 11 and 0.
    for (int d = 0; d < 12; ++d) twelfth[d] = cols[((d + 15 - (int)(n % 12)) % 12) / 3];
    split = true;
  } else if (lit && sp.effect == FX_PULSE) {
    dia = pulseDiameter(beat);
  } else if (lit && sp.effect == FX_WAVE) {
    // The line's centre per column, doubled units: -2A sin(2 pi (x + 0.5) / 128),
    // two columns past each edge too (the sine just carries on); then a round
    // brush swept along it, kept as one span of rows per column.
    const int a = waveAmp(beat);
    int16_t mid[DIAMETER + 4];
    for (int x = -2; x < DIAMETER + 2; ++x) mid[x + 2] = (int16_t)-roundDiv1024(2 * a * sin256(2 * x + 1));
    for (int x = 0; x < DIAMETER; ++x) {
      int lo = mid[x + 2] - WAVE_BRUSH[0], hi = mid[x + 2] + WAVE_BRUSH[0];
      for (int k = 1; k <= 2; ++k)
        for (int j = x + 2 - k; j <= x + 2 + k; j += 2 * k) {
          if (mid[j] - WAVE_BRUSH[k] < lo) lo = mid[j] - WAVE_BRUSH[k];
          if (mid[j] + WAVE_BRUSH[k] > hi) hi = mid[j] + WAVE_BRUSH[k];
        }
      lineLo[x] = (int16_t)lo; lineHi[x] = (int16_t)hi; lineCol[x] = ink;
    }
    wave = true;
  } else if (lit && (sp.effect == FX_ECG || sp.effect == FX_ECGSWEEP)) {
    // Each column's run goes from its point half way to each neighbour's, so the
    // steep edges stay connected; the line then takes the runs of the columns on
    // either side too, plus ECG_THICK up and down. Doubled units: dy = -2 * table.
    const bool sweep = sp.effect == FX_ECGSWEEP;
    const int s = sweep ? 0 : -ecgScroll(beat), lead = (DIAMETER / 2 + ecgScroll(beat)) & 127;
    Rgb2 dim = rgb2Dim(ink);
    if (dim == base) dim = ink;
    for (int x = 0; x < DIAMETER; ++x) {
      const int age = (lead - x) & 127;   // columns behind the sweep's leading edge
      lineCol[x] = !sweep || !beat.bpm ? ink : age >= ECG_WINDOW ? base : age >= ECG_WINDOW - ECG_FADE ? dim : ink;
    }
    int16_t runLo[DIAMETER + 2], runHi[DIAMETER + 2];
    for (int x = -1; x <= DIAMETER; ++x) {
      const int v = ECG[(x - s) & 127], c = -2 * v, l = -(v + ECG[(x - 1 - s) & 127]), r = -(v + ECG[(x + 1 - s) & 127]);
      runLo[x + 1] = (int16_t)(c < l ? (c < r ? c : r) : (l < r ? l : r));
      runHi[x + 1] = (int16_t)(c > l ? (c > r ? c : r) : (l > r ? l : r));
    }
    for (int x = 0; x < DIAMETER; ++x) {
      int lo = runLo[x], hi = runHi[x];
      for (int j = x + 1; j <= x + 2; ++j) { if (runLo[j] < lo) lo = runLo[j]; if (runHi[j] > hi) hi = runHi[j]; }
      lineLo[x] = (int16_t)(lo - ECG_THICK); lineHi[x] = (int16_t)(hi + ECG_THICK);
    }
    wave = true;   // drawn like the wave's line
  } else if (lit && sp.effect == FX_CLOCK) {
    int h, m;
    clockHands(beat, h, m);
    for (int k = 0; k < 12; ++k) { face.ux[k] = sin720(k * 60); face.uy[k] = -sin720(k * 60 + 180); }
    face.hx = sin720(h); face.hy = -sin720(h + 180);
    face.mx = sin720(m); face.my = -sin720(m + 180);
    clock = true;
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
      if (lit) {
        if (d2 <= r2) v = split ? twelfth[sector(dx, dy)]
                        : wave && dy >= lineLo[x] && dy <= lineHi[x] ? lineCol[x]
                        : clock && clockInk(face, dx, dy, d2) ? ink : base;
      }
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
