#pragma once
#include <Arduino.h>
#include "SnakeDisplay.h"
#include "SetStore.h"
#include "Face.h"
#include "Animation.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Ties the display, the stored sets and the rotation timer together.
class StopLightApp {
 public:
  StopLightApp(SnakeDisplay& d, SetStore& s) : _display(d), _store(s) {}
  int current() const { return _current; }
  void showSet(int index);            // renders immediately, restarts the timer
  void renderCurrent();
  void loop();                        // advances on the interval, steps animations
  void onSetsChanged();               // keep `current` valid after add/remove
  // Live editing: show this set on the LEDs without storing it. Rotation is
  // paused until clearDraft() or LIVE_TIMEOUT_MS without a new draft.
  void setDraft(const TextSet& s);
  void clearDraft();
  bool hasDraft() const { return _draftActive; }
  static constexpr uint32_t LIVE_TIMEOUT_MS = 60000;
 private:
  void prepare();                     // lay out the current set's texts once
  void render(uint8_t litMask, uint32_t elapsedMs);      // full face
  void renderSprites(uint8_t litMask, uint32_t elapsedMs); // sprite boxes only      // full face

  uint32_t frameKey(uint8_t mask, uint32_t elapsedMs) const;
  SnakeDisplay& _display;
  SetStore& _store;
  int _current = 0;
  uint32_t _lastSwitch = 0;
  TextLayout _layouts[Face::LIGHTS];
  Face::LightSpec _specs[Face::LIGHTS];
  const Animation* _anim = nullptr;
  uint32_t _unitMs = 1000;
  uint32_t _lastKey = 0xFFFFFFFF;
  uint8_t _lastMask = 0xFF;
  SemaphoreHandle_t _lock = xSemaphoreCreateMutex();   // web task vs render task
  TextSet _draft;
  bool _draftActive = false;
  bool _draftDirty = false;     // a new draft is waiting for the render task
  uint32_t _draftAt = 0;
};
