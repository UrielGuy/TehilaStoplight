#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "SetStore.h"

// WiFi access point + captive-portal web server for managing text sets.
class StopLightApp;   // owns display + store + rotation

class WebUI {
 public:
  WebUI(StopLightApp& app, SetStore& store) : _app(app), _store(store), _server(80) {}
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
  bool captiveRedirect();
  StopLightApp& _app;
  SetStore& _store;
  WebServer _server;
  DNSServer _dns;
  class BitmapCanvas* _canvas = nullptr;   // preview scratch, allocated once in begin()
  class Mp3Player* _mp3 = nullptr;
};
