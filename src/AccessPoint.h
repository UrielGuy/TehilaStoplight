#pragma once
#include <Arduino.h>
#include <AsyncUDP.h>

class WebServer;

// The sign's own WiFi access point, its DNS and the phones' connectivity
// checks (2026-10-23, after "it fails to connect, the connection dies and I
// can't reconnect"):
//
// - DNS answers every A query with the AP's address, from the AsyncUDP task,
//   so names still resolve while the (single-threaded) web server is busy
//   with an upload, a preview or a stalled client. The Arduino DNSServer it
//   replaces was polled from the web loop and gave an empty answer to any
//   query with an EDNS0 record, which modern phone resolvers send; AAAA and
//   HTTPS-record queries got an A record in reply.
// - The connectivity checks of Android, iOS/macOS, Windows and Firefox get
//   the "online" answer instead of a redirect. A redirect made phones treat
//   the sign as a captive portal: iOS shows the page in its sign-in sheet,
//   which forgets its storage and drops the WiFi when dismissed; Android
//   marks the network "sign in required" and does not offer "stay connected",
//   so it (or Samsung's "switch to mobile data") leaves it and does not
//   rejoin by itself. The price: no automatic pop-up, open
//   http://192.168.4.1/ (or any http:// name) in the browser.
namespace AccessPoint {
  void begin(const char* ssid, const char* password);
  // A request for another host: answer it if it is a connectivity check.
  bool answerProbe(WebServer& server);
  // The user reported that a power cycle cures the trouble, so something
  // wears out while the sign runs. One serial line a minute (stations,
  // internal heap free / lowest / largest block, PSRAM, the longest pause of
  // the web loop) shows what, the next time it happens.
  void webTick();     // web task, once per loop pass
  void healthLog();   // render loop
}
