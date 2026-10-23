#pragma once
#include <Arduino.h>
#include <vector>
#include "SnakeDisplay.h"
#include "SetStore.h"
#include "Face.h"
#include "Mp3Player.h"
#include "AnimStore.h"
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
  void setDraft(const KeyFrame& f, uint16_t bpm = 120);   // bpm: the tempo of the set being edited
  void clearDraft();
  bool hasDraft() const { return _draftActive; }
  // Playback smoothness of uploaded animations (read with "#ANIM PERF").
  struct PlayPerf {
    uint32_t shown = 0, skipped = 0, late = 0;   // late: shown more than one delay after it was due
    uint32_t maxGapMs = 0, maxLoopUs = 0, maxLockWaitUs = 0;
  } playPerf;
  // Diagnostics (PERF): what the current prepared frame holds.
  String debugCurrent() const {
    String o = "{\"draft\":" + String(_draftActive ? 1 : 0) + ",\"fullLight\":" + String(_cur->fullLight) + ",\"mask\":" + String(_cur->mask) + ",\"anims\":[";
    for (int i = 0; i < 3; ++i) o += String(_cur->anim[i].ok() ? 1 : 0) + (i < 2 ? "," : "");
    o += "],\"graphics\":[";
    for (int i = 0; i < 3; ++i) o += "\"" + _draft.l[i].graphic + "\"" + (i < 2 ? "," : "");
    return o + "]}";
  }
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
    uint16_t fadeMs = 3000;
    uint16_t bpm = 0;                 // the set's tempo for the beat animations
    int pos = -1;                     // playlist position it was prepared for
    AnimInfo anim[Face::LIGHTS];      // uploaded animation per light (empty = none)
    uint8_t animMode[Face::LIGHTS] = {0, 0, 0};
    uint32_t animFrom[Face::LIGHTS] = {0, 0, 0}, animLen[Face::LIGHTS] = {0, 0, 0};
    int animFrame(int i, uint32_t ms) const { return anim[i].frameIn(ms, animFrom[i], animLen[i]); }
    int fullLight = -1;               // light whose animation fills the whole sign
  };
  bool drawAnims(Canvas& c, const Prepared& p, uint32_t elapsedMs, const Face::Beat& beat, uint8_t which = 7);   // false = an animation frame is not decoded yet
  Face::Beat beatAt(const Prepared& p, uint32_t now) const;
  bool renderBeat(const Prepared& p, uint32_t elapsedMs, const Face::Beat& beat, uint8_t which);   // redraw the lights the beat changed
  void startAudio(const Prepared& p);
  void startSet();                                  // build the playlist, prepare frame 0
  void prepareFrame(Prepared& p, const LightDef l[3], uint8_t mask, uint32_t ms);
  void preparePos(Prepared& p, int pos);            // frame at playlist position
  bool render(const Prepared& p, uint32_t elapsedMs);
  bool renderSprites(const Prepared& p, uint32_t elapsedMs);
  uint32_t frameKey(const Prepared& p, uint32_t elapsedMs) const;
  Rgb2 randomColour(Rgb2 avoid);

  SnakeDisplay& _display;
  SetStore& _store;
  Mp3Player* _mp3 = nullptr;
  SemaphoreHandle_t _lock = xSemaphoreCreateRecursiveMutex();

  int _current = 0;
  uint32_t _setStart = 0, _frameStart = 0;
  uint32_t _beatStart = 0;            // beat grid: the set's start, or the last frame that started a track
  uint32_t _lastBeat[Face::LIGHTS] = {0, 0, 0};   // Face::beatState drawn last
  uint16_t _draftBpm = 120;
  uint8_t _animWait = 0;              // lights whose animation had no frame ready when the frame started
  std::vector<uint16_t> _playlist;
  int _pos = 0;
  Prepared _slots[2];
  Prepared* _cur = &_slots[0];
  Prepared* _next = &_slots[1];
  uint32_t _lastKey = 0xFFFFFFFF;
  int _perfLastFrame = -1;
  uint32_t _perfLastAt = 0;
  Rgb2 _recent[2] = {0, 0};           // last two random picks, never repeated

  KeyFrame _draft;
  bool _draftActive = false, _draftDirty = false;
  uint32_t _draftAt = 0;
};
