#pragma once
#include <Arduino.h>
#include <vector>
#include "SnakeDisplay.h"
#include "SetStore.h"
#include "Face.h"
#include "Mp3Player.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Plays the stored keyframe sets on the display. Runs on the render core;
// the web task only calls the public setters (recursive mutex inside).
class StopLightApp {
 public:
  StopLightApp(SnakeDisplay& d, SetStore& s) : _display(d), _store(s) {}
  void setMp3(Mp3Player* m) { _mp3 = m; }
  int current() const { return _current; }
  int frameIndex() const { return _pos < (int)_playlist.size() ? _playlist[_pos] : 0; }
  void showSet(int index);            // renders immediately, restarts timers
  void renderCurrent();
  void loop();                        // keyframes, loop sections, sprites, drafts
  void onSetsChanged();               // keep `current` valid after add/remove/edit
  // Live editing: show one frame on the LEDs without storing it. Playback is
  // paused until clearDraft() or LIVE_TIMEOUT_MS without a new draft.
  void setDraft(const KeyFrame& f);
  void clearDraft();
  bool hasDraft() const { return _draftActive; }
  static constexpr uint32_t LIVE_TIMEOUT_MS = 60000;
  // The web task must hold this while it changes the store's sets: the
  // render core reads them while preparing frames.
  void lockPlayback()   { xSemaphoreTakeRecursive(_lock, portMAX_DELAY); }
  void unlockPlayback() { xSemaphoreGiveRecursive(_lock); }

 private:
  struct Prepared {                   // one display frame, laid out and coloured
    TextLayout layouts[Face::LIGHTS];
    Face::LightSpec specs[Face::LIGHTS];
    uint8_t mask = 7;
    uint32_t ms = 1000;
    int16_t track = 0;
    int pos = -1;                     // playlist position it was prepared for
  };
  void startAudio(const Prepared& p);
  void startSet();                                  // build the playlist, prepare frame 0
  void prepareFrame(Prepared& p, const LightDef l[3], uint8_t mask, uint32_t ms);
  void preparePos(Prepared& p, int pos);            // frame at playlist position
  void render(const Prepared& p, uint32_t elapsedMs);
  void renderSprites(const Prepared& p, uint32_t elapsedMs);
  uint32_t frameKey(const Prepared& p, uint32_t elapsedMs) const;
  Rgb2 randomColour(Rgb2 avoid);

  SnakeDisplay& _display;
  SetStore& _store;
  Mp3Player* _mp3 = nullptr;
  SemaphoreHandle_t _lock = xSemaphoreCreateRecursiveMutex();

  int _current = 0;
  uint32_t _setStart = 0, _frameStart = 0;
  std::vector<uint16_t> _playlist;
  int _pos = 0;
  Prepared _slots[2];
  Prepared* _cur = &_slots[0];
  Prepared* _next = &_slots[1];
  uint32_t _lastKey = 0xFFFFFFFF;
  Rgb2 _recent[2] = {0, 0};           // last two random picks, never repeated

  KeyFrame _draft;
  bool _draftActive = false, _draftDirty = false;
  uint32_t _draftAt = 0;
};
