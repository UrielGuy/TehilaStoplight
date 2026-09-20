#include "SnakeDisplay.h"
#include "PanelPins.h"

SnakeDisplay::~SnakeDisplay() {
  delete _dma;
  _dma = nullptr;
}

bool SnakeDisplay::begin(const Options& opt) {
  if (_dma) return true;

  HUB75_I2S_CFG::i2s_pins pins = {
      PanelPins::r1_pin, PanelPins::g1_pin, PanelPins::b1_pin,
      PanelPins::r2_pin, PanelPins::g2_pin, PanelPins::b2_pin,
      PanelPins::a_pin,  PanelPins::b_pin,  PanelPins::c_pin,
      PanelPins::d_pin,  PanelPins::e_pin,
      PanelPins::lat_pin, PanelPins::oe_pin, PanelPins::clk_pin};

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

bool SnakeDisplay::mapToChain(int x, int y, int& dx, int& dy) {
  if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return false;

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
  setPixel(x, y, (r2 & 3) * 85, (g2 & 3) * 85, (b2 & 3) * 85);
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
