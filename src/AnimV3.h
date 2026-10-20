#pragma once
// Animation file format version 3: context-model arithmetic coding of the
// 7-bit pixels (bits 0-5 = 0bRRGGBB, bit 6 = silhouette light).  Portable:
// no Arduino headers, no heap, no floating point.  Encoder: tools/anim_v3.py.
//
// File layout (little endian):
//    0  "SAN" 0x03
//    4  w u16, h u16, n u16, reserved u16 (0)
//   12  K u8        keyframe interval: frame i is a keyframe iff i % K == 0
//   13  ctxset u8   0: contexts from left, up, left-4, up-4 (ordered dither)
//                   1: contexts from left, up, up-left, up-right
//   14  flags u8    bit 0: some frames are temporal (coded against the
//                   previous frame; the reader then needs a w*h buffer)
//                   bit 1: masked: only the pixels inside the circle of
//                   diameter 132 of each 128x128 tile are coded, the rest
//                   is black (w must be 128, h a multiple of 128)
//   15  reserved u8 (0)
//   16  nkeys u16   = ceil(n / K)
//   18  priorBytes u16 = V3_STATES
//   20  frame table n x { delay ms u16, bytes u32 }      (bytes = flag byte + coder bytes)
//   20+6n  key table nkeys x { file offset u32 of frame k*K }
//   then the prior: one byte per model state (trained probability, 8 bits)
//   then the frames: { flags u8 (bit 0: temporal), range coder bytes }
//
// Every frame starts from the prior, so decoding a frame needs only that
// frame's bytes plus, for a temporal frame, the previous frame's pixels.  A
// keyframe is never temporal, so seeking to frame f decodes at most
// f % K extra frames.  Whole-sign clips are written with K = 1 (every frame
// independent); one-light clips with K = 8 and temporal frames.
//
// Memory: V3Reader ~7.5 KB per clip (mostly the prior), V3Scratch ~17.5 KB
// per decoder (the model plus row/input buffers), plus the caller's
// previous-frame buffer (w*h bytes) when the header's temporal flag is set.
#include <stdint.h>
#include <stddef.h>
#ifndef V3_HOT
#define V3_HOT   // on the ESP32 the firmware defines this as IRAM_ATTR so the hot loop runs from RAM
#endif

enum { V3_MAX_W = 128, V3_IN_BUF = 1024, V3_STATES = 7449, V3_MASK_DIAMETER = 132 };

// read len bytes at offset into buf, return the number read (<0 on error).
typedef int (*V3ReadFn)(void* ctx, uint32_t offset, uint8_t* buf, uint32_t len);
// one decoded row (w pixel bytes), top to bottom.
typedef void (*V3EmitRow)(void* ctx, int y, const uint8_t* px);

struct V3Model { uint16_t st[V3_STATES]; };

// Per-clip state: header, prior, previous frame (for temporal clips).
struct V3Reader {
  V3ReadFn read; void* rctx;
  uint16_t w, h, n, nkeys;
  uint8_t K, ctxset, flags;
  uint32_t frameTableOff, keyTableOff;
  uint8_t prior[V3_STATES];
  uint8_t* prev;             // w*h bytes or NULL (required when flags & 1)
  int lastFrame;             // last frame fully decoded into prev, -1 = none
};

// Per-decoder scratch (~17.5 KB): the model (reloaded from the prior every
// frame) and the working buffers of one frame decode. One per decoding task;
// any scratch can decode any clip.
struct V3Scratch {
  V3Model model;
  uint8_t rows[8][V3_MAX_W]; // ring of decoded rows of the current frame
  uint8_t tf[2][V3_MAX_W], sf[2][V3_MAX_W];
  uint8_t in[V3_IN_BUF];
  uint32_t inPos, inLen, inOff, inEnd;
  bool ioErr;
  uint32_t range, code;
};

// Parse the header and load the prior.  prevBuf may be NULL for clips
// without temporal frames.
bool v3Begin(V3Reader& r, V3ReadFn read, void* ctx, uint8_t* prevBuf, uint32_t prevBufSize);
// Delay of a frame in ms (reads the table), 0 on error.
uint16_t v3FrameDelay(V3Reader& r, int frame);
// Decode one frame, emitting every row.  Decodes from the nearest keyframe
// when the frame does not follow the last decoded one. Two decoders may work
// on different frames of the same non-temporal clip at once (each with its
// own scratch); frames of a temporal clip must be decoded one at a time.
bool v3DecodeFrame(V3Reader& r, V3Scratch& sc, int frame, V3EmitRow emit, void* ectx);
