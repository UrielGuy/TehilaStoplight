#include "StopLightApp.h"
#include "DisplayCanvas.h"

void StopLightApp::prepare() {
  auto& sets = _store.sets();
  _anim = &animationById("all");
  _unitMs = 1000;
  for (int i = 0; i < Face::LIGHTS; ++i) { _layouts[i] = TextLayout(); _specs[i] = Face::LightSpec(); _specs[i].text = &_layouts[i]; }
  const bool haveSet = !sets.empty() && _current >= 0 && _current < (int)sets.size();
  if (_draftActive || haveSet) {
    const TextSet& s = _draftActive ? _draft : sets[_current];
    for (int i = 0; i < Face::LIGHTS; ++i) {
      _specs[i].sprite = Face::spriteById(s.graphic[i].c_str());
      if (!_specs[i].sprite) _layouts[i] = Face::layoutFor(i, s.text[i].c_str(), s.size[i]);
      _specs[i].bg = s.colour[i] & 63;
      _specs[i].fg = s.ink[i] & 63;
    }
    _anim = &animationById(s.anim.c_str());
    _unitMs = s.unitMs < 100 ? 100 : s.unitMs;
  }
  _lastKey = 0xFFFFFFFF;
}

// Changes whenever the visible frame would change: lit mask or any sprite frame.
uint32_t StopLightApp::frameKey(uint8_t mask, uint32_t elapsedMs) const {
  uint32_t k = mask;
  for (int i = 0; i < Face::LIGHTS; ++i)
    if (_specs[i].sprite) k = k * 64 + Face::spriteFrame(*_specs[i].sprite, elapsedMs);
  return k;
}

void StopLightApp::render(uint8_t litMask, uint32_t elapsedMs) {
  DisplayCanvas c(_display);
  Face::draw(c, _specs, litMask, elapsedMs);
  _display.show();
  // With double buffering the other buffer still holds the previous frame;
  // draw the same frame there too so the sprite-only repaints that follow
  // start from identical buffers.
  if (_display.isDoubleBuffered()) Face::draw(c, _specs, litMask, elapsedMs);
  _lastKey = frameKey(litMask, elapsedMs);
  _lastMask = litMask;
}

// Only sprite frames changed: repaint every sprite box (all of them, so the
// back buffer of a double-buffered display is brought fully up to date).
void StopLightApp::renderSprites(uint8_t litMask, uint32_t elapsedMs) {
  DisplayCanvas c(_display);
  Face::drawSpriteBoxes(c, _specs, litMask, elapsedMs);
  _display.show();
  _lastKey = frameKey(litMask, elapsedMs);
}

void StopLightApp::renderCurrent() {
  xSemaphoreTake(_lock, portMAX_DELAY);
  prepare();
  render(animationMask(*_anim, _unitMs, 0), 0);
  xSemaphoreGive(_lock);
}

void StopLightApp::showSet(int index) {
  auto& sets = _store.sets();
  if (sets.empty()) _current = 0;
  else _current = ((index % (int)sets.size()) + sets.size()) % sets.size();
  _lastSwitch = millis();
  renderCurrent();
}

void StopLightApp::onSetsChanged() {
  auto& sets = _store.sets();
  if (_current >= (int)sets.size()) _current = 0;
  _lastSwitch = millis();
  renderCurrent();
}

// Called from the web task: only stores the draft. The render task applies
// the newest one on its next pass, so a burst of edits costs one render.
void StopLightApp::setDraft(const TextSet& s) {
  xSemaphoreTake(_lock, portMAX_DELAY);
  _draft = s;
  _draftActive = true;
  _draftDirty = true;
  _draftAt = millis();
  xSemaphoreGive(_lock);
}

void StopLightApp::clearDraft() {
  if (!_draftActive) return;
  _draftActive = false;
  _lastSwitch = millis();
  renderCurrent();
}

void StopLightApp::loop() {
  auto& sets = _store.sets();
  const uint32_t now = millis();
  if (_draftActive && now - _draftAt > LIVE_TIMEOUT_MS) { clearDraft(); return; }
  if (_draftDirty) {
    xSemaphoreTake(_lock, portMAX_DELAY);
    _draftDirty = false;
    _lastSwitch = now;
    prepare();
    render(animationMask(*_anim, _unitMs, 0), 0);
    xSemaphoreGive(_lock);
    return;
  }
  if (!_draftActive && sets.size() >= 2 && now - _lastSwitch >= _store.intervalSec() * 1000UL) { showSet(_current + 1); return; }
  const uint32_t elapsed = now - _lastSwitch;
  const uint8_t m = _anim ? animationMask(*_anim, _unitMs, elapsed) : 7;
  if (frameKey(m, elapsed) == _lastKey) return;
  xSemaphoreTake(_lock, portMAX_DELAY);
  if (m != _lastMask) render(m, elapsed);
  else renderSprites(m, elapsed);
  xSemaphoreGive(_lock);
}
