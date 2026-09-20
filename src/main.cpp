#include <Arduino.h>
#include "SnakeDisplay.h"
#include "SetStore.h"
#include "StopLightApp.h"
#include "WebUI.h"

// StopLight: three circles with text or figures on a 128x384 HUB75 chain,
// managed from a captive-portal web page on the ESP32's own access point.

static const char* AP_SSID = "StopLight";
static const char* AP_PASS = "UriNelli";

SnakeDisplay display;
SetStore store;
StopLightApp app(display, store);
WebUI web(app, store);

void setup() {
  Serial.begin(115200);
  delay(300);

  // Display first: the DMA buffers want contiguous internal RAM.
  SnakeDisplay::Options opt;
  opt.doubleBuffer = true;
  if (!display.begin(opt)) {
    Serial.println("Double-buffered init failed, retrying single-buffered");
    opt.doubleBuffer = false;
    display.begin(opt);
  }
  if (!display.isRunning()) { Serial.println("Display init failed"); while (true) delay(1000); }
  display.setBrightness(128);
  Serial.printf("Free heap after display init: %u bytes\n", ESP.getFreeHeap());

  store.load();
  app.showSet(0);
  web.begin(AP_SSID, AP_PASS);
  Serial.printf("Free heap after start: %u bytes\n", ESP.getFreeHeap());

  // Two cores, two jobs: the web server (and captive DNS) runs on core 0 next
  // to the WiFi stack; frame rendering owns core 1 through the Arduino loop.
  xTaskCreatePinnedToCore([](void*) {
    for (;;) { web.loop(); vTaskDelay(pdMS_TO_TICKS(2)); }
  }, "web", 8192, nullptr, 1, nullptr, 0);
}

void loop() {   // core 1: rendering only
  app.loop();
  delay(2);
}
