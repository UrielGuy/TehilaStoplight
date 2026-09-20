#include "Animation.h"

#define TOP 1
#define MID 2
#define BOT 4
#define ALL 7

static const AnimStep S_ALL[]        = {{ALL, 1}};
static const AnimStep S_BLINK[]      = {{ALL, 1}, {0, 1}};
static const AnimStep S_DOWN_FILL[]  = {{TOP, 1}, {TOP | MID, 1}, {ALL, 3}};
static const AnimStep S_UP_FILL[]    = {{BOT, 1}, {BOT | MID, 1}, {ALL, 3}};
static const AnimStep S_DOWN_DRAIN[] = {{ALL, 3}, {MID | BOT, 1}, {BOT, 1}, {0, 1}};
static const AnimStep S_UP_DRAIN[]   = {{ALL, 3}, {TOP | MID, 1}, {TOP, 1}, {0, 1}};
static const AnimStep S_DOWN_ONE[]   = {{TOP, 1}, {MID, 1}, {BOT, 1}};
static const AnimStep S_UP_ONE[]     = {{BOT, 1}, {MID, 1}, {TOP, 1}};
static const AnimStep S_BOUNCE[]     = {{TOP, 1}, {MID, 1}, {BOT, 1}, {MID, 1}};
static const AnimStep S_TRAFFIC[]    = {{TOP, 4}, {TOP | MID, 1}, {BOT, 4}, {MID, 1}};      // European
static const AnimStep S_TRAFFIC_US[] = {{TOP, 4}, {BOT, 4}, {MID, 1}};                     // no red+amber
static const AnimStep S_PEDESTRIAN[] = {{TOP, 4}, {BOT, 4}, {BOT, 1}, {0, 1}, {BOT, 1}, {0, 1}, {BOT, 1}, {0, 1}};  // green blinks before red
static const AnimStep S_TOP_BLINK[]  = {{ALL, 1}, {MID | BOT, 1}};
static const AnimStep S_ROLL[]       = {{TOP, 1}, {MID, 1}, {BOT, 1}, {0, 1}};

static const Animation ANIMS[] = {
  {"all",        "All lit (steady)",                          S_ALL,        1},
  {"blink",      "All blink together",                        S_BLINK,      2},
  {"down-fill",  "Fill top to bottom",                        S_DOWN_FILL,  3},
  {"up-fill",    "Fill bottom to top",                        S_UP_FILL,    3},
  {"down-drain", "All lit, then empty top to bottom",         S_DOWN_DRAIN, 4},
  {"up-drain",   "All lit, then empty bottom to top",         S_UP_DRAIN,   4},
  {"down-one",   "One at a time, top to bottom",              S_DOWN_ONE,   3},
  {"up-one",     "One at a time, bottom to top",              S_UP_ONE,     3},
  {"bounce",     "One at a time, bouncing",                   S_BOUNCE,     4},
  {"roll",       "One at a time with a pause",                S_ROLL,       4},
  {"traffic",    "Traffic light (red, red+amber, green, amber)", S_TRAFFIC,  4},
  {"traffic-us", "Traffic light (red, green, amber)",         S_TRAFFIC_US, 3},
  {"pedestrian", "Pedestrian (green blinks before red)",      S_PEDESTRIAN, 8},
  {"top-blink",  "Top blinks, others steady",                 S_TOP_BLINK,  2},
};
static const int N_ANIMS = sizeof(ANIMS) / sizeof(ANIMS[0]);

int animationCount() { return N_ANIMS; }
const Animation& animationAt(int i) { return ANIMS[(i < 0 || i >= N_ANIMS) ? 0 : i]; }

const Animation& animationById(const std::string& id) {
  for (int i = 0; i < N_ANIMS; ++i) if (id == ANIMS[i].id) return ANIMS[i];
  return ANIMS[0];
}

uint8_t animationMask(const Animation& a, uint32_t unitMs, uint32_t elapsedMs) {
  if (a.nSteps <= 1 || unitMs == 0) return a.steps[0].mask;
  const uint32_t cycle = (uint32_t)a.cycleUnits() * unitMs;
  uint32_t t = elapsedMs % cycle;
  for (int i = 0; i < a.nSteps; ++i) {
    const uint32_t d = a.steps[i].units * unitMs;
    if (t < d) return a.steps[i].mask;
    t -= d;
  }
  return a.steps[0].mask;
}
