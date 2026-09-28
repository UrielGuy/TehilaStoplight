#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

// SnakeDisplay: six 128x64 HUB75E panels stacked vertically and chained in a
// serpentine (panel 0 at the top, wired left->right; panel 1 below it rotated
// 180 degrees so its input is on the right; and so on). Presents them as one
// 128 wide x 384 tall canvas with (0,0) at the top-left.
//
// The DMA driver only understands a single horizontal chain (768x64), so this
// class does the coordinate remap. Odd-numbered panels are rotated 180 degrees.
//
// Colour depth is COLOR_BITS (2) bits per channel: every 8-bit colour value
// you pass is truncated to its top two bits, giving 4 levels per channel and
// 64 colours. The DMA buffer is 48 KB per bit-plane (96 KB, or 192 KB with
// double buffering). Refresh is ~108 Hz at a 10 MHz clock. setPixel2() /
// setPixelLevel() address the hardware levels (0..3) directly.
class SnakeDisplay {
 public:
  static constexpr int PANEL_W    = 128;
  static constexpr int PANEL_H    = 64;
#ifndef PANEL_COUNT
#define PANEL_COUNT 6
#endif
  static constexpr int PANELS     = PANEL_COUNT;        // -DPANEL_COUNT=2 for the two-panel bench
  static constexpr int WIDTH      = PANEL_W;            // 128
  static constexpr int HEIGHT     = PANEL_H * PANELS;   // 384
  static constexpr uint8_t COLOR_BITS = 2;              // bits per channel
  static constexpr uint8_t MAX_LEVEL  = (1 << COLOR_BITS) - 1;  // 3

  // Every panel's content rotated 180 degrees relative to the default
  // assumption: the panel the adapter plugs into (top) has its input on the
  // RIGHT, the next one on the left, and so on. Verified on the installed
  // sign 2026-09-28. Build flag; the loose two-panel bench uses 0.
#ifndef FIRST_PANEL_FLIPPED_DEFAULT
#define FIRST_PANEL_FLIPPED_DEFAULT 1
#endif
  static constexpr bool FIRST_PANEL_FLIPPED = FIRST_PANEL_FLIPPED_DEFAULT;

  SnakeDisplay() = default;
  ~SnakeDisplay();

  struct Options {
    HUB75_I2S_CFG::clk_speed clock = HUB75_I2S_CFG::HZ_10M;
    uint8_t minRefreshHz = 60;    // driver shortens low bit-planes to stay above this
    bool doubleBuffer = false;    // draw off-screen, then show(); doubles DMA RAM
  };

  // Allocates the DMA buffers and starts refreshing. Call this BEFORE
  // WiFi.begin() so the ~96 KB of DMA-capable RAM is still free.
  bool begin(const Options& opt);
  bool begin() { return begin(Options()); }

  // 8-bit RGB (only the top 2 bits of each channel are used).
  void setPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b);
  // Hardware levels, each channel 0..MAX_LEVEL (15).
  void setPixelLevel(int x, int y, uint8_t r, uint8_t g, uint8_t b);
  // Coarse 0..3 per channel (kept for the test pattern; maps to 0,5,10,15).
  void setPixel2(int x, int y, uint8_t r2, uint8_t g2, uint8_t b2);
  // RGB565 (as used by Adafruit_GFX).
  void setPixel565(int x, int y, uint16_t color);

  void fill(uint8_t r, uint8_t g, uint8_t b);
  void clear();
  // With doubleBuffer: makes the frame drawn so far visible. Without: no-op.
  void show();
  bool isDoubleBuffered() const { return _doubleBuffered; }

  // 0..255. The DMA driver implements this by trimming the OE pulse, so it
  // costs no colour resolution.
  void setBrightness(uint8_t brightness);

  // Map canvas (x,y) to DMA-chain (dx,dy). Returns false if out of range.
  // Exposed so tests / a host-side simulator can check the geometry.
  static bool mapToChain(int x, int y, int& dx, int& dy);

  // Escape hatch: the underlying Adafruit_GFX-derived panel object, if you
  // want to draw straight onto the 768x64 chain.
  MatrixPanel_I2S_DMA* raw() { return _dma; }

  bool isRunning() const { return _dma != nullptr; }

 private:
  MatrixPanel_I2S_DMA* _dma = nullptr;
  bool _doubleBuffered = false;
};
