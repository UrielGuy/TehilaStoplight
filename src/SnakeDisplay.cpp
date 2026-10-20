#include "SnakeDisplay.h"
#include "PanelPins.h"

SnakeDisplay::~SnakeDisplay() {
  delete _dma;
  _dma = nullptr;
}

bool SnakeDisplay::begin(const Options& opt) {
  if (_dma) return true;

  _colourRotated = opt.colourRotated;
  HUB75_I2S_CFG::i2s_pins pins = {
      PanelPins::r1_pin, PanelPins::g1_pin, PanelPins::b1_pin,
      PanelPins::r2_pin, PanelPins::g2_pin, PanelPins::b2_pin,
      PanelPins::a_pin,  PanelPins::b_pin,  PanelPins::c_pin,
      PanelPins::d_pin,  PanelPins::e_pin,
      PanelPins::lat_pin, PanelPins::oe_pin, PanelPins::clk_pin};

  if (opt.colourRotated) {   // R takes G's pin, G takes B's, B takes R's
    pins.r1 = PanelPins::g1_pin; pins.g1 = PanelPins::b1_pin; pins.b1 = PanelPins::r1_pin;
    pins.r2 = PanelPins::g2_pin; pins.g2 = PanelPins::b2_pin; pins.b2 = PanelPins::r2_pin;
  }
  HUB75_I2S_CFG cfg(PANEL_W, PANEL_H, PANELS, pins);
  cfg.i2sspeed    = opt.clock;
  cfg.double_buff = opt.doubleBuffer;
  cfg.clkphase    = false;
  cfg.min_refresh_rate = opt.minRefreshHz;
  cfg.setPixelColorDepthBits(COLOR_BITS);

  _dma = new MatrixPanel_I2S_DMA(cfg);
  if (!_dma->begin()) {
    log_e("HUB75 DMA begin() failed (out of DMA RAM?)");
    delete _dma;
    _dma = nullptr;
    return false;
  }
  _doubleBuffered = opt.doubleBuffer;
  _dma->setBrightness8(128);
  _dma->clearScreen();
  if (_doubleBuffered) { _dma->flipDMABuffer(); _dma->clearScreen(); }
  return true;
}

void SnakeDisplay::show() {
  if (_dma && _doubleBuffered) _dma->flipDMABuffer();
}

bool SnakeDisplay::s_rotated = false;

// The library keeps its frame buffer private; an explicit template
// instantiation may name private members (C++ [temp.explicit]), which gives us
// a pointer-to-member without modifying the library.
namespace {
struct FbTag { typedef frameStruct* MatrixPanel_I2S_DMA::*type; friend type fbMember(FbTag); };
template <typename Tag, typename Tag::type M> struct Expose { friend typename Tag::type fbMember(Tag) { return M; } };
template struct Expose<FbTag, &MatrixPanel_I2S_DMA::fb>;

// Bits per colour-depth plane for every Rgb2 colour: plane 0 = LSB, 1 = MSB;
// bit order B G R (BIT_R1 is bit 0), as in updateMatrixDMABuffer.
struct PlaneLut {
  uint8_t p[2][64];
  PlaneLut() {
    for (int c = 0; c < 64; ++c)
      for (int d = 0; d < 2; ++d) {
        const int r = (c >> 4) & 3, g = (c >> 2) & 3, b = c & 3;
        p[d][c] = (((b >> d) & 1) << 2) | (((g >> d) & 1) << 1) | ((r >> d) & 1);
      }
  }
};
const PlaneLut LUT;
}  // namespace

void IRAM_ATTR SnakeDisplay::writeRow(int x0, int y, const uint8_t* rgb2, int n) {
  if (!_dma || y < 0 || y >= HEIGHT) return;
  if (x0 < 0) { rgb2 -= x0; n += x0; x0 = 0; }
  if (x0 + n > WIDTH) n = WIDTH - x0;
  if (n <= 0) return;
  static_assert(COLOR_BITS == 2, "writeRow assumes 2 bits per channel");
  int dx, dy, dx1, dy1;
  mapToChain(x0, y, dx, dy);
  int step = 1;
  if (n > 1) { mapToChain(x0 + 1, y, dx1, dy1); step = dx1 - dx; }
  frameStruct* fb = _dma->*fbMember(FbTag());
  const int half = PANEL_H / 2;                    // two rows are clocked out in parallel
  int row = dy, shift = 0;
  uint16_t clear = BITMASK_RGB1_CLEAR;
  if (row >= half) { row -= half; shift = BITS_RGB2_OFFSET; clear = BITMASK_RGB2_CLEAR; }
  rowBitStruct* rb = fb->rowBits[row].get();
  ESP32_I2S_DMA_STORAGE_TYPE* p0 = rb->getDataPtr(0) + dx;
  ESP32_I2S_DMA_STORAGE_TYPE* p1 = rb->getDataPtr(1) + dx;
  for (int i = 0; i < n; ++i, p0 += step, p1 += step) {
    const uint8_t c = rgb2[i] & 63;
    *p0 = (*p0 & clear) | (LUT.p[0][c] << shift);
    *p1 = (*p1 & clear) | (LUT.p[1][c] << shift);
  }
}

bool SnakeDisplay::mapToChain(int x, int y, int& dx, int& dy) {
  if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return false;
  if (s_rotated) { x = WIDTH - 1 - x; y = HEIGHT - 1 - y; }

  const int panel = y / PANEL_H;   // 0 = top panel = first in the chain
  const int py    = y % PANEL_H;
  const bool flipped = ((panel & 1) != 0) != FIRST_PANEL_FLIPPED;

  if (flipped) {
    // Panel is rotated 180 degrees: its input is on the right and its
    // row 0 is at the bottom.
    dx = panel * PANEL_W + (PANEL_W - 1 - x);
    dy = PANEL_H - 1 - py;
  } else {
    dx = panel * PANEL_W + x;
    dy = py;
  }
  return true;
}

void SnakeDisplay::setPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
  int dx, dy;
  if (!_dma || !mapToChain(x, y, dx, dy)) return;
  _dma->drawPixelRGB888(dx, dy, r, g, b);
}

void SnakeDisplay::setPixelLevel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
  // 0..15 -> 0..255 in steps of 17, so each level lands squarely in one bucket.
  const uint8_t k = 255 / MAX_LEVEL;
  setPixel(x, y, (r & MAX_LEVEL) * k, (g & MAX_LEVEL) * k, (b & MAX_LEVEL) * k);
}

void SnakeDisplay::setPixel2(int x, int y, uint8_t r2, uint8_t g2, uint8_t b2) {
  const uint8_t c = ((r2 & 3) << 4) | ((g2 & 3) << 2) | (b2 & 3);
  writeRow(x, y, &c, 1);   // exact levels (the library's linear mapping turned 170 into level 3)
}

void SnakeDisplay::setPixel565(int x, int y, uint16_t color) {
  int dx, dy;
  if (!_dma || !mapToChain(x, y, dx, dy)) return;
  _dma->drawPixel(dx, dy, color);
}

void SnakeDisplay::fill(uint8_t r, uint8_t g, uint8_t b) {
  if (_dma) _dma->fillScreenRGB888(r, g, b);   // geometry-independent
}

void SnakeDisplay::clear() {
  if (_dma) _dma->clearScreen();
}

void SnakeDisplay::setBrightness(uint8_t brightness) {
  if (_dma) _dma->setBrightness8(brightness);
}
