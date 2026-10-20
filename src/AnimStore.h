#pragma once
#include <Arduino.h>
#include <vector>
#include "Canvas.h"

// Uploaded animations, stored in LittleFS as /a/<id>.anm and made on the PC by
// tools/anim_convert.py. File format (little endian):
//   "SAN" <version>, w u16, h u16, n u16, reserved u16,
//   n x { delay ms u16, bytes u32 },
//   n frames, rows top to bottom, each one of
//     version 1: run-length pairs (count 1..255, pixel)
//     version 2: raw deflate (no zlib header, 1 KB window) of w*h pixel bytes;
//                about 2.5x smaller than version 1 on video. Inflated with the
//                ROM's tinfl through a 1 KB ring buffer (~12 KB static RAM).
//     version 3: context-model arithmetic coding (src/AnimV3.h, tools/anim_v3.py),
//                ~32% smaller than version 2 on the user's clips. The frame
//                table sits at offset 20 and frames are decoded by a per-clip
//                V3Reader (three slots, previous-frame buffers in PSRAM).
// Pixel bits 0-5 = Rgb2 colour (0bRRGGBB), bit 6 = "light" for the
// silhouette modes (decided on the PC from the full-colour luminance).
// 128x128 = one light (clipped to the circle); 128x384 = the whole sign.
struct AnimInfo {
  String id;
  uint16_t w = 0, h = 0;
  uint8_t version = 1;
  std::vector<uint16_t> delays;
  std::vector<uint32_t> offsets;     // file offset of each frame
  uint32_t totalMs = 0;
  bool ok() const { return !delays.empty(); }
  bool full() const { return h > w; }
  int frameAt(uint32_t ms) const { return frameIn(ms, 0, 0); }
  // Frame at ms when only [fromMs, fromMs + lenMs) of the clip loops
  // (lenMs 0 = to the end).
  int frameIn(uint32_t ms, uint32_t fromMs, uint32_t lenMs) const;
};

enum AnimMode : uint8_t { ANIM_ORIGINAL = 0, ANIM_MONO = 1, ANIM_MONO_INV = 2 };

// Timing of drawFrame, for finding stutter (read with "#ANIM PERF").
struct AnimPerf {
  uint32_t frames = 0;
  uint64_t usOpen = 0, usRead = 0, usInflate = 0, usTotal = 0;
  uint32_t maxTotal = 0, maxOpen = 0, maxRead = 0, maxInflate = 0, maxPixels = 0;
  int worstFrame = -1;
  uint32_t worstParts[4] = {0};   // open, read, inflate, pixels of the slowest frame
  uint32_t hist[8] = {0};   // total per frame: <10 <20 <40 <60 <80 <100 <150 >=150 ms
  void add(uint32_t open, uint32_t read, uint32_t inflate, uint32_t total, int frame);
};
extern AnimPerf animPerf;

namespace AnimStore {
struct Entry { String id; uint16_t w, h, frames; uint32_t bytes, ms; };
constexpr int CIRCLE = 128, FULL_H = 384;
bool begin();                               // mount (formats an empty partition)
std::vector<Entry> list();
bool exists(const String& id);
bool load(const String& id, AnimInfo& out);
bool remove(const String& id);
// Close the files kept open for playback. Call (under the playback lock)
// before anything replaces, renames or deletes a stored file.
void closeAll();
size_t freeBytes();
size_t totalBytes();
String pathFor(const String& id);
String sanitizeId(const String& raw);        // a-z 0-9 - _, at most 24 chars
bool validate(const String& path, String& err);
// One frame with its top-left at (x0, y0). clipCircle: only the pixels inside
// the 128 px circle. mode: original colours, or silhouette in fg on bg.
// Timing breakdown of one version-3 frame (microseconds), for "#ANIM BENCH".
struct BenchResult { uint32_t fileRead, decodeNoEmit, decodeNoEmitWarm, modelReload, decodeFromRam, bytes; bool ok; };
BenchResult bench(const AnimInfo& a, int frame);
// Returns false when the frame is not ready yet (version 3 clips are decoded
// in the background): nothing was painted, call again next loop.
bool drawFrame(const AnimInfo& a, int frame, Canvas& c, int x0, int y0, bool clipCircle,
               uint8_t mode, Rgb2 fg, Rgb2 bg);
// Start the two background decoder tasks (one per core). Call after begin().
void beginDecoders();
uint32_t internalFallbacks();   // diagnostics: frame buffers that did not fit in PSRAM
// Diagnostics: where the render core and each decoder last were (see PERF).
extern volatile int dbgRenderStage, dbgDecStage[2];
extern volatile uint32_t dbgRenderBeat;
}
