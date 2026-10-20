// Version 3 animation decoder: see AnimV3.h for the file layout and
// tools/anim_v3.py for the matching encoder (both must stay bit-exact).
//
// Per pixel, in raster order, with v the pixel, t the previous frame's pixel
// (temporal frames only) and l1/u1/l4/u4/d1/r1 the already decoded
// neighbours (0 outside the frame):
//   1. temporal frame: bit "v == t"      (context: the same hit of the left
//      and upper pixel, and whether l1 and u1 equal t)
//   2. if l1 == u1 == l4 == u4: bit "v == l1" (context: that hit of the left
//      and upper pixel)
//   3. else the three 2-bit channels, high bit then low bit, each channel
//      with the same channel of four neighbours as context (G and B also get
//      the channel just decoded), then the light bit with the light bits of
//      l1, u1, t and the decoded colour as context.
// Bits are coded with a binary range coder (LZMA style, 12-bit
// probabilities, count-based adaptation rate).
// Masked clips (header flags bit 1): in every 128-row tile only the pixels
// inside the circle of diameter 132 (the light plus a 2 px margin the sign's
// cover hides anyway) are coded; the others are black and carry the context
// flags tf = 0, sf = 2 (maskSpan, mirrored by mask_array in anim_v3.py).
#ifdef ESP_PLATFORM
#include <esp_attr.h>
#define V3_HOT IRAM_ATTR
#endif
#include "AnimV3.h"
#include <string.h>

namespace {

enum { OFF_T = 0, OFF_S = 16, OFF_R = 25, OFF_G = OFF_R + 256 * 3, OFF_B = OFF_G + 1024 * 3, OFF_L = OFF_B + 1024 * 3 };
static_assert(OFF_L + 512 == V3_STATES, "state layout");

// Columns [xs, xe) of tile row yy (0..127) that a masked clip codes.
static void maskSpan(int yy, int& xs, int& xe) {
  const int dy = 2 * yy - 127;
  const int r2 = V3_MASK_DIAMETER * V3_MASK_DIAMETER - dy * dy;
  xs = 0; xe = 128;
  if (r2 < 0) { xs = xe = 0; return; }
  while (xs < xe && (2 * xs - 127) * (2 * xs - 127) > r2) ++xs;
  while (xe > xs && (2 * (xe - 1) - 127) * (2 * (xe - 1) - 127) > r2) --xe;
}

inline uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

// ---- input stream: a window of the file [inOff, inEnd) through r.in.
// Called only when the 1 KB buffer runs dry: fills it and returns its first
// byte (pos/len are the caller's register copies of the buffer cursor).
static uint8_t V3_HOT refill(V3Reader& r, V3Scratch& sc, uint32_t& pos, uint32_t& len) {
  pos = len = 0;
  if (sc.inOff >= sc.inEnd || sc.ioErr) return 0;      // past the frame: feed zeros
  uint32_t want = sc.inEnd - sc.inOff;
  if (want > V3_IN_BUF) want = V3_IN_BUF;
  const int got = r.read(r.rctx, sc.inOff, sc.in, want);
  if (got <= 0) { sc.ioErr = true; return 0; }
  sc.inOff += (uint32_t)got; len = (uint32_t)got; pos = 1;
  return sc.in[0];
}

inline uint8_t nextByte(V3Reader& r, V3Scratch& sc) {
  if (sc.inPos >= sc.inLen) return refill(r, sc, sc.inPos, sc.inLen);
  return sc.in[sc.inPos++];
}

// ---- binary range decoder.  State: P(bit = 0) in 1/4096 << 4 | count.
// The coder registers (range, code, input cursor) live in locals of the
// decoding loop: through a V3Reader& every byte store into a row would force
// them back to memory (char stores may alias anything), which made the
// ESP32 spend ~100 cycles per decision. Hence a macro, not a function.
#define V3_BIT(SP, OUT) do {                                                   \
    uint16_t* sp_ = (SP);                                                      \
    const uint32_t s_ = *sp_, p_ = s_ >> 4, n_ = s_ & 15;                      \
    const uint32_t bound_ = (rc_range >> 12) * p_;                             \
    const uint32_t sh_ = n_ < 5 ? n_ + 1 : 5;                                  \
    uint32_t np_;                                                              \
    if (rc_code < bound_) { rc_range = bound_; (OUT) = 0; np_ = p_ + ((4080 - p_) >> sh_); } \
    else { rc_code -= bound_; rc_range -= bound_; (OUT) = 1; np_ = p_ - ((p_ - 16) >> sh_); } \
    *sp_ = (uint16_t)((np_ << 4) | (n_ < 15 ? n_ + 1 : 15));                   \
    while (rc_range < (1u << 24)) {                                            \
      rc_range <<= 8;                                                          \
      rc_code = (rc_code << 8) | (rc_pos < rc_len ? rc_in[rc_pos++] : refill(r, sc, rc_pos, rc_len)); \
    }                                                                          \
  } while (0)

bool readAt(V3Reader& r, uint32_t off, uint8_t* buf, uint32_t len) {
  return r.read(r.rctx, off, buf, len) == (int)len;
}

// Decode frame `frame` whose data starts at `off` with `len` bytes.
// Rows go to emit (if not NULL) and into r.prev (if present).
bool V3_HOT decodeOne(V3Reader& r, V3Scratch& sc, uint32_t off, uint32_t len, V3EmitRow emit, void* ectx) {
  if (len < 6) return false;
  sc.inOff = off; sc.inEnd = off + len; sc.inPos = sc.inLen = 0; sc.ioErr = false;
  const uint8_t fflags = nextByte(r, sc);
  const bool temporal = (fflags & 1) != 0;
  if (temporal && !r.prev) return false;
  uint32_t rc_range = 0xFFFFFFFFu, rc_code = 0;
  uint32_t rc_pos = sc.inPos, rc_len = sc.inLen;
  const uint8_t* const rc_in = sc.in;
  for (int i = 0; i < 5; ++i) rc_code = (rc_code << 8) | (rc_pos < rc_len ? rc_in[rc_pos++] : refill(r, sc, rc_pos, rc_len));
  // every frame starts from the prior
  uint16_t* const st = sc.model.st;
  for (int i = 0; i < V3_STATES; ++i) st[i] = (uint16_t)((r.prior[i] << 8) | 3);
  const int w = r.w, h = r.h;
  const int set = r.ctxset;
  const bool masked = (r.flags & 2) != 0;
  memset(sc.rows, 0, sizeof(sc.rows));
  memset(sc.tf, 0, sizeof(sc.tf)); memset(sc.sf, 0, sizeof(sc.sf));
  uint16_t* const stT = st + OFF_T; uint16_t* const stS = st + OFF_S; uint16_t* const stL = st + OFF_L;
  uint16_t* const stR = st + OFF_R; uint16_t* const stG = st + OFF_G; uint16_t* const stB = st + OFF_B;
  for (int y = 0; y < h; ++y) {
    uint8_t* const row = sc.rows[y & 7];
    const uint8_t* const up1 = y >= 1 ? sc.rows[(y - 1) & 7] : NULL;
    const uint8_t* const up4 = y >= 4 ? sc.rows[(y - 4) & 7] : NULL;
    const uint8_t* const tprev = temporal ? r.prev + (size_t)y * w : NULL;
    uint8_t* const tfc = sc.tf[y & 1]; const uint8_t* const tfu = sc.tf[(y + 1) & 1];
    uint8_t* const sfc = sc.sf[y & 1]; const uint8_t* const sfu = sc.sf[(y + 1) & 1];
    int xs = 0, xe = w;
    if (masked) {   // hidden pixels: black, tf 0, sf 2, no decisions
      maskSpan(y & 127, xs, xe);
      memset(row, 0, w); memset(tfc, 0, w); memset(sfc, 2, w);
    }
    for (int x = xs; x < xe; ++x) {
      const int l1 = x >= 1 ? row[x - 1] : 0;
      const int u1 = up1 ? up1[x] : 0;
      const int l4 = x >= 4 ? row[x - 4] : 0;
      const int u4 = up4 ? up4[x] : 0;
      const int tl = x ? tfc[x - 1] : 0, tu = y ? tfu[x] : 0;
      const int sl = x ? sfc[x - 1] : 0, su = y ? sfu[x] : 0;
      int bit;
      if (temporal) {
        const int t = tprev[x];
        V3_BIT(stT + tl * 8 + tu * 4 + (l1 == t) * 2 + (u1 == t), bit);
        tfc[x] = (uint8_t)bit;
        if (bit) { row[x] = (uint8_t)t; sfc[x] = 2; continue; }
      } else tfc[x] = 0;
      if (l1 == u1 && l1 == l4 && l1 == u4) {
        V3_BIT(stS + sl * 3 + su, bit);
        sfc[x] = (uint8_t)(bit ? 1 : 0);
        if (bit) { row[x] = (uint8_t)l1; continue; }
      } else sfc[x] = 2;
      // neighbours for the channel contexts
      int n2, n3;
      if (set == 0) { n2 = l4; n3 = u4; }
      else { n2 = (x >= 1 && up1) ? up1[x - 1] : 0; n3 = (x + 1 < w && up1) ? up1[x + 1] : 0; }
      int colour = 0, prevCh = 0;
      for (int ch = 0; ch < 3; ++ch) {
        const int sh = 4 - 2 * ch;
        const int c = (((l1 >> sh) & 3) << 6) | (((u1 >> sh) & 3) << 4) | (((n2 >> sh) & 3) << 2) | ((n3 >> sh) & 3);
        uint16_t* const p = ch == 0 ? stR + c * 3 : (ch == 1 ? stG : stB) + ((c << 2) | prevCh) * 3;
        int hi, lo;
        V3_BIT(p, hi);
        V3_BIT(p + 1 + hi, lo);
        prevCh = hi * 2 + lo;
        colour = (colour << 2) | prevCh;
      }
      const int tlight = temporal ? (tprev[x] >> 6) & 1 : 0;
      int light;
      V3_BIT(stL + ((l1 >> 6) & 1) * 256 + ((u1 >> 6) & 1) * 128 + tlight * 64 + colour, light);
      row[x] = (uint8_t)((light << 6) | colour);
    }
    if (emit) emit(ectx, y, row);
    if (r.prev) memcpy(r.prev + (size_t)y * w, row, w);
  }
  sc.inPos = rc_pos; sc.inLen = rc_len; sc.range = rc_range; sc.code = rc_code;
  return !sc.ioErr;
}

bool frameEntry(V3Reader& r, int frame, uint16_t* delay, uint32_t* bytes) {
  uint8_t e[6];
  if (!readAt(r, r.frameTableOff + 6u * (uint32_t)frame, e, 6)) return false;
  if (delay) *delay = rd16(e);
  if (bytes) *bytes = rd32(e + 2);
  return true;
}

}  // namespace

bool v3Begin(V3Reader& r, V3ReadFn read, void* ctx, uint8_t* prevBuf, uint32_t prevBufSize) {
  memset(&r, 0, sizeof(r));
  r.read = read; r.rctx = ctx; r.lastFrame = -1;
  uint8_t hd[20];
  if (!readAt(r, 0, hd, 20)) return false;
  if (hd[0] != 'S' || hd[1] != 'A' || hd[2] != 'N' || hd[3] != 3) return false;
  r.w = rd16(hd + 4); r.h = rd16(hd + 6); r.n = rd16(hd + 8);
  r.K = hd[12]; r.ctxset = hd[13]; r.flags = hd[14];
  r.nkeys = rd16(hd + 16);
  const uint16_t priorBytes = rd16(hd + 18);
  if (r.w == 0 || r.w > V3_MAX_W || r.h == 0 || r.n == 0 || r.K == 0 || r.ctxset > 1 || priorBytes != V3_STATES) return false;
  if ((r.flags & ~3) || ((r.flags & 2) && (r.w != 128 || (r.h & 127)))) return false;   // unknown flags; a masked clip is 128-wide tiles
  if (r.nkeys != (r.n + r.K - 1) / r.K) return false;
  if (r.flags & 1) {
    if (!prevBuf || prevBufSize < (uint32_t)r.w * r.h) return false;
    r.prev = prevBuf;
  } else r.prev = NULL;   // independent frames: nothing to carry over (two decoders may share the clip)
  r.frameTableOff = 20;
  r.keyTableOff = 20 + 6u * r.n;
  return readAt(r, r.keyTableOff + 4u * r.nkeys, r.prior, V3_STATES);
}

uint16_t v3FrameDelay(V3Reader& r, int frame) {
  uint16_t d = 0;
  if (frame < 0 || frame >= r.n || !frameEntry(r, frame, &d, NULL)) return 0;
  return d;
}

bool v3DecodeFrame(V3Reader& r, V3Scratch& sc, int frame, V3EmitRow emit, void* ectx) {
  if (frame < 0 || frame >= r.n) return false;
  int start = frame - frame % r.K;
  // continue from the previous frame when it is still in r.prev
  if (r.prev && r.lastFrame >= start && r.lastFrame < frame) start = r.lastFrame + 1;
  if (!(r.flags & 1)) start = frame;   // every frame independent
  r.lastFrame = -1;
  uint8_t kb[4];
  if (!readAt(r, r.keyTableOff + 4u * (uint32_t)(start / r.K), kb, 4)) return false;
  uint32_t off = rd32(kb);
  if (start % r.K) {   // walk the table from the keyframe to start
    for (int i = start - start % r.K; i < start; ++i) {
      uint32_t bytes;
      if (!frameEntry(r, i, NULL, &bytes)) return false;
      off += bytes;
    }
  }
  for (int i = start; i <= frame; ++i) {
    uint32_t bytes;
    if (!frameEntry(r, i, NULL, &bytes)) return false;
    if (!decodeOne(r, sc, off, bytes, i == frame ? emit : NULL, ectx)) return false;
    off += bytes;
  }
  if (r.prev) r.lastFrame = frame;
  return true;
}
