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
  // Whether the checks get the "online" answer (above) or, as before
  // 2026-10-23 and the default again, the redirect to the page (a captive
  // portal to the phone). Phones could not open the page after the switch
  // to "online" while laptops could; "#ANIM PROBES online|captive" flips it
  // until the next restart, to compare the two.
  bool probesOnline(int set = -1);
  // The user reported that a power cycle cures the trouble, so something
  // wears out while the sign runs. One serial line a minute (stations,
  // internal heap free / lowest / largest block, PSRAM, the longest pause of
  // the web loop) shows what, the next time it happens.
  void webTick();     // web task, once per loop pass
  // {"stations","loops","maxPauseMs"} since the last reset: web loop passes
  // and the longest gap between two of them (for "#ANIM PERF").
  String webStats(bool reset);
  // Debugging over the cable ("#ANIM STA <ssid> <password>", "#ANIM STA" for
  // the status, "#ANIM STA OFF"): also join another WiFi network so a PC on
  // that network can reach the web server while the sign's own network keeps
  // running. Never saved: a restart forgets it.
  String station(const String& args);
  // "#ANIM WEBLOG 1" / "0": one serial line per web request and per new
  // address handed out (who reaches the server at all, phones vs laptops).
  bool webLog(int set = -1);
  void healthLog();   // render loop
}
