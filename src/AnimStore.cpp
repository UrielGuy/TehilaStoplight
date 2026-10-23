#include "AnimStore.h"
#include <LittleFS.h>
#include <esp32s3/rom/miniz.h>
#include "AnimV3.h"
#include <new>

static const char* DIR_PATH = "/a";
static const char* MAGIC = "SAN";

int AnimInfo::frameIn(uint32_t ms, uint32_t fromMs, uint32_t lenMs) const {
  if (delays.size() <= 1 || totalMs == 0) return 0;
  if (fromMs >= totalMs) fromMs = 0;
  if (lenMs == 0 || fromMs + lenMs > totalMs) lenMs = totalMs - fromMs;
  uint32_t t = fromMs + ms % lenMs;
  for (size_t i = 0; i < delays.size(); ++i) { if (t < delays[i]) return (int)i; t -= delays[i]; }
  return 0;
}

AnimPerf animPerf;
namespace AnimStore {
volatile int dbgRenderStage = 0, dbgDecStage[2] = {0, 0};
volatile uint32_t dbgRenderBeat = 0;
}
void AnimPerf::add(uint32_t open, uint32_t read, uint32_t inflate, uint32_t total, int frame) {
  ++frames; usOpen += open; usRead += read; usInflate += inflate; usTotal += total;
  const uint32_t pixels = total - open - read - inflate;
  if (inflate > maxInflate) maxInflate = inflate;
  if (pixels > maxPixels) maxPixels = pixels;
  if (total > maxTotal) { maxTotal = total; worstFrame = frame; worstParts[0] = open; worstParts[1] = read; worstParts[2] = inflate; worstParts[3] = pixels; }
  if (open > maxOpen) maxOpen = open;
  if (read > maxRead) maxRead = read;
  static const uint32_t edges[7] = {10000, 20000, 40000, 60000, 80000, 100000, 150000};
  int b = 0; while (b < 7 && total >= edges[b]) ++b;
  ++hist[b];
}

namespace AnimStore {

bool begin() {
  if (!LittleFS.begin(true)) { Serial.println("[anim] LittleFS mount failed"); return false; }
  if (!LittleFS.exists(DIR_PATH)) LittleFS.mkdir(DIR_PATH);
  LittleFS.remove("/a/.upload");
  Serial.printf("[anim] storage %u KB, %u KB free\n", (unsigned)(totalBytes() / 1024), (unsigned)(freeBytes() / 1024));
  return true;
}

size_t totalBytes() { static size_t t = LittleFS.totalBytes(); return t; }   // fixed; the call walks the file system too
// usedBytes() walks the whole file system: ~0.5 s on the full 4.7 MB, with
// the renderer stalled meanwhile (flash access), and /api/state asked for it
// on every poll (2026-10-23). Kept until closeAll(), which precedes every change.
static size_t s_used = 0;
static bool s_usedValid = false;
size_t freeBytes() {
  if (!s_usedValid) { s_used = LittleFS.usedBytes(); s_usedValid = true; }
  const size_t t = totalBytes();
  return t > s_used ? t - s_used : 0;
}
String pathFor(const String& id) { return String(DIR_PATH) + "/" + id + ".anm"; }
std::vector<Entry> list();
bool exists(const String& id) {
  if (!id.length()) return false;
  for (const auto& e : list()) if (e.id == id) return true;
  return false;
}

String sanitizeId(const String& raw) {
  String s = raw;
  const int slash = s.lastIndexOf('/'); if (slash >= 0) s = s.substring(slash + 1);
  if (s.endsWith(".anm")) s = s.substring(0, s.length() - 4);
  String out;
  for (size_t i = 0; i < s.length() && out.length() < 24; ++i) {
    char ch = tolower(s[i]);
    if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_') out += ch;
    else if (ch == ' ' || ch == '.') out += '-';
  }
  return out.length() ? out : String("anim");
}

// Parses the header and frame table; expectSize = the file size the header
// implies (validate() compares it with the real size).
static bool readHeader(File& f, AnimInfo& a, uint32_t& expectSize) {
  uint8_t h[20];
  if (f.read(h, 12) != 12 || memcmp(h, MAGIC, 3) != 0 || h[3] < 1 || h[3] > 3) return false;
  a.version = h[3];
  a.w = h[4] | (h[5] << 8); a.h = h[6] | (h[7] << 8);
  const uint16_t n = h[8] | (h[9] << 8);
  if (a.w != CIRCLE || (a.h != CIRCLE && a.h != FULL_H) || n == 0 || n > 2000) return false;
  uint32_t tableOff = 12, fixed = 12 + 6u * n;
  if (a.version == 3) {
    if (f.read(h + 12, 8) != 8) return false;
    const uint16_t nkeys = h[16] | (h[17] << 8), priorBytes = h[18] | (h[19] << 8);
    if (priorBytes != V3_STATES) return false;   // a different model layout: not ours
    tableOff = 20; fixed = 20 + 6u * n + 4u * nkeys + priorBytes;
  }
  a.delays.resize(n); a.offsets.resize(n); a.totalMs = 0;
  uint32_t off = fixed, data = 0;
  std::vector<uint8_t> table(6u * n);   // one read, not n small ones
  if (!f.seek(tableOff) || f.read(table.data(), table.size()) != (int)table.size()) return false;
  for (uint16_t i = 0; i < n; ++i) {
    const uint8_t* e = &table[6u * i];
    uint16_t d = e[0] | (e[1] << 8); if (d < 20) d = 20;
    const uint32_t len = e[2] | (e[3] << 8) | ((uint32_t)e[4] << 16) | ((uint32_t)e[5] << 24);
    a.delays[i] = d; a.offsets[i] = off; off += len; data += len; a.totalMs += d;
  }
  expectSize = fixed + data;
  return true;
}

bool load(const String& id, AnimInfo& out) {
  out = AnimInfo();
  if (!id.length()) return false;
  File f = LittleFS.open(pathFor(id), "r");
  if (!f) return false;
  uint32_t data;
  const bool ok = readHeader(f, out, data);
  f.close();
  if (!ok) { out = AnimInfo(); return false; }
  out.id = id;
  return true;
}

bool validate(const String& path, String& err) {
  File f = LittleFS.open(path, "r");
  if (!f) { err = "upload not stored"; return false; }
  AnimInfo a; uint32_t expect;
  const bool ok = readHeader(f, a, expect);
  const size_t size = f.size();
  f.close();
  if (!ok) { err = "not an animation file (make it with tools/anim_convert.py)"; return false; }
  if (size != expect) { err = "file is truncated or the storage is full"; return false; }
  return true;
}

// The list is kept in RAM: reading the files' headers is a flash operation,
// and every flash operation pauses the other core (the renderer) while it
// runs. Page polling used to stretch animation frames from 24 to 80 ms that
// way (measured 2026-10-19). closeAll() runs before every change and drops it.
static std::vector<Entry> s_list;
static bool s_listValid = false;

static std::vector<Entry> scan();
std::vector<Entry> list() {
  if (!s_listValid) { s_list = scan(); s_listValid = true; }
  return s_list;
}

static std::vector<Entry> scan() {
  std::vector<Entry> out;
  File dir = LittleFS.open(DIR_PATH);
  if (!dir) return out;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String name = f.name();
    if (!name.endsWith(".anm")) continue;
    AnimInfo a; uint32_t data;
    if (readHeader(f, a, data))
      out.push_back({name.substring(0, name.length() - 4), a.w, a.h, (uint16_t)a.delays.size(), (uint32_t)f.size(), a.totalMs});
  }
  return out;
}

void closeAll();
bool remove(const String& id) { closeAll(); return LittleFS.remove(pathFor(id)); }

// Open file handles of the animations being played (render core only). Every
// change to a stored file goes through closeAll() first, under the playback lock.
struct CachedFile { String id; File f; };
static CachedFile s_open[3];
static int s_next = 0;

namespace { void v3Forget(); extern SemaphoreHandle_t s_mx, s_fileMx; struct V3Slot; }
static void closeSlotFiles();
void closeAll() {
  if (s_mx) { xSemaphoreTake(s_mx, portMAX_DELAY); v3Forget(); xSemaphoreGive(s_mx); }
  if (s_fileMx) xSemaphoreTake(s_fileMx, portMAX_DELAY);
  for (auto& o : s_open) { if (o.f) o.f.close(); o.id = ""; }
  closeSlotFiles();
  if (s_fileMx) xSemaphoreGive(s_fileMx);
  s_listValid = false;
  s_usedValid = false;
}


static File* openCached(const String& id) {
  for (auto& o : s_open) if (o.f && o.id == id) return &o.f;
  CachedFile& o = s_open[s_next]; s_next = (s_next + 1) % 3;
  if (o.f) o.f.close();
  o.f = LittleFS.open(pathFor(id), "r");
  o.id = o.f ? id : String();
  return o.f ? &o.f : nullptr;
}

// Columns [xs, xe) of row y that lie inside the 128 px circle.
static void circleSpan(int y, int& xs, int& xe) {
  const int dy = 2 * y - (CIRCLE - 1);
  const int r2 = CIRCLE * CIRCLE - dy * dy;
  xs = 0; xe = CIRCLE;
  if (r2 < 0) { xs = xe = 0; return; }
  while (xs < xe && (2 * xs - (CIRCLE - 1)) * (2 * xs - (CIRCLE - 1)) > r2) ++xs;
  while (xe > xs && (2 * (xe - 1) - (CIRCLE - 1)) * (2 * (xe - 1) - (CIRCLE - 1)) > r2) --xe;
}

// ======== version 3: background decoding on both cores ========
// Decoding a whole-sign frame costs ~95 ms of CPU, more than the 83 ms frame
// period at 12 fps, so one core cannot keep up (measured 2026-10-19). A clip
// being played gets a slot: its reader (header, prior, previous frame) and a
// ring of three decoded frames in PSRAM. Two decoder tasks, one per core,
// keep the ring filled up to AHEAD frames beyond the frame being shown; the
// render core only copies a ready frame to the LEDs.
namespace {
constexpr int SLOTS = 3, RING = 3, AHEAD = 2;
struct FrameBuf { uint8_t* px = nullptr; int frame = -1; uint8_t state = 0; };   // 0 free, 1 decoding, 2 ready
struct V3Slot;
struct SlotRange { V3Slot* begin() const; V3Slot* end() const; };   // the SLOTS slots (see beginDecoders)
struct V3Slot {
  String id; File f; V3Reader r;
  uint8_t* prev = nullptr; uint32_t prevSize = 0;
  uint32_t pxSize = 0; FrameBuf buf[RING];
  int n = 0; bool temporal = false;
  int playFrame = 0, nextDecode = 0;   // frame being shown; next frame the decoders should produce
  bool busy = false;                   // a temporal clip is decoded by one task at a time
  uint32_t readUs = 0, lastUse = 0;
};
V3Slot* s_v3 = nullptr;                // SLOTS slots, placed in PSRAM by beginDecoders()
V3Slot* SlotRange::begin() const { return s_v3; }
V3Slot* SlotRange::end() const { return s_v3 ? s_v3 + SLOTS : s_v3; }
V3Scratch s_scratch[2];                 // one per decoder task, internal RAM (the model is hot)
SemaphoreHandle_t s_mx = nullptr;      // slot bookkeeping
SemaphoreHandle_t s_fileMx = nullptr;  // decoder file reads vs closeAll()
SemaphoreHandle_t s_kick = nullptr;    // "there may be work"

int v3Read(void* ctx, uint32_t offset, uint8_t* buf, uint32_t len) {
  V3Slot* sl = (V3Slot*)ctx;
  xSemaphoreTake(s_fileMx, portMAX_DELAY);
  int got = -1;
  if (sl->f && sl->f.seek(offset)) {
    const uint32_t r0 = micros();
    got = sl->f.read(buf, len);
    sl->readUs += micros() - r0;
  }
  xSemaphoreGive(s_fileMx);
  return got;
}

uint32_t s_internalFallbacks = 0;   // frame buffers that had to come from internal RAM (PSRAM full?)
uint8_t* psAlloc(uint8_t* old, uint32_t& have, uint32_t need) {
  if (have >= need) return old;
  free(old);
  uint8_t* p = (uint8_t*)heap_caps_malloc(need, MALLOC_CAP_SPIRAM);
  if (!p) { p = (uint8_t*)malloc(need); ++s_internalFallbacks; }
  have = p ? need : 0;
  return p;
}

// Slot for a clip (s_mx held): existing, or the least recently used idle one.
V3Slot* acquire(const AnimInfo& a) {
  for (V3Slot& sl : SlotRange()) if (sl.id == a.id) return &sl;
  V3Slot* best = nullptr;
  for (V3Slot& sl : SlotRange()) {
    bool idle = !sl.busy;
    for (auto& b : sl.buf) if (b.state == 1) idle = false;
    if (idle && (!best || sl.lastUse < best->lastUse)) best = &sl;
  }
  if (!best) return nullptr;
  V3Slot& sl = *best;
  sl.id = "";
  dbgRenderStage = 20;
  xSemaphoreTake(s_fileMx, portMAX_DELAY);
  dbgRenderStage = 21;
  if (sl.f) sl.f.close();
  sl.f = LittleFS.open(pathFor(a.id), "r");
  xSemaphoreGive(s_fileMx);
  dbgRenderStage = 22;
  if (!sl.f) return nullptr;
  const uint32_t need = (uint32_t)a.w * a.h;
  sl.prev = psAlloc(sl.prev, sl.prevSize, need);
  for (auto& b : sl.buf) { uint32_t have = b.px ? sl.pxSize : 0; b.px = psAlloc(b.px, have, need); if (!b.px) return nullptr; b.state = 0; b.frame = -1; }
  sl.pxSize = need;
  sl.id = a.id;
  dbgRenderStage = 23;
  if (!v3Begin(sl.r, v3Read, &sl, sl.prev, sl.prevSize)) { sl.id = ""; return nullptr; }
  dbgRenderStage = 24;
  sl.n = a.delays.size(); sl.temporal = (sl.r.flags & 1) != 0;
  sl.playFrame = sl.nextDecode = 0; sl.busy = false;
  return &sl;
}

struct RowCopy { uint8_t* px; int w; static void emit(void* ctx, int y, const uint8_t* row) { RowCopy& c = *(RowCopy*)ctx; memcpy(c.px + (size_t)y * c.w, row, c.w); } };

// Pick a frame to decode (s_mx held). Returns false when nothing is pending.
bool pickWork(V3Slot*& outSlot, FrameBuf*& outBuf, int& outFrame) {
  for (V3Slot& sl : SlotRange()) {
    if (!sl.id.length() || sl.n == 0 || (sl.temporal && sl.busy)) continue;
    for (int guard = 0; guard < RING + 1; ++guard) {
      const int ahead = (sl.nextDecode - sl.playFrame + sl.n) % sl.n;
      if (ahead > AHEAD) break;
      FrameBuf* freeBuf = nullptr; bool have = false;
      for (auto& b : sl.buf) { if (b.state && b.frame == sl.nextDecode) have = true; else if (!b.state && !freeBuf) freeBuf = &b; }
      if (have) { sl.nextDecode = (sl.nextDecode + 1) % sl.n; continue; }   // already in the ring
      if (!freeBuf) break;                                                   // ring full
      outSlot = &sl; outBuf = freeBuf; outFrame = sl.nextDecode;
      freeBuf->state = 1; freeBuf->frame = outFrame;
      sl.nextDecode = (outFrame + 1) % sl.n;
      if (sl.temporal) sl.busy = true;
      return true;
    }
  }
  return false;
}

void decoderTask(void* arg) {
  V3Scratch& sc = *(V3Scratch*)arg;
  volatile int& stage = dbgDecStage[&sc == &s_scratch[0] ? 0 : 1];
  for (;;) {
    stage = 1;
    xSemaphoreTake(s_kick, pdMS_TO_TICKS(50));
    for (;;) {
      V3Slot* sl = nullptr; FrameBuf* b = nullptr; int frame = -1;
      stage = 2;
      xSemaphoreTake(s_mx, portMAX_DELAY);
      stage = 3;
      const bool got = pickWork(sl, b, frame);
      xSemaphoreGive(s_mx);
      if (!got) break;
      stage = 4;
      sl->readUs = 0;
      const uint32_t t0 = micros();
      RowCopy ctx{b->px, sl->r.w};
      const bool ok = v3DecodeFrame(sl->r, sc, frame, RowCopy::emit, &ctx);
      const uint32_t total = micros() - t0;
      animPerf.add(0, sl->readUs, total - sl->readUs, total, frame);
      stage = 5;
      xSemaphoreTake(s_mx, portMAX_DELAY);
      b->state = (ok && sl->id.length()) ? 2 : 0;
      if (sl->temporal) sl->busy = false;
      xSemaphoreGive(s_mx);
      stage = 6;
    }
  }
}

// Render core: paint `frame` of a version 3 clip if the decoders have it.
bool paintV3(const AnimInfo& a, int frame, Canvas& c, int x0, int y0, bool clipCircle, const Rgb2* map) {
  dbgRenderStage = 10;
  xSemaphoreTake(s_mx, portMAX_DELAY);
  dbgRenderStage = 11;
  V3Slot* sl = acquire(a);
  dbgRenderStage = 12;
  if (!sl) { xSemaphoreGive(s_mx); return false; }
  sl->lastUse = millis(); sl->playFrame = frame;
  const int n = sl->n;
  if ((sl->nextDecode - frame + n) % n > AHEAD + 1) {   // the pipeline is somewhere else: restart it here
    sl->nextDecode = frame;
    for (auto& b : sl->buf) if (b.state == 2) b.state = 0;
  }
  FrameBuf* hit = nullptr;
  for (auto& b : sl->buf) {
    if (b.state != 2) continue;
    if (b.frame == frame) hit = &b;
    else { const int d = (frame - b.frame + n) % n; if (d > 0 && d <= n / 2) b.state = 0; }   // behind the play position: stale
  }
  xSemaphoreGive(s_mx);
  xSemaphoreGive(s_kick);
  dbgRenderStage = 13;
  if (!hit) return false;
  // A ready buffer whose frame is the play frame is never recycled, so it is
  // safe to read it without the lock.
  static Rgb2 row[128];
  const uint8_t* px = hit->px;
  for (int y = 0; y < a.h; ++y, px += a.w) {
    for (int x = 0; x < a.w; ++x) row[x] = map[px[x] & 127];
    int xs = 0, xe = a.w;
    if (clipCircle) circleSpan(y, xs, xe);
    if (xe > xs) c.setRow(x0 + xs, y0 + y, row + xs, xe - xs);
  }
  dbgRenderStage = 14;
  return true;
}

void v3Forget() {   // s_mx held by the caller (closeAll)
  for (V3Slot& sl : SlotRange()) { sl.id = ""; sl.n = 0; }
}
}  // namespace

static void closeSlotFiles() { for (V3Slot& sl : SlotRange()) if (sl.f) sl.f.close(); }   // s_fileMx held

uint32_t internalFallbacks() { return s_internalFallbacks; }

void beginDecoders() {
  void* mem = heap_caps_malloc(sizeof(V3Slot) * SLOTS, MALLOC_CAP_SPIRAM);   // ~23 KB: the priors are read once per frame, PSRAM is fine
  if (!mem) mem = malloc(sizeof(V3Slot) * SLOTS);
  s_v3 = new (mem) V3Slot[SLOTS];
  s_mx = xSemaphoreCreateMutex();
  s_fileMx = xSemaphoreCreateMutex();
  s_kick = xSemaphoreCreateCounting(8, 0);
  xTaskCreatePinnedToCore(decoderTask, "anim0", 6144, &s_scratch[0], 1, nullptr, 0);
  xTaskCreatePinnedToCore(decoderTask, "anim1", 6144, &s_scratch[1], 1, nullptr, 1);
}

// ---- timing of one version 3 frame, in isolation (own reader, own file)
struct BenchCtx { File* f; std::vector<uint8_t>* img; uint32_t us; };
static int benchRead(void* ctx, uint32_t offset, uint8_t* buf, uint32_t len) {
  BenchCtx& b = *(BenchCtx*)ctx;
  if (b.img) {
    if (offset >= b.img->size()) return -1;
    const uint32_t n = min<uint32_t>(len, b.img->size() - offset);
    memcpy(buf, &(*b.img)[offset], n);
    return (int)n;
  }
  if (!b.f->seek(offset)) return -1;
  const uint32_t r0 = micros();
  const int got = b.f->read(buf, len);
  b.us += micros() - r0;
  return got;
}

BenchResult bench(const AnimInfo& a, int frame) {
  BenchResult b = {0, 0, 0, 0, 0, 0, false};
  if (a.version != 3 || !a.ok() || frame < 0 || frame >= (int)a.delays.size()) return b;
  File f = LittleFS.open(pathFor(a.id), "r");
  if (!f) return b;
  V3Reader* r = (V3Reader*)heap_caps_malloc(sizeof(V3Reader), MALLOC_CAP_SPIRAM);
  V3Scratch* sc = (V3Scratch*)heap_caps_malloc(sizeof(V3Scratch), MALLOC_CAP_INTERNAL);
  if (!sc) sc = (V3Scratch*)heap_caps_malloc(sizeof(V3Scratch), MALLOC_CAP_SPIRAM);
  uint8_t* prev = (uint8_t*)heap_caps_malloc((uint32_t)a.w * a.h, MALLOC_CAP_SPIRAM);
  BenchCtx ctx{&f, nullptr, 0};
  if (r && sc && prev && v3Begin(*r, benchRead, &ctx, prev, (uint32_t)a.w * a.h)) {
    const uint32_t end = (size_t)frame + 1 < a.offsets.size() ? a.offsets[frame + 1] : (uint32_t)f.size();
    b.bytes = end - a.offsets[frame];
    static uint8_t tmp[2048];
    uint32_t t0 = micros();
    f.seek(a.offsets[frame]);
    for (uint32_t left = b.bytes; left > 0;) { const int got = f.read(tmp, min<uint32_t>(sizeof(tmp), left)); if (got <= 0) break; left -= got; }
    b.fileRead = micros() - t0;
    t0 = micros(); v3DecodeFrame(*r, *sc, frame, nullptr, nullptr); b.decodeNoEmit = micros() - t0;
    r->lastFrame = -1;
    t0 = micros(); v3DecodeFrame(*r, *sc, frame, nullptr, nullptr); b.decodeNoEmitWarm = micros() - t0;
    t0 = micros();
    for (int i = 0; i < V3_STATES; ++i) sc->model.st[i] = (uint16_t)((r->prior[i] << 8) | 3);
    b.modelReload = micros() - t0;
    std::vector<uint8_t> img(f.size());
    f.seek(0); f.read(img.data(), img.size());
    ctx.img = &img;
    r->lastFrame = -1;
    t0 = micros(); v3DecodeFrame(*r, *sc, frame, nullptr, nullptr); b.decodeFromRam = micros() - t0;
    b.ok = true;
  }
  free(prev); free(sc); free(r);
  f.close();
  return b;
}

bool drawFrame(const AnimInfo& a, int frame, Canvas& c, int x0, int y0, bool clipCircle,
               uint8_t mode, Rgb2 fg, Rgb2 bg) {
  if (!a.ok() || frame < 0 || frame >= (int)a.delays.size() || a.w > 128) return true;   // nothing to wait for
  const uint32_t t0 = micros();
  if (a.version == 3) {
    Rgb2 map[128];
    for (int v = 0; v < 128; ++v)
      map[v] = (mode == ANIM_ORIGINAL) ? (Rgb2)(v & 63) : ((((v & 64) != 0) != (mode == ANIM_MONO_INV)) ? fg : bg);
    return paintV3(a, frame, c, x0, y0, clipCircle, map);
  }
  File* fp = openCached(a.id);
  if (!fp || !fp->seek(a.offsets[frame])) return true;
  File& f = *fp;
  const uint32_t tOpen = micros() - t0;
  uint32_t tRead = 0, tInf = 0;

  // Colour of every possible pixel byte for this mode, then whole rows out.
  Rgb2 map[128];
  for (int v = 0; v < 128; ++v)
    map[v] = (mode == ANIM_ORIGINAL) ? (Rgb2)(v & 63) : ((((v & 64) != 0) != (mode == ANIM_MONO_INV)) ? fg : bg);
  static Rgb2 row[128];
  int x = 0, y = 0;
  auto emit = [&](uint8_t px, int count) {
    while (count-- > 0 && y < a.h) {
      row[x++] = map[px & 127];
      if (x == a.w) {
        int xs = 0, xe = a.w;
        if (clipCircle) circleSpan(y, xs, xe);
        if (xe > xs) c.setRow(x0 + xs, y0 + y, row + xs, xe - xs);
        x = 0; ++y;
      }
    }
  };

  const uint32_t end = (size_t)frame + 1 < a.offsets.size() ? a.offsets[frame + 1] : (uint32_t)f.size();
  uint32_t left = end - a.offsets[frame];
  if (a.version == 2) {
    // Static: render core only. The ring must be a power of two and at least
    // the compressor's window (1 KB, see tools/anim_convert.py).
    static tinfl_decompressor inf;
    static uint8_t in[2048], ring[1024];
    size_t inPos = 0, inAvail = 0, outPos = 0;
    tinfl_init(&inf);
    while (y < a.h) {
      if (inAvail == 0 && left > 0) {
        const uint32_t r0 = micros();
        const int got = f.read(in, min<uint32_t>(sizeof(in), left));
        tRead += micros() - r0;
        if (got <= 0) break;
        left -= got; inPos = 0; inAvail = got;
      }
      size_t inBytes = inAvail, outBytes = sizeof(ring) - outPos;
      const uint32_t i0 = micros();
      const tinfl_status st = tinfl_decompress(&inf, in + inPos, &inBytes, ring, ring + outPos, &outBytes,
                                               left > 0 ? TINFL_FLAG_HAS_MORE_INPUT : 0);
      tInf += micros() - i0;
      inPos += inBytes; inAvail -= inBytes;
      for (size_t k = 0; k < outBytes; ++k) emit(ring[outPos + k], 1);
      outPos = (outPos + outBytes) & (sizeof(ring) - 1);
      if (st == TINFL_STATUS_DONE || st < 0) break;
      if (st == TINFL_STATUS_NEEDS_MORE_INPUT && inAvail == 0 && left == 0) break;
    }
  } else {
    static uint8_t buf[1024];
    while (y < a.h && left > 0) {
      const uint32_t r0 = micros();
      const int got = f.read(buf, min<uint32_t>(sizeof(buf), left));
      tRead += micros() - r0;
      if (got < 2) break;
      const int pairs = got / 2;
      left -= pairs * 2;
      if (got & 1) f.seek(f.position() - 1);   // keep pairs whole
      for (int i = 0; i < pairs; ++i) emit(buf[2 * i + 1], buf[2 * i]);
    }
  }
  animPerf.add(tOpen, tRead, tInf, micros() - t0, frame);
  return true;
}

}  // namespace AnimStore
