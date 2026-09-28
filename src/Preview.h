#pragma once
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include "Canvas.h"

// Off-screen 128x384 canvas at 4 bits per pixel indexing a per-image palette
// of up to 16 Rgb2 colours (a face uses at most 7), plus a 4-bpp indexed BMP
// encoder so the web page (and the host harness) can show exactly what the
// LEDs show. No Arduino dependencies.
class BitmapCanvas : public Canvas {
 public:
  static constexpr int W = 128, H = 384;
  static constexpr int ROW_BYTES = W / 2;
  static constexpr size_t BMP_HEADER = 14 + 40 + 16 * 4;
  static constexpr size_t BMP_SIZE = BMP_HEADER + (size_t)ROW_BYTES * H;

  BitmapCanvas() { reset(); }
  // Clear pixels and the palette. Call before every image when the canvas is
  // reused: the palette holds at most 16 colours and new ones map to black
  // once it is full (that made random-coloured previews go black).
  void reset() { memset(_px, 0, sizeof(_px)); _pal[0] = RGB2_BLACK; _nPal = 1; }
  void set(int x, int y, Rgb2 c) override {
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    const uint8_t idx = index(c);
    const int i = y * W + x;
    if (i & 1) _px[i >> 1] = (_px[i >> 1] & 0xF0) | idx;
    else       _px[i >> 1] = (_px[i >> 1] & 0x0F) | (idx << 4);
  }
  uint8_t get(int x, int y) const {           // palette index
    const int i = y * W + x;
    return (i & 1) ? (_px[i >> 1] & 0x0F) : (_px[i >> 1] >> 4);
  }
  Rgb2 colourAt(int x, int y) const { return _pal[get(x, y)]; }
  // BMP is produced in pieces: the header, then rows from the bottom up.
  void bmpHeader(uint8_t out[BMP_HEADER]) const;
  void bmpRow(int y, uint8_t out[ROW_BYTES]) const;

  // Compact run-length stream for the live preview:
  //   'R','L', W lo, W hi, H lo, H hi, nPal, then nPal x (R,G,B) 8-bit,
  //   then runs of uint16 little-endian: high 4 bits = palette index,
  //   low 12 bits = run length (max 4095; longer runs are split).
  // sink(bytes, len, ctx) is called with successive pieces.
  typedef void (*Sink)(const uint8_t* p, size_t n, void* ctx);
  void rleStream(Sink sink, void* ctx) const;
  static constexpr size_t RLE_HEADER_MAX = 7 + 16 * 3;
 private:
  uint8_t index(Rgb2 c) {
    for (int i = 0; i < _nPal; ++i) if (_pal[i] == c) return i;
    if (_nPal < 16) { _pal[_nPal] = c; return _nPal++; }
    return 0;   // palette full: draw black rather than a wrong colour
  }
  uint8_t _px[W * H / 2];
  Rgb2 _pal[16];
  int _nPal;
};
