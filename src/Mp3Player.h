#pragma once
#include <Arduino.h>
#include <HardwareSerial.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Minimal driver for the DIYables "MP3 Player Module" (YX5300-type serial
// player with a 3.5 mm jack and microSD): 9600 baud, 8-byte frames
// 7E FF 06 CMD FB HI LO EF with no checksum. Replies come as 8-byte frames
// too; 10-byte checksummed frames (YX5200/DFPlayer family) are also accepted.
// Tracks are addressed by number (001.mp3 ... in the card's root, in copy
// order); the chip reports how many files the card holds but never their
// names. Safe to call from both cores (mutex inside); poll() must run
// regularly.
class Mp3Player {
 public:
  void begin(HardwareSerial& serial, int rxPin, int txPin, uint8_t volume);
  void poll();                                // parse replies, drive the start-up sequence
  void play(uint16_t track);                  // 1-based root track
  void stop();
  void setVolume(uint8_t v);                  // 0..30
  uint8_t volume() const { return _volume; }
  bool present() const { return _present; }   // answered at least one query
  uint16_t trackCount() const { return _tracks; }
  void rescan();                              // query the file count again
  const char* lastError() const { return _err; }

 private:
  void send(uint8_t cmd, uint16_t param, bool feedback = false);
  void handle(const uint8_t* f);
  HardwareSerial* _s = nullptr;
  SemaphoreHandle_t _lock = xSemaphoreCreateMutex();
  uint8_t _buf[12]; int _n = 0;
  bool _present = false, _online = false;
  uint16_t _tracks = 0;
  uint8_t _volume = 20;
  uint32_t _started = 0, _lastQuery = 0;
  int _queries = 0;
  const char* _err = "";
};
