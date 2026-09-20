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
  void begin(const char* ssid, const char* password);
  void loop();
 private:
  void sendState();
  void handleCheck();
  void handlePreview(bool rle);
  void handleLive(bool on);
  bool parseSet(TextSet& s, String& err);   // from the JSON body
  void handleAdd();
  void handleDelete();
  void handleShow();
  void handleInterval();
  bool captiveRedirect();
  StopLightApp& _app;
  SetStore& _store;
  WebServer _server;
  DNSServer _dns;
};
