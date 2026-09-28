#include "Preview.h"

static void put32(uint8_t* p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void put16(uint8_t* p, uint16_t v) { p[0] = v; p[1] = v >> 8; }

void BitmapCanvas::bmpHeader(uint8_t hdr[BMP_HEADER]) const {
  memset(hdr, 0, BMP_HEADER);
  hdr[0] = 'B'; hdr[1] = 'M';
  put32(hdr + 2, BMP_SIZE);
  put32(hdr + 10, BMP_HEADER);
  put32(hdr + 14, 40);
  put32(hdr + 18, W);
  put32(hdr + 22, H);
  put16(hdr + 26, 1);
  put16(hdr + 28, 4);
  put32(hdr + 34, (uint32_t)ROW_BYTES * H);
  put32(hdr + 46, 16);
  // Palette, BGR0, from the per-image Rgb2 colours (level 1 shown a little
  // brighter than 1/3 so dim rings read like a real LED at low drive).
  static const uint8_t L[4] = {0, 140, 195, 255};   // level 1 shown brighter than 1/3 so dim rings stay visible on a phone
  for (int i = 0; i < 16; ++i) {
    const Rgb2 c = i < _nPal ? _pal[i] : RGB2_BLACK;
    uint8_t* e = hdr + 54 + i * 4;
    e[0] = L[rgb2B(c)]; e[1] = L[rgb2G(c)]; e[2] = L[rgb2R(c)]; e[3] = 0;
    if (c == RGB2_BLACK) { e[0] = e[1] = e[2] = 12; }
  }
}

void BitmapCanvas::rleStream(Sink sink, void* ctx) const {
  static const uint8_t L[4] = {0, 140, 195, 255};   // level 1 shown brighter than 1/3 so dim rings stay visible on a phone
  uint8_t hdr[RLE_HEADER_MAX];
  hdr[0] = 'R'; hdr[1] = 'L'; put16(hdr + 2, W); put16(hdr + 4, H); hdr[6] = (uint8_t)_nPal;
  for (int i = 0; i < _nPal; ++i) {
    const Rgb2 c = _pal[i];
    uint8_t* e = hdr + 7 + i * 3;
    if (c == RGB2_BLACK) { e[0] = e[1] = e[2] = 12; }
    else { e[0] = L[rgb2R(c)]; e[1] = L[rgb2G(c)]; e[2] = L[rgb2B(c)]; }
  }
  sink(hdr, 7 + _nPal * 3, ctx);
  uint8_t buf[512];
  size_t n = 0;
  int cur = -1, len = 0;
  auto flushRun = [&]() {
    while (len > 0) {
      const int l = len > 4095 ? 4095 : len;
      const uint16_t v = (uint16_t)((cur << 12) | l);
      buf[n++] = v & 0xFF; buf[n++] = v >> 8;
      len -= l;
      if (n >= sizeof(buf)) { sink(buf, n, ctx); n = 0; }
    }
  };
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      const int idx = get(x, y);
      if (idx == cur) { ++len; continue; }
      flushRun();
      cur = idx; len = 1;
    }
  flushRun();
  if (n) sink(buf, n, ctx);
}

void BitmapCanvas::bmpRow(int y, uint8_t row[ROW_BYTES]) const {
  memcpy(row, _px + (size_t)y * ROW_BYTES, ROW_BYTES);   // already 4 bpp, two pixels per byte
}
