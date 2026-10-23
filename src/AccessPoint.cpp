#include "AccessPoint.h"
#include <WiFi.h>
#include <WebServer.h>
#include <esp_wifi.h>

namespace {

AsyncUDP s_dns;
uint8_t s_ip[4];

// One DNS query in, one answer out: the question is echoed, an A (or ANY)
// question gets the AP's address with a short TTL, anything else (AAAA,
// HTTPS records ...) gets "no such record" so the phone falls back to A.
// An EDNS0 OPT record in the query is dropped from the answer, which plain
// DNS allows.
void onDns(AsyncUDPPacket& p) {
  const uint8_t* q = p.data();
  const size_t n = p.length();
  if (n < 12 || n > 512 || (q[2] & 0x80)) return;   // too short / too long / not a query
  static uint8_t out[512 + 16];   // only the AsyncUDP task calls this; its stack is 4 KB
  memcpy(out, q, 12);
  out[2] = 0x84 | (q[2] & 0x01);                     // response, authoritative, keep "recursion desired"
  out[3] = 0x80;                                     // recursion available, no error
  memset(out + 4, 0, 8);                             // counts filled in below
  const uint8_t opcode = (q[2] >> 3) & 0x0F;
  const uint16_t questions = q[4] << 8 | q[5];
  if (opcode != 0 || questions != 1) { out[3] |= 4; p.write(out, 12); return; }   // not implemented
  size_t i = 12;
  while (i < n && q[i]) { if (q[i] & 0xC0) return; i += q[i] + 1; }   // labels of the name
  if (i + 5 > n) return;
  const uint16_t qtype = q[i + 1] << 8 | q[i + 2], qclass = (q[i + 3] << 8 | q[i + 4]) & 0x7FFF;
  size_t len = i + 5;
  memcpy(out + 12, q + 12, len - 12);
  out[5] = 1;                                        // one question
  if ((qtype == 1 || qtype == 255) && qclass == 1) {
    static const uint8_t answer[12] = {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 10, 0, 4};   // name -> question, A, IN, TTL 10 s, 4 bytes
    memcpy(out + len, answer, 12); memcpy(out + len + 12, s_ip, 4);
    len += 16;
    out[7] = 1;                                      // one answer
  }
  p.write(out, len);
}

}  // namespace

void AccessPoint::begin(const char* ssid, const char* password) {
  WiFi.mode(WIFI_AP);
  // 20 MHz wide, not 40: a 40 MHz channel overlaps more neighbours in the
  // crowded 2.4 GHz band and some phones cope worse; 20 MHz is plenty here.
  // The boot log line at the end says which width the AP runs.
  esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
  // Channel 6, visible SSID: channel 1 (the default) is occasionally hidden
  // by phones that filter overlapping networks. Up to 8 clients: with 4, a
  // phone, the PC and a couple of stale associations (a station that left
  // without saying goodbye keeps its slot for 5 minutes) could refuse a join.
  WiFi.softAP(ssid, password, 6, 0, 8);
  delay(100);
  const IPAddress ip = WiFi.softAPIP();
  for (int k = 0; k < 4; ++k) s_ip[k] = ip[k];
  s_dns.onPacket(onDns);
  if (!s_dns.listen(53)) Serial.println("[wifi] DNS: cannot listen on port 53");

  // Who joins and leaves, for the serial log when a phone has trouble.
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    const uint8_t* m = info.wifi_ap_staconnected.mac;
    Serial.printf("[wifi] joined %02x:%02x:%02x:%02x:%02x:%02x (%d connected, heap %u)\n",
                  m[0], m[1], m[2], m[3], m[4], m[5], WiFi.softAPgetStationNum(), ESP.getFreeHeap());
  }, ARDUINO_EVENT_WIFI_AP_STACONNECTED);
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    const uint8_t* m = info.wifi_ap_stadisconnected.mac;
    Serial.printf("[wifi] left %02x:%02x:%02x:%02x:%02x:%02x (%d connected, heap %u)\n",
                  m[0], m[1], m[2], m[3], m[4], m[5], WiFi.softAPgetStationNum(), ESP.getFreeHeap());
  }, ARDUINO_EVENT_WIFI_AP_STADISCONNECTED);

  wifi_bandwidth_t bw = WIFI_BW_HT40;
  esp_wifi_get_bandwidth(WIFI_IF_AP, &bw);
  Serial.printf("[wifi] channel 6, %s MHz, tx power %.1f dBm\n", bw == WIFI_BW_HT20 ? "20" : "40", WiFi.getTxPower() / 4.0f);
}

static volatile uint32_t s_webAt = 0, s_webGapMax = 0;

void AccessPoint::webTick() {
  const uint32_t now = millis();
  if (s_webAt && now - s_webAt > s_webGapMax) s_webGapMax = now - s_webAt;
  s_webAt = now;
}

void AccessPoint::healthLog() {
  static uint32_t last = 0;
  const uint32_t now = millis();
  if (now - last < 60000) return;
  last = now;
  const uint32_t gap = s_webGapMax; s_webGapMax = 0;
  const uint32_t at = s_webAt, since = millis() - at;   // read in this order: the web task moves s_webAt
  Serial.printf("[health] up %lu min, %d stations, heap %u KB (lowest %u, block %u), psram %u KB, web loop max pause %lu ms%s\n",
                (unsigned long)(now / 60000), WiFi.softAPgetStationNum(),
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024), (unsigned)(ESP.getMinFreeHeap() / 1024),
                (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024), (unsigned long)gap,
                at && since > 10000 ? " - WEB LOOP STUCK" : "");
}

// The URLs phones and computers fetch to decide whether a network reaches
// the internet, with the answer each expects when it does.
bool AccessPoint::answerProbe(WebServer& s) {
  const String& u = s.uri();
  if (u == "/generate_204" || u == "/gen_204") {                     // Android, Chrome
    s.send(204, "text/plain", "");
  } else if (u == "/hotspot-detect.html" || u == "/library/test/success.html") {   // iOS, macOS
    s.send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
  } else if (u == "/connecttest.txt") {                              // Windows 10/11
    s.send(200, "text/plain", "Microsoft Connect Test");
  } else if (u == "/ncsi.txt") {                                     // older Windows
    s.send(200, "text/plain", "Microsoft NCSI");
  } else if (u == "/success.txt") {                                  // Firefox
    s.send(200, "text/plain", "success\n");
  } else if (u == "/canonical.html") {                               // Firefox
    s.send(200, "text/html", "<meta http-equiv=\"refresh\" content=\"0;url=https://support.mozilla.org/kb/captive-portal\"/>");
  } else if (u == "/check_network_status.txt") {                     // GNOME NetworkManager
    s.send(200, "text/plain", "NetworkManager is online\n");
  } else {
    return false;
  }
  return true;
}
