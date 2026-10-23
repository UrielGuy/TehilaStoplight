#include "SerialLink.h"
#include "AccessPoint.h"
#include "AnimStore.h"
#include "StopLightApp.h"
#include <esp_rom_crc.h>
#include <mbedtls/base64.h>
#include <esp_heap_caps.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <vector>

static const char* TMP = "/a/.serial";
uint32_t bootHeap[5] = {0, 0, 0, 0, 0};   // free internal heap at start-up milestones (main.cpp)

#ifndef MP3_TEST   // the MP3 bring-up build has no store, display or player
extern SetStore store;
extern SnakeDisplay display;
extern Mp3Player mp3;
#endif

// Sets JSON (up to 48 KB) and its parsed document live in PSRAM: the internal
// heap is ~70 KB when idle.
static uint8_t* bigAlloc(size_t n) {
  void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return (uint8_t*)(p ? p : malloc(n));
}
struct PsramJsonAllocator : ArduinoJson::Allocator {
  void* allocate(size_t n) override { return bigAlloc(n); }
  void deallocate(void* p) override { free(p); }
  void* reallocate(void* p, size_t n) override {
    void* q = heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return q ? q : realloc(p, n);
  }
};

// One write: a reply line never interleaves with the log.
void SerialLink::reply(const String& msg) { _s.print("#ANIM " + msg + "\n"); }

void SerialLink::dropUpload() {
  if (_f) _f.close();
  if (!_upSets) LittleFS.remove(TMP);
  if (_setsBuf) { free(_setsBuf); _setsBuf = nullptr; }
  _up = _inPacket = _upSets = false;
}

void SerialLink::dropSnapshot() {
  if (_snap) free(_snap);
  _snap = nullptr; _snapLen = 0;
}

void SerialLink::sendBlock(unsigned long off, const uint8_t* raw, int got) {
  unsigned char b64[1040]; size_t olen = 0;
  mbedtls_base64_encode(b64, sizeof(b64) - 1, &olen, raw, got);
  b64[olen] = 0;
  char head[64];
  snprintf(head, sizeof(head), "BLK %lu %d %08lx ", off, got, (unsigned long)esp_rom_crc32_le(0, raw, got));
  reply(String(head) + (const char*)b64);
}

void SerialLink::packetDone() {
  _inPacket = false;
  if (esp_rom_crc32_le(0, _buf, _pLen) != _pCrc || _pOff > _got) { reply("NAK " + String(_got)); return; }
  if (_pOff + _pLen <= _got) { reply("ACK " + String(_got)); return; }   // a resend of data we have (our ACK was lost)
  if (_pOff != _got) { reply("NAK " + String(_got)); return; }
  if (_upSets) {
    if (_got + _pLen > _size) { dropUpload(); reply("ERR more data than announced"); return; }
    memcpy(_setsBuf + _got, _buf, _pLen);
  } else if (_f.write(_buf, _pLen) != _pLen) { dropUpload(); reply("ERR storage full"); return; }
  _lastAt = millis();   // a write near the end of the space can take seconds
  _crc = esp_rom_crc32_le(_crc, _buf, _pLen);
  _got += _pLen;
  reply("ACK " + String(_got));
}

void SerialLink::poll() {
  const uint32_t now = millis();
  if (_up && now - _lastAt > 60000) { dropUpload(); reply("ERR timeout"); }
  if (_snap && now - _snapAt > 120000) dropSnapshot();
  if (_inPacket) {
    int avail;
    while (_inPacket && (avail = _s.available()) > 0) {
      const int n = _s.readBytes(_buf + _pHave, min<int>(avail, _pLen - _pHave));
      if (n <= 0) break;
      _pHave += n; _pAt = _lastAt = millis();
      if (_pHave == _pLen) packetDone();
    }
    if (_inPacket && millis() - _pAt > 1000) { _inPacket = false; reply("NAK " + String(_got)); }
    if (_inPacket) return;
  }
  while (_s.available() > 0) {
    const char ch = (char)_s.read();
    if (ch == '\n' || ch == '\r') {
      if (_line.startsWith("#ANIM ")) command(_line.substring(6));
      _line = "";
      if (_inPacket) return;   // the following bytes are packet data
    } else if (_line.length() < 160) _line += ch;
    else _line = "";
  }
}

void SerialLink::command(const String& line) {
  _lastAt = millis();
  if (line.startsWith("DATA ")) {
    if (!_up) { reply("ERR no upload open"); return; }
    unsigned long off = 0, len = 0, crc = 0;
    if (sscanf(line.c_str() + 5, "%lu %lu %lx", &off, &len, &crc) != 3 || len == 0 || len > MAX_PACKET) { reply("NAK " + String(_got)); return; }
    _pOff = off; _pLen = len; _pCrc = crc; _pHave = 0; _pAt = millis();
    _inPacket = true;
  } else if (line == "POS") {
    reply("POS " + String(_got));
  } else if (line.startsWith("PUT ")) {
    dropUpload();
    const int sp = line.lastIndexOf(' ');
    _id = AnimStore::sanitizeId(line.substring(4, sp));
    _size = line.substring(sp + 1).toInt();
    if (_size < 12 || _size > 4 * 1024 * 1024) { reply("ERR bad size"); return; }
    // Replacing a clip that would not fit next to its old copy: delete the old
    // copy first (the PC asked before replacing). Writing into a full file
    // system stalls LittleFS for ~30 s, so never start an upload that cannot fit.
    if (_size + 8192 > AnimStore::freeBytes() && AnimStore::exists(_id)) {
      _app.lockPlayback(); AnimStore::remove(_id); _app.unlockPlayback();
      _app.renderCurrent();
    }
    if (_size + 8192 > AnimStore::freeBytes()) { reply("ERR storage full (" + String(AnimStore::freeBytes() / 1024) + " KB free)"); return; }
    _f = LittleFS.open(TMP, "w");
    if (!_f) { reply("ERR cannot write to storage"); return; }
    _up = true; _got = 0; _crc = 0;
    reply("READY");
  } else if (line.startsWith("END ")) {
    if (!_up) { reply("ERR no upload open"); return; }
    const uint32_t want = strtoul(line.substring(4).c_str(), nullptr, 16);
    if (_got != _size) { reply("ERR incomplete (" + String(_got) + " of " + String(_size) + ")"); return; }
    if (_upSets) {
      if (want != _crc) { dropUpload(); reply("ERR checksum mismatch"); return; }
      finishSets();
      return;
    }
    _f.close(); _up = false;
    String err;
    if (want != _crc) err = "checksum mismatch";
    else AnimStore::validate(TMP, err);
    if (!err.isEmpty()) { LittleFS.remove(TMP); reply("ERR " + err); return; }
    _app.lockPlayback();
    AnimStore::closeAll();
    LittleFS.remove(AnimStore::pathFor(_id));
    LittleFS.rename(TMP, AnimStore::pathFor(_id));
    _app.unlockPlayback();
    _app.renderCurrent();
    reply("OK " + String(AnimStore::freeBytes()));
  } else if (line == "PERF" || line == "PERF RESET") {
    const AnimPerf& p = animPerf; const auto& q = _app.playPerf;
    const uint32_t n = p.frames ? p.frames : 1;
    String o = "{\"frames\":" + String(p.frames) + ",\"avgMs\":{\"open\":" + String(p.usOpen / n / 1000.0, 2) +
               ",\"read\":" + String(p.usRead / n / 1000.0, 2) + ",\"inflate\":" + String(p.usInflate / n / 1000.0, 2) +
               ",\"total\":" + String(p.usTotal / n / 1000.0, 2) + "},\"maxMs\":{\"open\":" + String(p.maxOpen / 1000.0, 1) +
               ",\"read\":" + String(p.maxRead / 1000.0, 1) + ",\"inflate\":" + String(p.maxInflate / 1000.0, 1) +
               ",\"pixels\":" + String(p.maxPixels / 1000.0, 1) + ",\"total\":" + String(p.maxTotal / 1000.0, 1) +
               "},\"worst\":{\"frame\":" + String(p.worstFrame) + ",\"open\":" + String(p.worstParts[0] / 1000.0, 1) +
               ",\"read\":" + String(p.worstParts[1] / 1000.0, 1) + ",\"inflate\":" + String(p.worstParts[2] / 1000.0, 1) +
               ",\"pixels\":" + String(p.worstParts[3] / 1000.0, 1) + "},\"hist\":[";
    for (int i = 0; i < 8; ++i) o += String(p.hist[i]) + (i < 7 ? "," : "]");
    o += ",\"shown\":" + String(q.shown) + ",\"skipped\":" + String(q.skipped) + ",\"late\":" + String(q.late) +
         ",\"maxGapMs\":" + String(q.maxGapMs) + ",\"maxLoopMs\":" + String(q.maxLoopUs / 1000.0, 1) +
         ",\"maxLockWaitMs\":" + String(q.maxLockWaitUs / 1000.0, 1) + ",\"heap\":" + String(ESP.getFreeHeap()) +
         ",\"psram\":" + String(ESP.getPsramSize()) + ",\"psramFree\":" + String(ESP.getFreePsram()) +
         ",\"heapLargest\":" + String(ESP.getMaxAllocHeap()) + ",\"uptimeMs\":" + String(millis()) +
         ",\"internalFallbacks\":" + String(AnimStore::internalFallbacks()) + ",\"renderBeat\":" + String(AnimStore::dbgRenderBeat) +
         ",\"renderStage\":" + String(AnimStore::dbgRenderStage) + ",\"decStage\":[" + String(AnimStore::dbgDecStage[0]) + "," + String(AnimStore::dbgDecStage[1]) + "]" +
         ",\"cur\":" + _app.debugCurrent() + ",\"heapTotal\":" + String(heap_caps_get_total_size(MALLOC_CAP_INTERNAL)) +
         ",\"bootHeap\":[" + String(bootHeap[0]) + "," + String(bootHeap[1]) + "," + String(bootHeap[2]) + "," + String(bootHeap[3]) + "," + String(bootHeap[4]) + "]}";
    o = o.substring(0, o.length() - 1) + ",\"web\":" + AccessPoint::webStats(line == "PERF RESET") + "}";
    if (line == "PERF RESET") { animPerf = AnimPerf(); _app.playPerf = StopLightApp::PlayPerf(); }
    reply("PERF " + o);
  } else if (line.startsWith("LOADTEST ")) {   // load an animation's header twice, report
    AnimInfo a, b;
    const String id = AnimStore::sanitizeId(line.substring(9));
    const bool ok1 = AnimStore::load(id, a), ok2 = AnimStore::load(id, b);
    reply("LOADTEST " + id + " " + String(ok1) + " " + String(ok2) + " frames " + String(a.delays.size()) + " ver " + String(a.version) + " heap " + String(ESP.getFreeHeap()));
  } else if (line.startsWith("BENCH ")) {   // BENCH <id> <frame>: decode timing of one version-3 frame
    char name[40]; int frame = 0;
    if (sscanf(line.c_str() + 6, "%39s %d", name, &frame) < 1) { reply("ERR bad request"); return; }
    AnimInfo a;
    if (!AnimStore::load(AnimStore::sanitizeId(name), a)) { reply("ERR no such animation"); return; }
    _app.lockPlayback();
    const AnimStore::BenchResult b = AnimStore::bench(a, frame);
    _app.unlockPlayback();
    if (!b.ok) { reply("ERR not a version 3 clip"); return; }
    reply("BENCH {\"bytes\":" + String(b.bytes) + ",\"fileReadMs\":" + String(b.fileRead / 1000.0, 2) + ",\"decodeMs\":" + String(b.decodeNoEmit / 1000.0, 2) +
          ",\"decodeWarmMs\":" + String(b.decodeNoEmitWarm / 1000.0, 2) + ",\"modelReloadMs\":" + String(b.modelReload / 1000.0, 2) +
          ",\"decodeFromRamMs\":" + String(b.decodeFromRam / 1000.0, 2) + "}");
  } else if (line.startsWith("PLAY ")) {   // test hook: show an animation as a live draft (clears after 60 s)
    KeyFrame f;
    String rest = line.substring(5);
    for (int i = 0; i < 3 && rest.length(); ++i) {   // "a,b,c" = top, middle, bottom
      const int comma = rest.indexOf(',');
      const String id = comma >= 0 ? rest.substring(0, comma) : rest;
      rest = comma >= 0 ? rest.substring(comma + 1) : "";
      if (id.length()) { f.l[i].graphic = "u:" + AnimStore::sanitizeId(id); f.l[i].kind = "short"; f.l[i].bg = 0; f.l[i].ink = 63; }
    }
    _app.setDraft(f);
    reply("OK playing");
  } else if (line == "STOP") {
    _app.clearDraft();
    reply("OK stopped");
  } else if (line == "STA" || line.startsWith("STA ")) {   // debugging: also join a WiFi network (see AccessPoint.h)
    reply("STA " + AccessPoint::station(line.length() > 4 ? line.substring(4) : String()));
  } else if (line.startsWith("PROBES")) {   // debugging: how phones' connectivity checks are answered (see AccessPoint.h)
    if (line.length() > 7) AccessPoint::probesOnline(line.substring(7) == "online" ? 1 : 0);
    reply(String("PROBES ") + (AccessPoint::probesOnline() ? "online" : "captive"));
  } else if (line.startsWith("WEBLOG")) {   // debugging: log every web request (see AccessPoint.h)
    reply(String("WEBLOG ") + (AccessPoint::webLog(line.endsWith("0") ? 0 : 1) ? "on" : "off"));
  } else if (line == "SPACE") {
    reply("SPACE " + String(AnimStore::freeBytes()) + " " + String(AnimStore::totalBytes()));
  } else if (line == "LIST") {
    String out = "[";
    for (const auto& e : AnimStore::list()) {
      if (out.length() > 1) out += ",";
      out += "{\"id\":\"" + e.id + "\",\"full\":" + (e.h > e.w ? "true" : "false") + ",\"frames\":" + e.frames + ",\"ms\":" + e.ms + ",\"bytes\":" + e.bytes + "}";
    }
    reply("LIST " + out + "]");
  } else if (line.startsWith("GET ")) {
    char name[40]; unsigned long off = 0, len = 0;
    if (sscanf(line.c_str() + 4, "%39s %lu %lu", name, &off, &len) != 3 || len == 0 || len > 768) { reply("ERR bad request"); return; }
    if (strcmp(name, ".sets") == 0) {   // the SETS snapshot (no animation id starts with a dot)
      if (!_snap) { reply("ERR no snapshot (send SETS first)"); return; }
      _snapAt = millis();
      const int got = off >= _snapLen ? 0 : (int)min<unsigned long>(len, _snapLen - off);
      sendBlock(off, _snap + (got ? off : 0), got);
      return;
    }
    File f = LittleFS.open(AnimStore::pathFor(AnimStore::sanitizeId(name)), "r");
    if (!f) { reply("ERR no such animation"); return; }
    uint8_t raw[768];
    int got = 0;
    if (f.seek(off)) got = f.read(raw, len);
    f.close();
    if (got < 0) got = 0;
    sendBlock(off, raw, got);
  } else if (line.startsWith("SUM ")) {   // size and CRC of a stored file (the PC diffs before uploading)
    const String id = AnimStore::sanitizeId(line.substring(4));
    File f = LittleFS.open(AnimStore::pathFor(id), "r");
    if (!f) { reply("ERR no such animation"); return; }
    uint32_t crc = 0, n = 0;
    int got;
    while ((got = f.read(_buf, sizeof(_buf))) > 0) { crc = esp_rom_crc32_le(crc, _buf, got); n += got; }
    f.close();
    char hex[12];
    snprintf(hex, sizeof(hex), "%08lx", (unsigned long)crc);
    reply("SUM " + id + " " + String(n) + " " + hex);
#ifndef MP3_TEST
  } else if (line == "SETS") {
    dropSnapshot();
    _app.lockPlayback();
    const String js = store.toJson();
    _app.unlockPlayback();
    _snap = bigAlloc(js.length() + 1);
    if (!_snap) { reply("ERR out of memory"); return; }
    memcpy(_snap, js.c_str(), js.length());
    _snapLen = js.length(); _snapAt = millis();
    char hex[12];
    snprintf(hex, sizeof(hex), "%08lx", (unsigned long)esp_rom_crc32_le(0, _snap, _snapLen));
    reply("SETS " + String(_snapLen) + " " + hex);
  } else if (line.startsWith("SPUT ")) {
    dropUpload();
    _size = line.substring(5).toInt();
    if (_size < 2 || _size > MAX_SETS_UPLOAD) { reply("ERR bad size (at most " + String(MAX_SETS_UPLOAD) + " bytes)"); return; }
    _setsBuf = bigAlloc(_size + 1);
    if (!_setsBuf) { reply("ERR out of memory"); return; }
    _up = _upSets = true; _got = 0; _crc = 0;
    reply("READY");
  } else if (line == "CONF" || line.startsWith("CONF ")) {
    conf(line.length() > 5 ? line.substring(5) : String());
#endif
  } else if (line.startsWith("DEL ")) {
    _app.lockPlayback(); AnimStore::remove(AnimStore::sanitizeId(line.substring(4))); _app.unlockPlayback();
    _app.renderCurrent();
    reply("OK " + String(AnimStore::freeBytes()));
  } else {
    reply("ERR unknown command");
  }
}

#ifndef MP3_TEST
// The checks WebUI::checkLights makes for the page's Add (keep in step).
static bool lightsOk(const LightDef l[3], String& err) {
  static const char* names[3] = {"top", "middle", "bottom"};
  for (int i = 0; i < 3; ++i) {
    if (l[i].graphic.length()) {
      if (l[i].graphic.startsWith("u:")) {
        if (!AnimStore::exists(l[i].graphic.substring(2))) { err = String(names[i]) + ": animation " + l[i].graphic.substring(2) + " not on the sign"; return false; }
      } else if (!Face::spriteById(l[i].graphic.c_str()) && !Face::effectById(l[i].graphic.c_str())) { err = String(names[i]) + ": unknown graphic " + l[i].graphic; return false; }
      continue;
    }
    if (l[i].text.length() > SetStore::MAX_TEXT_BYTES) { err = String(names[i]) + ": text too long"; return false; }
    String t = l[i].text; t.trim();
    if (t.isEmpty()) continue;
    TextLayout L = Face::layoutFor(i, l[i].text.c_str(), l[i].size);
    if (!L.ok()) { err = String(names[i]) + ": " + L.error.c_str(); return false; }
  }
  return true;
}

void SerialLink::finishSets() {
  String err;
  std::vector<TextSet> sets;
  {
    PsramJsonAllocator alloc;
    JsonDocument doc(&alloc);
    if (deserializeJson(doc, (const char*)_setsBuf, _size)) err = "bad json";
    else if (!doc["sets"].is<JsonArrayConst>()) err = "no sets array";
    else if (doc["sets"].size() > SetStore::MAX_SETS) err = "at most " + String(SetStore::MAX_SETS) + " sets";
    else {
      int k = 0;
      for (JsonObjectConst o : doc["sets"].as<JsonArrayConst>()) {
        ++k;
        TextSet s;
        setFromJson(o, s);
        const String where = "set " + String(k) + (s.name.length() ? " (" + s.name + ")" : String()) + ", ";
        if (s.frames.empty()) { err = where + "no frames"; break; }
        if (o["frames"].size() > SetStore::MAX_FRAMES) { err = where + "at most " + String(SetStore::MAX_FRAMES) + " frames"; break; }
        for (size_t f = 0; f < s.frames.size() && err.isEmpty(); ++f) {
          String e;
          if (!lightsOk(s.frames[f].l, e)) err = where + "frame " + String(f + 1) + ", " + e;
        }
        if (!err.isEmpty()) break;
        sets.push_back(s);
      }
    }
  }
  dropUpload();
  if (!err.isEmpty()) { reply("ERR " + err); return; }
  _app.lockPlayback();   // the render core reads the sets while preparing frames
  _app.clearDraft();
  std::swap(store.sets(), sets);
  const bool saved = store.save();
  if (!saved) std::swap(store.sets(), sets);   // the old sets back, nothing written
  else _app.onSetsChanged();
  const size_t n = store.sets().size();
  _app.unlockPlayback();
  if (!saved) { reply("ERR storage full: the sets take more than " + String(SetStore::MAX_JSON_BYTES / 1000) + " KB once stored"); return; }
  reply("OK " + String(n) + " " + String(store.toJson().length()));
}

// Global settings, as POST /api/settings and /api/mp3/volume do (same NVS keys).
void SerialLink::conf(const String& json) {
  bool restart = false, redraw = false, colrot = display.colourRotated();
  if (json.length()) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) { reply("ERR bad json"); return; }
    Preferences p;
    p.begin("stoplight", false);
    if (doc["offDark"].is<bool>()) {
      const bool dark = doc["offDark"].as<bool>();
      Face::setOffDark(dark); p.putBool("offdark", dark); redraw = true;
    }
    if (doc["rotate180"].is<bool>()) {
      const bool rot = doc["rotate180"].as<bool>();
      SnakeDisplay::setRotated(rot); p.putBool("rot180", rot); redraw = true;
    }
    if (doc["volume"].is<int>()) {
      int v = doc["volume"].as<int>(); v = v < 0 ? 0 : v > 30 ? 30 : v;
      mp3.setVolume(v); p.putUChar("vol", v);
    }
    if (doc["colourRotated"].is<bool>() && doc["colourRotated"].as<bool>() != colrot) {   // takes effect at display init
      colrot = doc["colourRotated"].as<bool>();
      p.putBool("colrot", colrot); restart = true;
    }
    p.end();
    if (redraw) _app.renderCurrent();
  }
  reply(String("CONF {\"offDark\":") + (Face::offDark() ? "true" : "false") + ",\"rotate180\":" + (SnakeDisplay::rotated() ? "true" : "false") +
        ",\"colourRotated\":" + (colrot ? "true" : "false") + ",\"volume\":" + String(mp3.volume()) + ",\"mp3\":" + (mp3.present() ? "true" : "false") +
        ",\"tracks\":" + String(mp3.trackCount()) + (restart ? ",\"restart\":true}" : "}"));
  if (restart) { _s.flush(); delay(300); ESP.restart(); }
}
#else
void SerialLink::finishSets() { dropUpload(); reply("ERR not in this build"); }
void SerialLink::conf(const String&) { reply("ERR not in this build"); }
#endif
