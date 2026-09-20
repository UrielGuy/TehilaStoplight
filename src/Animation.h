#pragma once
#include <stdint.h>
#include <string>

// An animation is a cycle of steps; each step says which lights are lit
// (bit 0 = top/red, bit 1 = middle/orange, bit 2 = bottom/green) and how many
// time units it lasts. The unit length (ms) is chosen per set.
struct AnimStep { uint8_t mask; uint8_t units; };

struct Animation {
  const char* id;      // stored in the set JSON
  const char* name;    // shown on the page
  const AnimStep* steps;
  int nSteps;
  bool isStatic() const { return nSteps == 1; }
  int cycleUnits() const { int u = 0; for (int i = 0; i < nSteps; ++i) u += steps[i].units; return u; }
};

int animationCount();
const Animation& animationAt(int i);
const Animation& animationById(const std::string& id);   // unknown id -> "all"

// Mask to show `elapsedMs` after the animation started, with `unitMs` per unit.
uint8_t animationMask(const Animation& a, uint32_t unitMs, uint32_t elapsedMs);
