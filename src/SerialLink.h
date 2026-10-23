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
//   PC: #ANIM SUM <name>                        board: #ANIM SUM <name> <bytes> <crc32 hex> | #ANIM ERR <why>
// Sets and settings (tools/sign_editor.py downloads and uploads the whole sign):
//   PC: #ANIM SETS                              board: #ANIM SETS <bytes> <crc32 hex>
//        takes a snapshot of the stored sets ({"sets":[...]}, as kept in NVS); read it
//        with GET .sets <offset> <len> (BLK replies as for a file). Dropped after 2 min idle.
//   PC: #ANIM SPUT <size>                       board: #ANIM READY | #ANIM ERR <why>
//        then DATA packets and END as for PUT; END answers #ANIM OK <sets> <stored json bytes>
//        or #ANIM ERR <why>. The JSON ({"sets":[...]}, at most 48 KB) replaces every stored
//        set after the checks the page's Add makes (texts fit, graphics and animations
//        exist, at most 40 sets / 64 frames each, 32 KB once stored); an error changes nothing.
//   PC: #ANIM CONF                              board: #ANIM CONF {"offDark":b,"rotate180":b,"colourRotated":b,"volume":n,"mp3":b,"tracks":n}
//   PC: #ANIM CONF {"offDark":b,...}            board: the same reply, after applying the keys given (offDark,
//        rotate180, volume 0..30, colourRotated; a changed colourRotated restarts the board after the reply)
// A packet whose bytes stop arriving for 1 s is answered with NAK; an upload
// idle for 60 s is dropped. A replacing upload that would not fit next to the
// old copy deletes the old copy at PUT. Polled from the web task (core 0); the finished
// file is renamed into place under the playback lock.
class SerialLink {
 public:
  static constexpr size_t MAX_PACKET = 2048;
  static constexpr size_t MAX_SETS_UPLOAD = 48 * 1024;
  SerialLink(Stream& s, StopLightApp& app) : _s(s), _app(app) {}
  void poll();
 private:
  void command(const String& line);
  void reply(const String& msg);
  void packetDone();
  void dropUpload();
  void sendBlock(unsigned long off, const uint8_t* raw, int got);
  void finishSets();           // END of an SPUT: check and store
  void conf(const String& json);
  void dropSnapshot();
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
  bool _upSets = false;        // the open upload is the sets JSON (into _setsBuf, PSRAM), not a file
  uint8_t* _setsBuf = nullptr;
  uint8_t* _snap = nullptr;    // SETS snapshot, read with GET .sets
  uint32_t _snapLen = 0, _snapAt = 0;
};
