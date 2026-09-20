#include "WebUI.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include "WebPage.h"
#include "StopLightApp.h"
#include "Preview.h"
#include "Animation.h"

static const byte DNS_PORT = 53;

void WebUI::begin(const char* ssid, const char* password) {
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
  _server.on("/api/interval", HTTP_POST, [this] { handleInterval(); });
  _server.onNotFound([this] {
    const String& uri = _server.uri();
    if (uri.startsWith("/api/sets/") && _server.method() == HTTP_DELETE) { handleDelete(); return; }
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
  doc["interval"] = _store.intervalSec();
  doc["current"] = _app.current();
  doc["live"] = _app.hasDraft();
  JsonArray sizes = doc["sizes"].to<JsonArray>();
  for (int i = 0; i < Face::fontSizeCount(); ++i) sizes.add(Face::fontSize(i));
  JsonArray sprites = doc["sprites"].to<JsonArray>();
  for (int i = 0; i < Face::spriteCount(); ++i) {
    const Sprite& s = Face::spriteAt(i);
    JsonObject o = sprites.add<JsonObject>();
    o["id"] = s.id; o["name"] = s.name; o["frames"] = s.nFrames; o["ms"] = s.frameMs;
  }
  JsonArray anims = doc["animations"].to<JsonArray>();
  for (int i = 0; i < animationCount(); ++i) {
    const Animation& a = animationAt(i);
    JsonObject o = anims.add<JsonObject>();
    o["id"] = a.id; o["name"] = a.name;
    JsonArray st = o["steps"].to<JsonArray>();
    for (int k = 0; k < a.nSteps; ++k) { JsonArray s = st.add<JsonArray>(); s.add(a.steps[k].mask); s.add(a.steps[k].units); }
  }
  JsonArray arr = doc["sets"].to<JsonArray>();
  for (const TextSet& s : _store.sets()) {
    JsonObject o = arr.add<JsonObject>();
    JsonArray t = o["t"].to<JsonArray>();
    JsonArray sz = o["s"].to<JsonArray>();
    JsonArray col = o["c"].to<JsonArray>();
    JsonArray ink = o["f"].to<JsonArray>();
    JsonArray gr = o["g"].to<JsonArray>();
    for (int i = 0; i < 3; ++i) { t.add(s.text[i]); sz.add(s.size[i]); col.add(s.colour[i]); ink.add(s.ink[i]); gr.add(s.graphic[i]); }
    o["a"] = s.anim; o["p"] = s.unitMs;
  }
  String out;
  serializeJson(doc, out);
  _server.sendHeader("Cache-Control", "no-store");
  _server.send(200, "application/json; charset=utf-8", out);
}

// ok/size/lines for the capped layout, plus maxFit = the largest size that
// fits with no cap (what "automatic" would pick).
static void checkInto(JsonObject o, int light, const String& text, int maxSize) {
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
    if (!specs[i].sprite) {
      L[i] = Face::layoutFor(i, _server.arg("t" + n).c_str(), _server.arg("s" + n).toInt());
      specs[i].text = &L[i];   // a text that does not fit renders as an empty circle
    }
    specs[i].bg = _server.hasArg("c" + n) ? (_server.arg("c" + n).toInt() & 63) : 0;
    specs[i].fg = _server.arg("f" + n).toInt() & 63;
  }
  const uint8_t mask = _server.hasArg("m") ? (_server.arg("m").toInt() & 7) : 7;
  const uint32_t k = _server.arg("k").toInt();
  auto* canvas = new BitmapCanvas();   // 24 KB; heap rather than stack
  Face::draw(*canvas, specs, mask, k);
  if (rle) {
    _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    _server.sendHeader("Cache-Control", "no-store");
    _server.send(200, "application/octet-stream", "");
    canvas->rleStream([](const uint8_t* p, size_t n, void* ctx) {
      static_cast<WebServer*>(ctx)->sendContent((const char*)p, n);
    }, &_server);
    _server.sendContent("");   // end of chunked body
    delete canvas;
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
  delete canvas;
}

// Reads a set from the JSON body. err is set (and false returned) when a
// text does not fit; the set is still filled in so a live draft can show it.
bool WebUI::parseSet(TextSet& s, String& err) {
  JsonDocument doc;
  if (deserializeJson(doc, _server.arg("plain"))) { err = "bad json"; return false; }
  static const char* names[3] = {"top", "middle", "bottom"};
  bool ok = true;
  for (int i = 0; i < 3; ++i) {
    s.text[i] = (const char*)(doc["t"][i] | "");
    s.text[i].trim();
    s.size[i] = doc["s"][i] | 0;
    s.colour[i] = (doc["c"][i] | (int)Face::defaultColour(i)) & 63;
    s.ink[i] = (doc["f"][i] | 0) & 63;
    s.graphic[i] = Face::spriteById((const char*)(doc["g"][i] | "")) ? (const char*)doc["g"][i] : "";
    if (s.graphic[i].length() || !ok) continue;
    if (s.text[i].length() > SetStore::MAX_TEXT_BYTES) { err = String(names[i]) + ": text too long"; ok = false; continue; }
    TextLayout L = Face::layoutFor(i, s.text[i].c_str(), s.size[i]);
    if (!L.ok()) { err = String(names[i]) + ": " + L.error.c_str(); ok = false; }
  }
  s.anim = animationById((const char*)(doc["a"] | "all")).id;
  int p = doc["p"] | 1000;
  s.unitMs = p < 100 ? 100 : p > 60000 ? 60000 : p;
  return ok;
}

void WebUI::handleLive(bool on) {
  if (!on) { _app.clearDraft(); sendState(); return; }
  TextSet s; String err;
  parseSet(s, err);            // a text that does not fit just shows as an empty circle
  _app.setDraft(s);
  sendState();
}

void WebUI::handleAdd() {
  if (_store.sets().size() >= SetStore::MAX_SETS) { _server.send(400, "application/json", "{\"error\":\"too many sets\"}"); return; }
  TextSet s; String err;
  if (!parseSet(s, err)) {
    JsonDocument e; e["error"] = err;
    String out; serializeJson(e, out);
    _server.send(400, "application/json; charset=utf-8", out);
    return;
  }
  _app.clearDraft();
  _store.sets().push_back(s);
  _store.save();
  if (_store.sets().size() == 1) _app.showSet(0); else _app.onSetsChanged();
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

void WebUI::handleInterval() {
  JsonDocument doc;
  if (deserializeJson(doc, _server.arg("plain"))) { _server.send(400, "application/json", "{\"error\":\"bad json\"}"); return; }
  _store.setIntervalSec(doc["seconds"] | 60);
  _store.save();
  sendState();
}
