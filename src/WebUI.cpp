#include "WebUI.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include "WebPage.h"
#include "StopLightApp.h"
#include "Preview.h"

static const byte DNS_PORT = 53;

void WebUI::begin(const char* ssid, const char* password) {
  _canvas = new BitmapCanvas();   // 24 KB, kept for the life of the server
  WiFi.mode(WIFI_AP);
  // Channel 6, visible SSID, up to 4 clients: channel 1 (the default) is
  // occasionally hidden by phones that filter overlapping networks.
  WiFi.softAP(ssid, password, 6, 0, 4);
  delay(100);
  _dns.setErrorReplyCode(DNSReplyCode::NoError);
  _dns.start(DNS_PORT, "*", WiFi.softAPIP());

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
  _server.on("/api/sets", HTTP_POST, [this] { handleAdd(); });
  _server.onNotFound([this] {
    const String& uri = _server.uri();
    if (uri.startsWith("/api/sets/") && _server.method() == HTTP_DELETE) { handleDelete(); return; }
    if (uri.startsWith("/api/sets/") && _server.method() == HTTP_PUT) { handleAdd(uri.substring(strlen("/api/sets/")).toInt()); return; }
    if (uri.startsWith("/api/show/") && _server.method() == HTTP_POST)   { handleShow();   return; }
    if (captiveRedirect()) return;
    _server.send(404, "text/plain", "not found");
  });
  _server.begin();
  Serial.printf("AP '%s' up, open http://%s/\n", ssid, WiFi.softAPIP().toString().c_str());
}

void WebUI::loop() {
  _dns.processNextRequest();
  _server.handleClient();
}

// Phones and laptops probe these URLs to detect a captive portal; answering
// with a redirect makes them pop the page up on connect.
bool WebUI::captiveRedirect() {
  const String host = _server.hostHeader();
  const String ip = WiFi.softAPIP().toString();
  if (host == ip) return false;
  _server.sendHeader("Location", "http://" + ip + "/", true);
  _server.send(302, "text/plain", "");
  return true;
}

void WebUI::sendState() {
  JsonDocument doc;
  doc["current"] = _app.current();
  doc["live"] = _app.hasDraft();
  doc["frame"] = _app.frameIndex();
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
    if (!specs[i].sprite && _server.arg("k" + n) != "off") {
      L[i] = Face::layoutFor(i, _server.arg("t" + n).c_str(), _server.arg("s" + n).toInt());
      specs[i].text = &L[i];   // a text that does not fit renders as an empty circle
    }
    specs[i].bg = _server.hasArg("c" + n) ? (_server.arg("c" + n).toInt() & 63) : Face::defaultColour(i);
    specs[i].fg = _server.arg("f" + n).toInt() & 63;
  }
  const uint8_t mask = _server.hasArg("m") ? (_server.arg("m").toInt() & 7) : 7;
  const uint32_t k = _server.arg("k").toInt();
  BitmapCanvas* canvas = _canvas;      // only the web task uses it
  Face::draw(*canvas, specs, mask, k);
  if (rle) {
    _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    _server.sendHeader("Cache-Control", "no-store");
    _server.send(200, "application/octet-stream", "");
    canvas->rleStream([](const uint8_t* p, size_t n, void* ctx) {
      static_cast<WebServer*>(ctx)->sendContent((const char*)p, n);
    }, &_server);
    _server.sendContent("");   // end of chunked body
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
      if (!Face::spriteById(l[i].graphic.c_str())) { err = String(names[i]) + ": unknown graphic"; return false; }
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
  _app.setDraft(f);
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
  _app.clearDraft();
  TextSet backup;
  if (replaceIndex >= 0) { backup = sets[replaceIndex]; sets[replaceIndex] = s; }
  else sets.push_back(s);
  if (!_store.save()) {
    if (replaceIndex >= 0) sets[replaceIndex] = backup; else sets.pop_back();
    _server.send(400, "application/json", "{\"error\":\"storage full: delete a set or shorten this one\"}");
    return;
  }
  if (replaceIndex < 0 && sets.size() == 1) _app.showSet(0);
  else if (replaceIndex >= 0) _app.showSet(replaceIndex);
  else _app.onSetsChanged();
  sendState();
}

void WebUI::handleDelete() {
  int i = _server.uri().substring(strlen("/api/sets/")).toInt();
  auto& sets = _store.sets();
  if (i < 0 || i >= (int)sets.size()) { _server.send(404, "application/json", "{\"error\":\"no such set\"}"); return; }
  sets.erase(sets.begin() + i);
  _store.save();
  _app.onSetsChanged();
  sendState();
}

void WebUI::handleShow() {
  int i = _server.uri().substring(strlen("/api/show/")).toInt();
  if (i < 0 || i >= (int)_store.sets().size()) { _server.send(404, "application/json", "{\"error\":\"no such set\"}"); return; }
  _app.showSet(i);
  sendState();
}

