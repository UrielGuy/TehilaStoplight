#pragma once
#include "Arduino.h"
// Stand-in for src/AccessPoint.h (WiFi): SerialLink's PERF reply only asks for the web loop stats.
namespace AccessPoint {
inline String webStats(bool) { return "{}"; }
inline String station(const String&) { return "off"; }
inline bool webLog(int = -1) { return false; }
inline bool probesOnline(int = -1) { return false; }
}
