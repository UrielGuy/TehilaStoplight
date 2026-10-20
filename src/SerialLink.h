#pragma once
#include <Arduino.h>
#include <LittleFS.h>
class StopLightApp;

// Animation upload over a serial port: the UART socket (CH343) or the native
// USB socket. Lines starting with "#ANIM" are commands; everything else the
// board prints (the log) is ignored by the PC side (tools/anim_convert.py).
// Every data packet carries its offset and CRC, so a lost byte or a lost
// reply only costs a resend (the native USB link drops one now and then).
//   PC: #ANIM PUT <name> <size>                 board: #ANIM READY | #ANIM ERR <why>
//   PC: #ANIM DATA <offset> <len> <crc32 hex>\n + len raw bytes (len <= 2048)
//                                               board: #ANIM ACK <next offset> | #ANIM NAK <next offset>
//   PC: #ANIM POS                               board: #ANIM POS <next offset>
//   PC: #ANIM END <crc32 hex of the file>       board: #ANIM OK <free bytes> | #ANIM ERR <why>
//   PC: #ANIM LIST                              board: #ANIM LIST <json array>
//   PC: #ANIM SPACE                             board: #ANIM SPACE <free bytes> <total bytes>
//   PC: #ANIM DEL <name>                        board: #ANIM OK <free bytes>
//   PC: #ANIM GET <name> <offset> <len<=768>    board: #ANIM BLK <offset> <len> <crc32 hex> <base64> | #ANIM ERR <why>
//        (reads a stored file back, e.g. tools/anim_recompress.py backs up and re-encodes them)
// A packet whose bytes stop arriving for 1 s is answered with NAK; an upload
// idle for 60 s is dropped. A replacing upload that would not fit next to the
// old copy deletes the old copy at PUT. Polled from the web task (core 0); the finished
// file is renamed into place under the playback lock.
class SerialLink {
 public:
  static constexpr size_t MAX_PACKET = 2048;
  SerialLink(Stream& s, StopLightApp& app) : _s(s), _app(app) {}
  void poll();
 private:
  void command(const String& line);
  void reply(const String& msg);
  void packetDone();
  void dropUpload();
  Stream& _s;
  StopLightApp& _app;
  String _line;
  bool _up = false;            // an upload is open
  bool _inPacket = false;      // reading the raw bytes of a DATA packet
  File _f;
  String _id;
  uint32_t _size = 0, _got = 0, _crc = 0, _lastAt = 0;
  uint32_t _pOff = 0, _pLen = 0, _pCrc = 0, _pHave = 0, _pAt = 0;
  uint8_t _buf[MAX_PACKET];
};
