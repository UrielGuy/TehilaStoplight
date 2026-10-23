#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include "AccessPoint.h"

// The Arduino WebServer serves one connection at a time and waits up to 5 s
// (HTTP_MAX_DATA_WAIT, a fixed #define) for the request on it. Phone browsers
// open spare connections they may never use, and each held every other
// request up for those 5 s: measured 2026-10-23, 1/2/4 idle connections
// delayed a request by 4.8/9.8/19.8 s and the phone's page timed out. An idle
// connection is now dropped as soon as another one is waiting (the browser
// simply opens a new one when it needs it).
// The Arduino WebServer takes one connection at a time and waits up to 5 s
// for it to send its request. Phones open connections ahead of time (Chrome
// while the address is typed) and send on them a moment later, so one quiet
// connection held up everyone else (2026-10-23: 5 s per idle connection),
// and closing quiet ones early refused the phone's request when it came.
// This keeps up to POOL connections open, serves whichever has sent a
// request (longest waiting first) and closes a connection only after IDLE_MS
// of silence, or the quietest one when the pool is full.
class SignWebServer : public WebServer {
 public:
  SignWebServer(int port) : WebServer(port) { _spy.srv = this; addHandler(&_spy); }   // first handler: sees every request
  void loopOnce() {
    const uint32_t now = millis();
    while (_server.hasClient()) {
      WiFiClient c = _server.available();
      if (!c) break;
      int slot = -1, quiet = 0;
      for (int i = 0; i < POOL; ++i) {
        if (!_pool[i].connected()) { slot = i; break; }
        if (now - _since[i] > now - _since[quiet]) quiet = i;
      }
      if (slot < 0) {
        slot = quiet;
        if (AccessPoint::webLog()) Serial.printf("[web] pool full: closed %s, quiet %lu ms\n", _pool[slot].remoteIP().toString().c_str(), (unsigned long)(now - _since[slot]));
        _pool[slot].stop();
      }
      _pool[slot] = c;
      _since[slot] = now;
    }
    int best = -1;
    for (int i = 0; i < POOL; ++i) {
      if (!_pool[i].connected()) { _pool[i] = WiFiClient(); continue; }
      if (_pool[i].available()) {
        if (best < 0 || now - _since[i] > now - _since[best]) best = i;
      } else if (now - _since[i] > IDLE_MS) {
        if (AccessPoint::webLog()) Serial.printf("[web] %s quiet %lu ms, closed\n", _pool[i].remoteIP().toString().c_str(), (unsigned long)(now - _since[i]));
        _pool[i].stop();
        _pool[i] = WiFiClient();
      }
    }
    if (best < 0) { delay(1); return; }
    _currentClient = _pool[best];
    _pool[best] = WiFiClient();
    _currentStatus = HC_WAIT_READ;
    _statusChange = millis();
    const uint32_t seen = _spy.seen, t0 = millis();
    handleClient();   // parses, answers and closes this connection
    // "#ANIM WEBLOG 1": one serial line per request (client, method, host, path, time taken).
    if (AccessPoint::webLog() && _spy.seen != seen)
      Serial.printf("[web] %s %s %s%s %lu ms (waited %lu)\n", _spy.from.toString().c_str(), _spy.method == HTTP_GET ? "GET" : _spy.method == HTTP_POST ? "POST" : "other",
                    _hostHeader.c_str(), _spy.uri.c_str(), (unsigned long)(millis() - t0), (unsigned long)(t0 - _since[best]));
  }

 private:
  static const int POOL = 8;               // of the 16 lwIP sockets
  static const uint32_t IDLE_MS = 20000;
  WiFiClient _pool[POOL];
  uint32_t _since[POOL] = {};
  struct Spy : RequestHandler {
    SignWebServer* srv = nullptr;
    uint32_t seen = 0;
    IPAddress from;
    HTTPMethod method = HTTP_ANY;
    String uri;
    bool canHandle(HTTPMethod m, String u) override { ++seen; from = srv->client().remoteIP(); method = m; uri = u; return false; }
  } _spy;
};
#include "SetStore.h"
#include "SnakeDisplay.h"

// Web server for managing text sets, on the access point of AccessPoint.h.
class StopLightApp;   // owns display + store + rotation

class WebUI {
 public:
  WebUI(StopLightApp& app, SetStore& store) : _app(app), _store(store), _server(80) {}
  void setDisplay(const SnakeDisplay* d) { _display = d; }
  void setMp3(class Mp3Player* m) { _mp3 = m; }
  void begin(const char* ssid, const char* password);
  void loop();
 private:
  void sendState();
  void handleCheck();
  void handlePreview(bool rle);
  void handleLive(bool on);
  bool parseSet(TextSet& s, String& err);   // from the JSON body
  bool checkLights(const LightDef l[3], String& err);
  void handleAdd(int replaceIndex = -1);
  void handleDelete();
  void handleShow();
  void handleAnimUpload();
  void handleAnimDone();
  void sendAnims();
  String _upId, _upErr;
  bool _upOpen = false;
  bool captiveRedirect();
  StopLightApp& _app;
  const SnakeDisplay* _display = nullptr;
  SetStore& _store;
  SignWebServer _server;
  class BitmapCanvas* _canvas = nullptr;   // preview scratch, allocated once in begin()
  class Mp3Player* _mp3 = nullptr;
};
