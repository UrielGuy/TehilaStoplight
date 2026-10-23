#include <Arduino.h>
#include <esp_heap_caps.h>
#include "SnakeDisplay.h"
#include "SetStore.h"
#include "StopLightApp.h"
#include "WebUI.h"
#include "PanelPins.h"
#include "Mp3Player.h"
#include <Preferences.h>
#include "AnimStore.h"
#include "SerialLink.h"
#include "AccessPoint.h"

// StopLight: three circles with text or figures on a 128x384 HUB75 chain,
// managed from a captive-portal web page on the ESP32's own access point.

#ifdef MP3_TEST
// ---------------------------------------------------------------------------
// MP3 module test build (pio run -e s3-mp3test): no display, no WiFi. Talks
// to the module on UART1 and takes commands on the USB console (115200):
//   l      read pin levels (which free pins are held high)
//   q      query the file count      r  reset the module
//   p<n>   play track n              s  stop
//   v<n>   volume 0..30              8 / 9  send next commands as 8-byte / 10-byte (checksum) frames
//   b<n>   switch UART baud (9600 default; try 19200/38400/4800)
// Everything received from the module is printed as hex.
// ---------------------------------------------------------------------------
static bool useChecksum = false;
static int txPin = Mp3Pins::tx_pin, rxPin = Mp3Pins::rx_pin;   // 'w' swaps them at runtime
static void mp3send(uint8_t cmd, uint16_t param) {
  uint8_t f[10] = {0x7E, 0xFF, 0x06, cmd, 0, (uint8_t)(param >> 8), (uint8_t)param, 0, 0, 0xEF};
  if (useChecksum) {
    const uint16_t cs = (uint16_t)(0 - (0xFF + 0x06 + cmd + f[4] + f[5] + f[6]));
    f[7] = cs >> 8; f[8] = cs & 0xFF;
    Serial1.write(f, 10);
  } else {
    f[7] = 0xEF; Serial1.write(f, 8);
  }
  Serial.printf("-> cmd 0x%02X param %u (%s)\n", cmd, param, useChecksum ? "10-byte" : "8-byte");
}
static void levels() {
  static const int probe[] = {3, 9, 10, 11, 12, 14, 38, 39, 40, 41, 42, 45, 46};
  Serial.print("pins HIGH (floating read):");
  for (int p : probe) { pinMode(p, INPUT); delay(2); if (digitalRead(p)) Serial.printf(" %d", p); }
  Serial.print(" | HIGH with pull-down:");
  for (int p : probe) { pinMode(p, INPUT_PULLDOWN); delay(2); if (digitalRead(p)) Serial.printf(" %d", p); pinMode(p, INPUT); }
  Serial.println();
  pinMode(Mp3Pins::rx_pin, INPUT);
}
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== MP3 MODULE TEST === (l q r p<n> s v<n> 8 9 b<n>)");
  levels();
  Serial1.begin(9600, SERIAL_8N1, rxPin, txPin);
  Serial.printf("UART1 at 9600: ESP32 TX GPIO %d -> module RX, module TX -> ESP32 RX GPIO %d\n", txPin, rxPin);
  delay(1500);
  mp3send(0x09, 2);   // select the TF card
  delay(200);
  mp3send(0x06, 15);  // volume
  delay(200);
  mp3send(0x48, 0);   // file count
}
void loop() {
  static String cmd;
  while (Serial1.available()) Serial.printf("<- %02X\n", Serial1.read());
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == '\n' || c == '\r') {
      cmd.trim();
      if (cmd.length()) {
        const char k = cmd[0]; const int n = cmd.substring(1).toInt();
        if (k == 'l') levels();
        else if (k == 'q') mp3send(0x48, 0);
        else if (k == 'r') mp3send(0x0C, 0);
        else if (k == 'p') mp3send(0x03, n ? n : 1);
        else if (k == 's') mp3send(0x16, 0);
        else if (k == 'v') mp3send(0x06, n);
        else if (k == '8') { useChecksum = false; Serial.println("8-byte frames"); }
        else if (k == '9') { useChecksum = true; Serial.println("10-byte frames with checksum"); }
        else if (k == 'b') { Serial1.updateBaudRate(n); Serial.printf("baud %d\n", n); }
        else if (k == 'x') {   // bridge test: drive our TX pin as a plain GPIO and watch our RX pin
          Serial1.end();
          pinMode(rxPin, INPUT); pinMode(txPin, OUTPUT);
          digitalWrite(txPin, LOW);  delay(50); const int lo = digitalRead(rxPin);
          digitalWrite(txPin, HIGH); delay(50); const int hi = digitalRead(rxPin);
          pinMode(txPin, INPUT); delay(20); const int idle = digitalRead(rxPin);
          Serial.printf("GPIO %d driven LOW -> GPIO %d reads %d; HIGH -> %d; both released -> %d reads %d  => %s\n", txPin, rxPin, lo, hi, rxPin, idle,
                        (lo == 0 && hi == 1) ? "lines CONNECTED (bridge, same pin, or the module repeats what it hears)" : "lines are separate");
          Serial1.begin(9600, SERIAL_8N1, rxPin, txPin);
        }
        else if (k == 'w') {   // swap the two serial pins
          Serial1.end(); const int t = txPin; txPin = rxPin; rxPin = t;
          pinMode(rxPin, INPUT); delay(5);
          Serial.printf("swapped: ESP32 TX GPIO %d, ESP32 RX GPIO %d (idle level on RX: %d)\n", txPin, rxPin, digitalRead(rxPin));
          Serial1.begin(9600, SERIAL_8N1, rxPin, txPin);
        }
        else Serial.println("?");
      }
      cmd = "";
    } else cmd += c;
  }
  delay(2);
}
#else
static const char* AP_SSID = "StopLight";
static const char* AP_PASS = "UriNelli";

SnakeDisplay display;
SetStore store;

// The Arduino loop task (rendering, incl. text layout) gets 16 KB of stack.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);
StopLightApp app(display, store);
WebUI web(app, store);
SerialLink uartLink(Serial, app);       // animation uploads over the UART socket
SerialLink usbLink(USBSerial, app);     // ... and over the native USB socket
Mp3Player mp3;   // DIYables Mini MP3 on UART1 (Mp3Pins), optional

void setup() {
  // Internal RAM is what WiFi needs (its buffers, the WPA handshake), and the
  // Arduino SDK reserves none of it once PSRAM is on: only allocations of
  // 4 KB and more went to PSRAM, so web replies, Strings and JSON slowly
  // fragmented internal RAM until phones could no longer join (2026-10-23:
  // 32 KB free, largest block 4 KB). From 512 bytes up, PSRAM it is.
  heap_caps_malloc_extmem_enable(512);
  Serial.setRxBufferSize(4096);   // serial animation uploads arrive in 1 KB chunks
  Serial.begin(115200);
  USBSerial.setRxBufferSize(4096);
  USBSerial.setTxBufferSize(4096);   // a whole read-back reply (~1.1 KB) fits without waiting
  USBSerial.begin();
  USBSerial.setTxTimeoutMs(50);   // replies only; with 0 the driver drops a write whenever its lock is busy
  USBSerial.setDebugOutput(false); // keep the ESP log off the native USB socket (it is the upload channel there)
  delay(300);

  // Display first: the DMA buffers want contiguous internal RAM.
  SnakeDisplay::Options opt;
  opt.clock = HUB75_I2S_CFG::HZ_20M;   // ~200 Hz on six panels; the adapter's short ribbons cope
  // Single buffer on the six-panel sign: the second 96 KB buffer left only
  // ~77 KB of heap, and concurrent page traffic then crashed the device
  // (2026-09-28). Sprite frames repaint only their box, so tearing is not
  // visible. The two-panel bench keeps double buffering.
  opt.doubleBuffer = (PANEL_COUNT <= 2);
  { Preferences p; p.begin("stoplight", true); opt.colourRotated = p.getBool("colrot", true); p.end(); }   // 2026-10-19: the panels in use have rotated colour inputs
  if (!display.begin(opt)) {
    Serial.println("Double-buffered init failed, retrying single-buffered");
    opt.doubleBuffer = false;
    display.begin(opt);
  }
  if (!display.isRunning()) { Serial.println("Display init failed"); while (true) delay(1000); }
  display.setBrightness(128);
  Serial.printf("Free heap after display init: %u bytes\n", ESP.getFreeHeap());

  extern uint32_t bootHeap[5];
  bootHeap[0] = ESP.getFreeHeap();
  store.load();
  AnimStore::begin();
  bootHeap[1] = ESP.getFreeHeap();
  AnimStore::beginDecoders();   // two background decoder tasks, one per core
  bootHeap[2] = ESP.getFreeHeap();
  { Preferences p; p.begin("stoplight", true);
    const uint8_t vol = p.getUChar("vol", 20);
    Face::setOffDark(p.getBool("offdark", false));
    SnakeDisplay::setRotated(p.getBool("rot180", true));   // the sign hangs the other way up since 2026-10-19
    p.end();
    mp3.begin(Serial1, Mp3Pins::rx_pin, Mp3Pins::tx_pin, vol); }
  app.setMp3(&mp3);
  web.setMp3(&mp3);
  web.setDisplay(&display);
  app.showSet(0);
  bootHeap[3] = ESP.getFreeHeap();
  web.begin(AP_SSID, AP_PASS);
  bootHeap[4] = ESP.getFreeHeap();
  Serial.printf("Free heap after start: %u bytes\n", ESP.getFreeHeap());

  // Two cores, two jobs: the web server (and captive DNS) runs on core 0 next
  // to the WiFi stack; frame rendering owns core 1 through the Arduino loop.
  xTaskCreatePinnedToCore([](void*) {
    for (;;) { AccessPoint::webTick(); web.loop(); uartLink.poll(); usbLink.poll(); vTaskDelay(pdMS_TO_TICKS(2)); }
  }, "web", 16 * 1024, nullptr, 1, nullptr, 0);
}

void loop() {   // core 1: rendering, plus the MP3 module's replies
  mp3.poll();
  static bool reported = false;
  if (!reported && mp3.present()) { reported = true; Serial.printf("[mp3] module present, %u tracks on the card, volume %u\n", mp3.trackCount(), mp3.volume()); }
  app.loop();
  AccessPoint::healthLog();   // one status line a minute on the serial log
  delay(2);
}
#endif  // MP3_TEST
