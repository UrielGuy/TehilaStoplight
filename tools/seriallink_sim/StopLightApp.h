#pragma once
// Stand-in for src/StopLightApp.h: records what SerialLink asks of the player
// (printed on stderr, counted for the test).
#include "Arduino.h"
#include "SetStore.h"
#include "Face.h"
#include "SnakeDisplay.h"
#include "Mp3Player.h"

class StopLightApp {
 public:
  struct PlayPerf {
    uint32_t shown = 0, skipped = 0, late = 0;
    uint32_t maxGapMs = 0, maxLoopUs = 0, maxLockWaitUs = 0;
  } playPerf;
  void lockPlayback() { ++_locks; }
  void unlockPlayback() { if (_locks <= 0) fprintf(stderr, "[sim] UNLOCK WITHOUT LOCK\n"); --_locks; }
  void renderCurrent() { fprintf(stderr, "[sim] renderCurrent (lock depth %d)\n", _locks); }
  void onSetsChanged() { fprintf(stderr, "[sim] onSetsChanged (lock depth %d)\n", _locks); }
  void setDraft(const KeyFrame&, uint16_t = 120) { fprintf(stderr, "[sim] setDraft\n"); }
  void clearDraft() { fprintf(stderr, "[sim] clearDraft\n"); }
  String debugCurrent() const { return "{}"; }
  int lockDepth() const { return _locks; }
 private:
  int _locks = 0;
};
