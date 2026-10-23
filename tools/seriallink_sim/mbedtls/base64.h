#pragma once
#include <cstddef>
inline int mbedtls_base64_encode(unsigned char* dst, size_t dlen, size_t* olen, const unsigned char* src, size_t slen) {
  static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t need = (slen + 2) / 3 * 4;
  if (need + 1 > dlen) { *olen = need + 1; return -0x002A; }
  size_t o = 0;
  for (size_t i = 0; i < slen; i += 3) {
    unsigned v = src[i] << 16 | (i + 1 < slen ? src[i + 1] << 8 : 0) | (i + 2 < slen ? src[i + 2] : 0);
    dst[o++] = T[(v >> 18) & 63];
    dst[o++] = T[(v >> 12) & 63];
    dst[o++] = i + 1 < slen ? T[(v >> 6) & 63] : '=';
    dst[o++] = i + 2 < slen ? T[v & 63] : '=';
  }
  dst[o] = 0;
  *olen = o;
  return 0;
}
