#include "StopLightApp.h"
#include "DisplayCanvas.h"
#include <esp_random.h>

// Random non-black colour, different from `avoid` and from the last two
// random picks (so three randoms side by side always differ).
Rgb2 StopLightApp::randomColour(Rgb2 avoid) {
  Rgb2 c = 1;
  for (int tries = 0; tries < 32; ++tries) {
    c = 1 + esp_random() % 63;
    if (c != avoid && c != _recent[0] && c != _recent[1]) break;
  }
  _recent[1] = _recent[0]; _recent[0] = c;
  return c;
}

void StopLightApp::prepareFrame(Prepared& p, const LightDef l[3], uint8_t mask, uint32_t ms) {
  p.track = 0;
  for (int i = 0; i < Face::LIGHTS; ++i) {
    p.layouts[i] = TextLayout();
    p.specs[i] = Face::LightSpec();
    p.specs[i].sprite = Face::spriteById(l[i].graphic.c_str());
    p.anim[i] = AnimInfo();
    p.animMode[i] = l[i].gmode;
    p.animFrom[i] = l[i].aFrom; p.animLen[i] = l[i].aLen;
    const bool uploaded = l[i].graphic.startsWith("u:");
    if (uploaded && l[i].kind != "off") AnimStore::load(l[i].graphic.substring(2), p.anim[i]);
    if (!p.specs[i].sprite && !uploaded && l[i].kind != "off") {
      p.layouts[i] = Face::layoutFor(i, l[i].text.c_str(), l[i].size);
      p.specs[i].text = &p.layouts[i];
    }
    p.specs[i].bg = (l[i].bg == COLOUR_RANDOM) ? randomColour(RGB2_BLACK) : (l[i].bg & 63);
    p.specs[i].fg = (l[i].ink == COLOUR_RANDOM) ? randomColour(p.specs[i].bg) : (l[i].ink & 63);
  }
  p.mask = mask;
  p.ms = ms;
  p.fullLight = -1;
  for (int i = 0; i < Face::LIGHTS; ++i)
    if (p.anim[i].ok() && p.anim[i].full()) { p.fullLight = i; break; }
}

// Uploaded animations: a whole-sign one replaces the face, circle ones are
// drawn into their (lit) lights over the plain circle.
bool StopLightApp::drawAnims(Canvas& c, const Prepared& p, uint32_t elapsedMs) {
  if (p.fullLight >= 0) {
    const int i = p.fullLight;
    return AnimStore::drawFrame(p.anim[i], p.animFrame(i, elapsedMs), c, 0, 0, false, p.animMode[i], p.specs[i].fg, p.specs[i].bg);
  }
  bool all = true;
  for (int i = 0; i < Face::LIGHTS; ++i)
    if (p.anim[i].ok() && ((p.mask >> i) & 1))
      all &= AnimStore::drawFrame(p.anim[i], p.animFrame(i, elapsedMs), c, 0, i * Face::DIAMETER, true, p.animMode[i], p.specs[i].fg, p.specs[i].bg);
  return all;
}

void StopLightApp::preparePos(Prepared& p, int pos) {
  auto& sets = _store.sets();
  if (_draftActive) { prepareFrame(p, _draft.l, _draft.mask(), 1000); p.pos = 0; return; }
  if (sets.empty() || _current < 0 || _current >= (int)sets.size() || _playlist.empty()) {
    static const LightDef blank[3];
    prepareFrame(p, blank, 7, 1000); p.pos = 0; return;
  }
  const KeyFrame& f = sets[_current].frames[_playlist[pos]];
  prepareFrame(p, f.l, f.mask(), f.ms);
  p.track = f.track;
  p.fadeMs = f.fadeMs;
  p.pos = pos;
}

// Audio tied to a frame: play its track or stop, when the frame starts.
void StopLightApp::startAudio(const Prepared& p) {
  if (!_mp3 || _draftActive) return;
  if (p.track > 0) _mp3->play(p.track);
  else if (p.track == -1) _mp3->stop();
  else if (p.track == -2) _mp3->fadeOut(p.fadeMs);
}

void StopLightApp::startSet() {
  auto& sets = _store.sets();
  _playlist.clear();
  if (!_draftActive && !sets.empty() && _current >= 0 && _current < (int)sets.size()) _playlist = sets[_current].playlist();
  _pos = 0;
  _next->pos = -1;
  preparePos(*_cur, 0);
  _lastKey = 0xFFFFFFFF;
}

uint32_t StopLightApp::frameKey(const Prepared& p, uint32_t elapsedMs) const {
  uint32_t k = (uint32_t)p.pos + 1;
  for (int i = 0; i < Face::LIGHTS; ++i)
    if (p.specs[i].sprite) k = k * 64 + Face::spriteFrame(*p.specs[i].sprite, elapsedMs);
  for (int i = 0; i < Face::LIGHTS; ++i)
    if (p.anim[i].ok()) k = k * 1000003u + (uint32_t)p.animFrame(i, elapsedMs);
  return k;
}

bool StopLightApp::render(const Prepared& p, uint32_t elapsedMs) {
  DisplayCanvas c(_display);
  bool painted = true;
  for (int pass = 0; pass < (_display.isDoubleBuffered() ? 2 : 1); ++pass) {   // keep both buffers equal
    if (p.fullLight < 0) Face::draw(c, p.specs, p.mask, elapsedMs);
    painted &= drawAnims(c, p, elapsedMs);
    if (pass == 0) _display.show();
  }
  return painted;
}

bool StopLightApp::renderSprites(const Prepared& p, uint32_t elapsedMs) {
  DisplayCanvas c(_display);
  if (p.fullLight < 0) Face::drawSpriteBoxes(c, p.specs, p.mask, elapsedMs);
  const bool painted = drawAnims(c, p, elapsedMs);
  _display.show();
  return painted;
}

void StopLightApp::renderCurrent() {
  xSemaphoreTakeRecursive(_lock, portMAX_DELAY);
  startSet();
  const bool painted = render(*_cur, 0);
  startAudio(*_cur);
  _lastKey = painted ? frameKey(*_cur, 0) : 0xFFFFFFFF;   // not ready: retry via renderSprites
  xSemaphoreGiveRecursive(_lock);
}

void StopLightApp::showSet(int index) {
  auto& sets = _store.sets();
  if (sets.empty()) _current = 0;
  else _current = ((index % (int)sets.size()) + sets.size()) % sets.size();
  _setStart = _frameStart = millis();
  renderCurrent();
}

void StopLightApp::onSetsChanged() {
  auto& sets = _store.sets();
  if (_current >= (int)sets.size()) _current = 0;
  _setStart = _frameStart = millis();
  renderCurrent();
}

void StopLightApp::setDraft(const KeyFrame& f) {
  xSemaphoreTakeRecursive(_lock, portMAX_DELAY);
  _draft = f;
  _draftActive = true;
  _draftDirty = true;
  _draftAt = millis();
  xSemaphoreGiveRecursive(_lock);
}

void StopLightApp::clearDraft() {
  if (!_draftActive) return;
  _draftActive = false;
  _setStart = _frameStart = millis();
  renderCurrent();
}

void StopLightApp::loop() {
  auto& sets = _store.sets();
  const uint32_t now = millis();
  ++AnimStore::dbgRenderBeat; AnimStore::dbgRenderStage = 1;
  const uint32_t l0 = micros();
  xSemaphoreTakeRecursive(_lock, portMAX_DELAY);   // the web task rebuilds state via renderCurrent()
  { const uint32_t w = micros() - l0; if (w > playPerf.maxLockWaitUs) playPerf.maxLockWaitUs = w; }
  if (_draftActive && now - _draftAt > LIVE_TIMEOUT_MS) { clearDraft(); xSemaphoreGiveRecursive(_lock); return; }
  if (_draftDirty) {
    _draftDirty = false;
    _setStart = _frameStart = now;
    AnimStore::dbgRenderStage = 2;
    startSet();
    AnimStore::dbgRenderStage = 3;
    const bool painted = render(*_cur, 0);
    _lastKey = painted ? frameKey(*_cur, 0) : 0xFFFFFFFF;   // not ready: retry via renderSprites
    xSemaphoreGiveRecursive(_lock);
    return;
  }
  uint32_t elapsed;
  if (_draftActive) {
    elapsed = now - _setStart;
  } else {
    if (!_playlist.empty() && now - _frameStart >= _cur->ms) {
      // Next playlist position; at the end, hand over to the next set (or
      // start over when this is the only one).
      _frameStart = now;
      if (++_pos >= (int)_playlist.size()) {
        if (sets.size() >= 2) { showSet(_current + 1); xSemaphoreGiveRecursive(_lock); return; }
        _pos = 0; _setStart = now;
      }
      if (_next->pos == _pos) { Prepared* t = _cur; _cur = _next; _next = t; }
      else preparePos(*_cur, _pos);
      _next->pos = -1;
      const bool painted = render(*_cur, 0);
      startAudio(*_cur);
      _lastKey = painted ? frameKey(*_cur, 0) : 0xFFFFFFFF;   // not ready: retry via renderSprites
      xSemaphoreGiveRecursive(_lock);
      return;
    }
    elapsed = now - _frameStart;
    // Idle time: lay out the following frame ahead of its turn.
    if (!_playlist.empty() && _playlist.size() > 1 && _next->pos != (_pos + 1) % (int)_playlist.size()) {
      const int np = (_pos + 1) % (int)_playlist.size();
      if (np != 0 || sets.size() < 2) preparePos(*_next, np);
      else _next->pos = -2;   // next set will be prepared by showSet()
    }
  }
  AnimStore::dbgRenderStage = 4;
  const uint32_t key = frameKey(*_cur, elapsed);
  if (key != _lastKey) {
    AnimStore::dbgRenderStage = 5;
    // Smoothness bookkeeping for the first uploaded animation in the frame.
    for (int i = 0; i < Face::LIGHTS; ++i) {
      if (!_cur->anim[i].ok()) continue;
      const int fi = _cur->animFrame(i, elapsed);
      if (fi != _perfLastFrame) {
        const uint32_t t = millis();
        const int n = _cur->anim[i].delays.size();
        if (_perfLastFrame >= 0) {
          const int step = (fi - _perfLastFrame + n) % n;
          if (step > 1) playPerf.skipped += step - 1;
          const uint32_t gap = t - _perfLastAt;
          if (gap > playPerf.maxGapMs) playPerf.maxGapMs = gap;
          if (gap > 2u * _cur->anim[i].delays[_perfLastFrame]) ++playPerf.late;
        }
        ++playPerf.shown; _perfLastFrame = fi; _perfLastAt = t;
      }
      break;
    }
    if (renderSprites(*_cur, elapsed)) _lastKey = key;   // a frame still decoding: try again next loop
  }
  { const uint32_t d = micros() - l0; if (d > playPerf.maxLoopUs) playPerf.maxLoopUs = d; }
  xSemaphoreGiveRecursive(_lock);
}
