#pragma once
#include "Arduino.h"
// Stand-in for src/AccessPoint.h (WiFi): SerialLink's PERF reply only asks for the web loop stats.
namespace AccessPoint {
inline String webStats(bool) { return "{}"; }
}
