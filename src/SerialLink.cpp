#include "SerialLink.h"
#include "AnimStore.h"
#include "StopLightApp.h"
#include <esp_rom_crc.h>
#include <mbedtls/base64.h>

static const char* TMP = "/a/.serial";
uint32_t bootHeap[5] = {0, 0, 0, 0, 0};   // free internal heap at start-up milestones (main.cpp)

// One write: a reply line never interleaves with the log.
void SerialLink::reply(const String& msg) { _s.print("#ANIM " + msg + "\n"); }

void SerialLink::dropUpload() {
  if (_f) _f.close();
  LittleFS.remove(TMP);
  _up = _inPacket = false;
}

void SerialLink::packetDone() {
  _inPacket = false;
  if (esp_rom_crc32_le(0, _buf, _pLen) != _pCrc || _pOff > _got) { reply("NAK " + String(_got)); return; }
  if (_pOff + _pLen <= _got) { reply("ACK " + String(_got)); return; }   // a resend of data we have (our ACK was lost)
  if (_pOff != _got) { reply("NAK " + String(_got)); return; }
  if (_f.write(_buf, _pLen) != _pLen) { dropUpload(); reply("ERR storage full"); return; }
  _lastAt = millis();   // a write near the end of the space can take seconds
  _crc = esp_rom_crc32_le(_crc, _buf, _pLen);
  _got += _pLen;
  reply("ACK " + String(_got));
}

void SerialLink::poll() {
  const uint32_t now = millis();
  if (_up && now - _lastAt > 60000) { dropUpload(); reply("ERR timeout"); }
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
    File f = LittleFS.open(AnimStore::pathFor(AnimStore::sanitizeId(name)), "r");
    if (!f) { reply("ERR no such animation"); return; }
    uint8_t raw[768];
    int got = 0;
    if (f.seek(off)) got = f.read(raw, len);
    f.close();
    if (got < 0) got = 0;
    unsigned char b64[1040]; size_t olen = 0;
    mbedtls_base64_encode(b64, sizeof(b64) - 1, &olen, raw, got);
    b64[olen] = 0;
    char head[64];
    snprintf(head, sizeof(head), "BLK %lu %d %08lx ", off, got, (unsigned long)esp_rom_crc32_le(0, raw, got));
    reply(String(head) + (const char*)b64);
  } else if (line.startsWith("DEL ")) {
    _app.lockPlayback(); AnimStore::remove(AnimStore::sanitizeId(line.substring(4))); _app.unlockPlayback();
    _app.renderCurrent();
    reply("OK " + String(AnimStore::freeBytes()));
  } else {
    reply("ERR unknown command");
  }
}
