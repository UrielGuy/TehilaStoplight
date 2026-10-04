#include "Mp3Player.h"

enum : uint8_t {
  CMD_PLAY_TRACK = 0x03, CMD_VOLUME = 0x06, CMD_SELECT_DEV = 0x09, CMD_RESET = 0x0C,
  CMD_STOP = 0x16, CMD_QUERY_SD_COUNT = 0x48,
  RSP_ONLINE = 0x3F, RSP_SD_INSERTED = 0x3A, RSP_ERROR = 0x40, RSP_ACK = 0x41, RSP_SD_COUNT = 0x48,
};

void Mp3Player::begin(HardwareSerial& serial, int rxPin, int txPin, uint8_t volume) {
  _s = &serial;
  _volume = volume > 30 ? 30 : volume;
  // A powered, correctly wired module holds its TX line high; log it once.
  pinMode(rxPin, INPUT);
  delay(5);
  Serial.printf("[mp3] module TX line (GPIO %d) idles %s\n", rxPin, digitalRead(rxPin) ? "high: module present" : "low: no module or no power");
  _s->begin(9600, SERIAL_8N1, rxPin, txPin);
  _started = millis();
  _lastQuery = 0; _queries = 0;
  send(CMD_RESET, 0);                 // the module answers 0x3F once its card is read (~1-2 s)
}

void Mp3Player::send(uint8_t cmd, uint16_t param, bool feedback) {
  if (!_s) return;
  // 10-byte frame with checksum (the DIYables module replied in this format
  // and accepts it; the 8-byte checksum-free form also works on it).
  uint8_t g[10] = {0x7E, 0xFF, 0x06, cmd, (uint8_t)(feedback ? 1 : 0), (uint8_t)(param >> 8), (uint8_t)param, 0, 0, 0xEF};
  const uint16_t cs = (uint16_t)(0 - (0xFF + 0x06 + cmd + g[4] + g[5] + g[6]));
  g[7] = cs >> 8; g[8] = cs & 0xFF;
  xSemaphoreTake(_lock, portMAX_DELAY);
  _s->write(g, 10);
  xSemaphoreGive(_lock);
}

void Mp3Player::play(uint16_t track) { if (track) send(CMD_PLAY_TRACK, track); }
void Mp3Player::stop() { send(CMD_STOP, 0); }
void Mp3Player::setVolume(uint8_t v) { _volume = v > 30 ? 30 : v; send(CMD_VOLUME, _volume); }
void Mp3Player::rescan() { _queries = 0; _lastQuery = 0; }

void Mp3Player::handle(const uint8_t* f) {
  const uint8_t cmd = f[3];
  const uint16_t p = (f[5] << 8) | f[6];
  if (cmd == RSP_SD_COUNT || cmd == RSP_ERROR || cmd == RSP_ONLINE) Serial.printf("[mp3] reply 0x%02X param %u\n", cmd, p);
  switch (cmd) {
    case RSP_ONLINE:
    case RSP_SD_INSERTED:
      _online = true;
      send(CMD_SELECT_DEV, 2);          // 2 = TF card
      send(CMD_VOLUME, _volume);
      _lastQuery = 0;                   // ask for the count right away
      break;
    case RSP_SD_COUNT:
      _tracks = p; _present = true; _err = "";
      break;
    case RSP_ERROR:
      _err = p == 1 ? "module busy" : p == 2 ? "sleeping" : p == 3 ? "serial error" : p == 4 ? "checksum" :
             p == 5 ? "track out of range" : p == 6 ? "track not found" : p == 7 ? "insert error" : "error";
      _present = true;
      break;
    default: break;
  }
}

void Mp3Player::poll() {
  if (!_s) return;
  while (_s->available()) {
    const uint8_t b = _s->read();
    if (_n == 0 && b != 0x7E) continue;
    _buf[_n++] = b;
    if (b == 0xEF && _n >= 8) {
      // 8-byte frame (YX5300) or 10-byte frame with checksum (YX5200 family).
      if (_n == 8) handle(_buf);
      else if (_n == 10) {
        const uint16_t sum = _buf[1] + _buf[2] + _buf[3] + _buf[4] + _buf[5] + _buf[6];
        const uint16_t cs = (_buf[7] << 8) | _buf[8];
        if ((uint16_t)(sum + cs) == 0) handle(_buf);
      }
      _n = 0;
    } else if (_n >= (int)sizeof(_buf)) {
      _n = 0;
    }
  }
  // Ask for the file count after start-up, and retry a few times if the
  // module stays quiet (some boards never send the 0x3F "online" frame).
  const uint32_t now = millis();
  if (!_present && _queries < 6 && now - _started > 1500 && now - _lastQuery > 2500) {
    if (!_online) { send(CMD_SELECT_DEV, 2); send(CMD_VOLUME, _volume); }
    send(CMD_QUERY_SD_COUNT, 0);
    _lastQuery = now; ++_queries;
  }
}
