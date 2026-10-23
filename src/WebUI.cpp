#include "WebUI.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include "WebPage.h"
#include "StopLightApp.h"
#include "Preview.h"
#include "Mp3Player.h"
#include <Preferences.h>
#include <LittleFS.h>
#include "AnimStore.h"
#include "AccessPoint.h"
#include <vector>

void WebUI::begin(const char* ssid, const char* password) {
  _canvas = new BitmapCanvas();   // 24 KB, kept for the life of the server
  AccessPoint::begin(ssid, password);   // WiFi + DNS for every name, see AccessPoint.h

  _server.on("/", HTTP_GET, [this] {
    _server.sendHeader("Cache-Control", "no-store");
    _server.send_P(200, "text/html; charset=utf-8", WEB_PAGE);
  });
  _server.on("/api/state", HTTP_GET, [this] { sendState(); });
  _server.on("/api/check", HTTP_GET, [this] { handleCheck(); });
  _server.on("/api/preview.bmp", HTTP_GET, [this] { handlePreview(false); });
  _server.on("/api/preview.rle", HTTP_GET, [this] { handlePreview(true); });
  _server.on("/api/live", HTTP_POST, [this] { handleLive(true); });
  _server.on("/api/live", HTTP_DELETE, [this] { handleLive(false); });
  _server.on("/api/mp3/stop", HTTP_POST, [this] { if (_mp3) _mp3->stop(); sendState(); });
  _server.on("/api/mp3/rescan", HTTP_POST, [this] { if (_mp3) _mp3->rescan(); sendState(); });
  _server.on("/api/mp3/volume", HTTP_POST, [this] {
    JsonDocument doc;
    if (deserializeJson(doc, _server.arg("plain")) || !_mp3) { _server.send(400, "application/json", "{\"error\":\"bad json\"}"); return; }
    int v = doc["volume"] | 20; v = v < 0 ? 0 : v > 30 ? 30 : v;
    _mp3->setVolume(v);
    Preferences p; p.begin("stoplight", false); p.putUChar("vol", v); p.end();
    sendState();
  });
  _server.on("/api/settings", HTTP_POST, [this] {
    JsonDocument doc;
    if (deserializeJson(doc, _server.arg("plain"))) { _server.send(400, "application/json", "{\"error\":\"bad json\"}"); return; }
    if (doc["offDark"].is<bool>()) {
      const bool dark = doc["offDark"].as<bool>();
      Face::setOffDark(dark);
      Preferences p; p.begin("stoplight", false); p.putBool("offdark", dark); p.end();
      _app.renderCurrent();
    }
    if (doc["colourRotated"].is<bool>()) {   // takes effect at display init: save and restart
      Preferences p; p.begin("stoplight", false); p.putBool("colrot", doc["colourRotated"].as<bool>()); p.end();
      _server.send(200, "application/json", "{\"restart\":true}");
      delay(300);
      ESP.restart();
      return;
    }
    if (doc["rotate180"].is<bool>()) {
      const bool rot = doc["rotate180"].as<bool>();
      SnakeDisplay::setRotated(rot);
      Preferences p; p.begin("stoplight", false); p.putBool("rot180", rot); p.end();
      _app.renderCurrent();
    }
    sendState();
  });
  _server.on("/api/sets", HTTP_POST, [this] { handleAdd(); });
  _server.on("/api/anims", HTTP_GET, [this] { sendAnims(); });
  _server.on("/api/anims", HTTP_POST, [this] { handleAnimDone(); }, [this] { handleAnimUpload(); });
  _server.onNotFound([this] {
    const String& uri = _server.uri();
    if (uri.startsWith("/api/sets/") && _server.method() == HTTP_DELETE) { handleDelete(); return; }
    if (uri.startsWith("/api/sets/") && _server.method() == HTTP_PUT) { handleAdd(uri.substring(strlen("/api/sets/")).toInt()); return; }
    if (uri.startsWith("/api/show/") && _server.method() == HTTP_POST)   { handleShow();   return; }
    if (uri.startsWith("/api/anims/") && uri.endsWith("/rename") && _server.method() == HTTP_POST) {
      // Rename a stored animation and point every set that uses it at the new name.
      const String from = AnimStore::sanitizeId(uri.substring(strlen("/api/anims/"), uri.length() - strlen("/rename")));
      JsonDocument doc;
      if (deserializeJson(doc, _server.arg("plain"))) { _server.send(400, "application/json", "{\"error\":\"bad json\"}"); return; }
      const String to = AnimStore::sanitizeId((const char*)(doc["name"] | ""));
      auto fail = [this](const String& e) { JsonDocument d; d["error"] = e; String o; serializeJson(d, o); _server.send(400, "application/json", o); };
      if (!AnimStore::exists(from)) { fail("no such animation"); return; }
      if (to == from) { sendAnims(); return; }
      if (AnimStore::exists(to)) { fail("there is already an animation called " + to); return; }
      _app.lockPlayback();
      AnimStore::closeAll();
      const bool ok = LittleFS.rename(AnimStore::pathFor(from), AnimStore::pathFor(to));
      int changed = 0;
      if (ok)
        for (TextSet& st : _store.sets())
          for (KeyFrame& f : st.frames)
            for (LightDef& l : f.l)
              if (l.graphic == "u:" + from) { l.graphic = "u:" + to; ++changed; }
      if (changed) _store.save();
      _app.unlockPlayback();
      if (!ok) { fail("rename failed"); return; }
      _app.renderCurrent();
      Serial.printf("[anim] renamed '%s' to '%s' (%d uses in sets updated)\n", from.c_str(), to.c_str(), changed);
      sendAnims();
      return;
    }
    if (uri.startsWith("/api/anims/")) {
      String id = uri.substring(strlen("/api/anims/"));
      if (id.endsWith(".anm")) id = id.substring(0, id.length() - 4);
      id = AnimStore::sanitizeId(id);
      if (_server.method() == HTTP_GET) {
        File f = LittleFS.open(AnimStore::pathFor(id), "r");
        if (!f) { _server.send(404, "text/plain", "no such animation"); return; }
        _server.sendHeader("Cache-Control", "no-cache");   // a name can be replaced or renamed
        _server.streamFile(f, "application/octet-stream");
        f.close();
        return;
      }
      if (_server.method() == HTTP_DELETE) {
        _app.lockPlayback(); AnimStore::remove(id); _app.unlockPlayback();
        _app.renderCurrent();
        sendAnims();
        return;
      }
    }
    if (uri.startsWith("/api/mp3/play/") && _server.method() == HTTP_POST) {
      if (_mp3) _mp3->play(uri.substring(strlen("/api/mp3/play/")).toInt());
      sendState(); return;
    }
    if (captiveRedirect()) return;
    _server.send(404, "text/plain", "not found");
  });
  _server.begin();
  Serial.printf("AP '%s' up, open http://%s/\n", ssid, WiFi.softAPIP().toString().c_str());
}

void WebUI::loop() {
  _server.loopOnce();
}

// A request for another host: the connectivity checks of phones and laptops
// get the "online" answer (AccessPoint.h says why), anything else a redirect
// to the page, so any http:// address typed in the browser lands here.
bool WebUI::captiveRedirect() {
  const String host = _server.hostHeader();
  const String ip = WiFi.softAPIP().toString();
  if (host == ip) return false;
  if (AccessPoint::answerProbe(_server)) return true;
  _server.sendHeader("Location", "http://" + ip + "/", true);
  _server.send(302, "text/plain", "");
  return true;
}

void WebUI::sendState() {
  JsonDocument doc;
  doc["current"] = _app.current();
  doc["live"] = _app.hasDraft();
  doc["frame"] = _app.frameIndex();
  JsonObject mp3 = doc["mp3"].to<JsonObject>();
  mp3["present"] = _mp3 && _mp3->present();
  mp3["tracks"] = _mp3 ? _mp3->trackCount() : 0;
  mp3["volume"] = _mp3 ? _mp3->volume() : 0;
  mp3["error"] = _mp3 ? _mp3->lastError() : "";
  doc["settings"]["offDark"] = Face::offDark();
  doc["settings"]["rotate180"] = SnakeDisplay::rotated();
  doc["settings"]["colourRotated"] = _display && _display->colourRotated();
  JsonArray anims = doc["anims"].to<JsonArray>();
  for (const auto& e : AnimStore::list()) {
    JsonObject o = anims.add<JsonObject>();
    o["id"] = e.id; o["full"] = e.h > e.w; o["frames"] = e.frames; o["ms"] = e.ms; o["bytes"] = e.bytes;
  }
  doc["animFree"] = AnimStore::freeBytes();
  doc["animTotal"] = AnimStore::totalBytes();
  doc["heap"] = ESP.getFreeHeap();
  doc["heapMin"] = ESP.getMinFreeHeap();
  doc["heapBlock"] = ESP.getMaxAllocHeap();
  JsonArray sizes = doc["sizes"].to<JsonArray>();
  for (int i = 0; i < Face::fontSizeCount(); ++i) sizes.add(Face::fontSize(i));
  JsonArray sprites = doc["sprites"].to<JsonArray>();
  for (int i = 0; i < Face::spriteCount(); ++i) {
    const Sprite& s = Face::spriteAt(i);
    JsonObject o = sprites.add<JsonObject>();
    o["id"] = s.id; o["name"] = s.name; o["frames"] = s.nFrames; o["ms"] = s.frameMs;
  }
  JsonArray arr = doc["sets"].to<JsonArray>();
  for (const TextSet& st : _store.sets()) setToJson(arr.add<JsonObject>(), st);
  String out;
  serializeJson(doc, out);
  _server.sendHeader("Cache-Control", "no-store");
  _server.send(200, "application/json; charset=utf-8", out);
}

// ok/size/lines for the capped layout, plus maxFit = the largest size that
// fits with no cap (what "automatic" would pick).
static void checkInto(JsonObject o, int light, const String& text, int maxSize) {
  String tt = text; tt.trim();
  if (tt.isEmpty()) { o["ok"] = true; o["size"] = 0; o["lines"] = 0; o["maxFit"] = 0; return; }
  TextLayout L = Face::layoutFor(light, text.c_str(), maxSize);
  o["ok"] = L.ok();
  if (L.ok()) { o["size"] = L.font->size; o["lines"] = L.lines; }
  else o["error"] = L.error.c_str();
  TextLayout A = maxSize ? Face::layoutFor(light, text.c_str(), 0) : L;
  o["maxFit"] = A.ok() ? A.font->size : 0;
}

void WebUI::handleCheck() {
  String text = _server.arg("text");
  int light = _server.hasArg("light") ? _server.arg("light").toInt() : 0;
  int maxSize = _server.hasArg("size") ? _server.arg("size").toInt() : 0;
  if (text.length() > SetStore::MAX_TEXT_BYTES) {
    _server.send(200, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"too long\"}");
    return;
  }
  JsonDocument doc;
  checkInto(doc.to<JsonObject>(), light, text, maxSize);
  String out;
  serializeJson(doc, out);
  _server.send(200, "application/json; charset=utf-8", out);
}

void WebUI::handlePreview(bool rle) {
  TextLayout L[Face::LIGHTS];
  Face::LightSpec specs[Face::LIGHTS];
  for (int i = 0; i < Face::LIGHTS; ++i) {
    const String n = String(i);
    specs[i].sprite = Face::spriteById(_server.arg("g" + n).c_str());
    specs[i].effect = Face::effectById(_server.arg("g" + n).c_str());
    if (!specs[i].sprite && !specs[i].effect && _server.arg("k" + n) != "off") {
      L[i] = Face::layoutFor(i, _server.arg("t" + n).c_str(), _server.arg("s" + n).toInt());
      specs[i].text = &L[i];   // a text that does not fit renders as an empty circle
    }
    specs[i].bg = _server.hasArg("c" + n) ? (_server.arg("c" + n).toInt() & 63) : Face::defaultColour(i);
    specs[i].fg = _server.arg("f" + n).toInt() & 63;
    specs[i].hueBg = _server.arg("hc" + n).toInt() % 360;
    specs[i].hueFg = _server.arg("hf" + n).toInt() % 360;
    const String q = _server.arg("q" + n);   // "a,b,c": quarters 2-4
    for (int k = 0, from = 0; k < 3 && from < (int)q.length(); ++k) {
      int comma = q.indexOf(',', from); if (comma < 0) comma = q.length();
      specs[i].quarters[k] = q.substring(from, comma).toInt() & 63;
      from = comma + 1;
    }
  }
  const uint8_t mask = _server.hasArg("m") ? (_server.arg("m").toInt() & 7) : 7;
  const uint32_t k = _server.arg("k").toInt();
  Face::Beat beat;   // bpm, b = time on the beat grid (default: the frame's k)
  beat.bpm = _server.arg("bpm").toInt();
  beat.frameMs = k;
  beat.gridMs = _server.hasArg("b") ? (uint32_t)_server.arg("b").toInt() : k;
  beat.frameLen = _server.arg("fl").toInt();   // the frame's length (fills)
  BitmapCanvas* canvas = _canvas;      // only the web task uses it
  canvas->reset();                     // fresh palette for every image
  Face::draw(*canvas, specs, mask, k, beat);
  if (rle) {
    // Build the whole stream first and send it with a Content-Length: a
    // chunked reply with one lost chunk showed up on phones as a partly
    // black preview (the browser could not tell the body was incomplete).
    std::vector<uint8_t>* out = new std::vector<uint8_t>();
    out->reserve(6144);
    canvas->rleStream([](const uint8_t* p, size_t n, void* ctx) {
      auto* v = static_cast<std::vector<uint8_t>*>(ctx);
      v->insert(v->end(), p, p + n);
    }, out);
    _server.setContentLength(out->size());
    _server.sendHeader("Cache-Control", "no-store");
    _server.send(200, "application/octet-stream", "");
    _server.sendContent((const char*)out->data(), out->size());
    delete out;
    return;
  }
  uint8_t hdr[BitmapCanvas::BMP_HEADER], row[BitmapCanvas::ROW_BYTES];
  canvas->bmpHeader(hdr);
  _server.setContentLength(BitmapCanvas::BMP_SIZE);
  _server.sendHeader("Cache-Control", "no-store");
  _server.send(200, "image/bmp", "");
  _server.sendContent((const char*)hdr, sizeof(hdr));
  for (int y = BitmapCanvas::H - 1; y >= 0; --y) {      // BMP rows are bottom-up
    canvas->bmpRow(y, row);
    _server.sendContent((const char*)row, sizeof(row));
  }
}

// Every text of a light must fit (empty is fine: a blank light).
bool WebUI::checkLights(const LightDef l[3], String& err) {
  static const char* names[3] = {"top", "middle", "bottom"};
  for (int i = 0; i < 3; ++i) {
    if (l[i].graphic.length()) {
      if (l[i].graphic.startsWith("u:")) {
        if (!AnimStore::exists(l[i].graphic.substring(2))) { err = String(names[i]) + ": animation not on the sign"; return false; }
      } else if (!Face::spriteById(l[i].graphic.c_str()) && !Face::effectById(l[i].graphic.c_str())) { err = String(names[i]) + ": unknown graphic"; return false; }
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

// Reads a set from the JSON body and validates every text.
bool WebUI::parseSet(TextSet& s, String& err) {
  JsonDocument doc;
  if (deserializeJson(doc, _server.arg("plain"))) { err = "bad json"; return false; }
  setFromJson(doc.as<JsonObjectConst>(), s);
  if (s.frames.empty()) { err = "no frames"; return false; }
  if (doc["frames"].size() > SetStore::MAX_FRAMES) { err = "at most " + String(SetStore::MAX_FRAMES) + " frames"; return false; }
  for (size_t k = 0; k < s.frames.size(); ++k)
    if (!checkLights(s.frames[k].l, err)) { err = "frame " + String(k + 1) + ", " + err; return false; }
  return true;
}

void WebUI::handleLive(bool on) {
  if (!on) { _app.clearDraft(); sendState(); return; }
  JsonDocument doc;
  if (deserializeJson(doc, _server.arg("plain"))) { _server.send(400, "application/json", "{\"error\":\"bad json\"}"); return; }
  KeyFrame f;
  frameFromJson(doc.as<JsonObjectConst>(), f);   // a text that does not fit just shows as an empty circle
  const int bpm = doc["bpm"] | 120;
  _app.setDraft(f, bpm < 10 ? 10 : bpm > 400 ? 400 : bpm);
  sendState();
}

void WebUI::handleAdd(int replaceIndex) {
  auto& sets = _store.sets();
  if (replaceIndex < 0 && sets.size() >= SetStore::MAX_SETS) { _server.send(400, "application/json", "{\"error\":\"too many sets\"}"); return; }
  if (replaceIndex >= (int)sets.size()) { _server.send(404, "application/json", "{\"error\":\"no such set\"}"); return; }
  TextSet s; String err;
  if (!parseSet(s, err)) {
    JsonDocument e; e["error"] = err;
    String out; serializeJson(e, out);
    _server.send(400, "application/json; charset=utf-8", out);
    return;
  }
  _app.lockPlayback();                 // the render core reads sets while preparing frames
  _app.clearDraft();
  TextSet backup;
  if (replaceIndex >= 0) { backup = sets[replaceIndex]; sets[replaceIndex] = s; }
  else sets.push_back(s);
  const bool saved = _store.save();
  if (!saved) {
    if (replaceIndex >= 0) sets[replaceIndex] = backup; else sets.pop_back();
  } else if (replaceIndex < 0 && sets.size() == 1) _app.showSet(0);
  else if (replaceIndex >= 0) _app.showSet(replaceIndex);
  else _app.onSetsChanged();
  _app.unlockPlayback();
  if (!saved) { _server.send(400, "application/json", "{\"error\":\"storage full: delete a set or shorten this one\"}"); return; }
  sendState();
}

void WebUI::handleDelete() {
  int i = _server.uri().substring(strlen("/api/sets/")).toInt();
  auto& sets = _store.sets();
  if (i < 0 || i >= (int)sets.size()) { _server.send(404, "application/json", "{\"error\":\"no such set\"}"); return; }
  _app.lockPlayback();
  sets.erase(sets.begin() + i);
  _store.save();
  _app.onSetsChanged();
  _app.unlockPlayback();
  sendState();
}

void WebUI::handleShow() {
  int i = _server.uri().substring(strlen("/api/show/")).toInt();
  if (i < 0 || i >= (int)_store.sets().size()) { _server.send(404, "application/json", "{\"error\":\"no such set\"}"); return; }
  _app.showSet(i);
  sendState();
}


// ---------- uploaded animations ----------
static File s_upFile;

void WebUI::sendAnims() {
  JsonDocument doc;
  JsonArray arr = doc["anims"].to<JsonArray>();
  for (const auto& e : AnimStore::list()) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = e.id; o["full"] = e.h > e.w; o["frames"] = e.frames; o["ms"] = e.ms; o["bytes"] = e.bytes;
  }
  doc["free"] = AnimStore::freeBytes();
  doc["total"] = AnimStore::totalBytes();
  String out; serializeJson(doc, out);
  _server.sendHeader("Cache-Control", "no-store");
  _server.send(200, "application/json", out);
}

// Streams the multipart body to /a/.upload; the done handler validates it and
// renames it into place under the playback lock.
void WebUI::handleAnimUpload() {
  HTTPUpload& up = _server.upload();
  if (up.status == UPLOAD_FILE_START) {
    _upErr = "";
    _upId = AnimStore::sanitizeId(_server.hasArg("name") && _server.arg("name").length() ? _server.arg("name") : up.filename);
    // The page sends the file size: a replacement that would not fit next to
    // the old copy deletes the old copy first (writing into a full LittleFS
    // stalls for ~30 s). The page asked the user before replacing.
    const uint32_t size = _server.hasArg("size") ? (uint32_t)_server.arg("size").toInt() : 0;
    if (size && size + 8192 > AnimStore::freeBytes() && AnimStore::exists(_upId)) {
      _app.lockPlayback(); AnimStore::remove(_upId); _app.unlockPlayback();
      _app.renderCurrent();
    }
    if (size && size + 8192 > AnimStore::freeBytes()) { _upErr = "storage full (" + String(AnimStore::freeBytes() / 1024) + " KB free)"; return; }
    s_upFile = LittleFS.open("/a/.upload", "w");
    _upOpen = (bool)s_upFile;
    if (!_upOpen) _upErr = "cannot write to storage";
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (_upOpen && _upErr.isEmpty() && s_upFile.write(up.buf, up.currentSize) != up.currentSize) _upErr = "storage full";
  } else if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) {
    if (_upOpen) { s_upFile.close(); _upOpen = false; }
    if (up.status == UPLOAD_FILE_ABORTED) _upErr = "upload aborted";
  }
}

void WebUI::handleAnimDone() {
  if (_upOpen) { s_upFile.close(); _upOpen = false; }
  String err = _upErr;
  if (err.isEmpty() && _upId.isEmpty()) err = "no file";
  if (err.isEmpty()) AnimStore::validate("/a/.upload", err);
  if (!err.isEmpty()) {
    LittleFS.remove("/a/.upload");
    JsonDocument d; d["error"] = err; String out; serializeJson(d, out);
    _server.send(400, "application/json", out);
    return;
  }
  _app.lockPlayback();
  const String path = AnimStore::pathFor(_upId);
  AnimStore::closeAll();
  LittleFS.remove(path);
  LittleFS.rename("/a/.upload", path);
  _app.unlockPlayback();
  _app.renderCurrent();   // a set may already use this name
  Serial.printf("[anim] stored '%s', %u KB free\n", _upId.c_str(), (unsigned)(AnimStore::freeBytes() / 1024));
  sendAnims();
}
