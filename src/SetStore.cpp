#include "SetStore.h"
#include <Preferences.h>
#include <algorithm>

static const char* NS = "stoplight";
static const uint8_t DEF_BG[3] = {48, 52, 12};

void lightsToJson(JsonObject o, const LightDef l[3]) {
  JsonArray t = o["t"].to<JsonArray>(), k = o["k"].to<JsonArray>(), g = o["g"].to<JsonArray>();
  JsonArray s = o["s"].to<JsonArray>(), c = o["c"].to<JsonArray>(), f = o["f"].to<JsonArray>();
  for (int i = 0; i < 3; ++i) {
    t.add(l[i].text); k.add(l[i].kind); g.add(l[i].graphic);
    s.add(l[i].size); c.add(l[i].bg); f.add(l[i].ink);
  }
  if (l[0].aFrom || l[1].aFrom || l[2].aFrom || l[0].aLen || l[1].aLen || l[2].aLen) {
    JsonArray af = o["af"].to<JsonArray>(), al = o["al"].to<JsonArray>();
    for (int i = 0; i < 3; ++i) { af.add(l[i].aFrom); al.add(l[i].aLen); }
  }
  if (l[0].gmode || l[1].gmode || l[2].gmode) {
    JsonArray gm = o["gm"].to<JsonArray>();
    for (int i = 0; i < 3; ++i) gm.add(l[i].gmode);
  }
  if (l[0].hueBg || l[1].hueBg || l[2].hueBg || l[0].hueInk || l[1].hueInk || l[2].hueInk) {
    JsonArray hc = o["hc"].to<JsonArray>(), hf = o["hf"].to<JsonArray>();
    for (int i = 0; i < 3; ++i) { hc.add(l[i].hueBg); hf.add(l[i].hueInk); }
  }
  bool quarters = false;
  for (int i = 0; i < 3; ++i) quarters |= l[i].graphic == "fx:quarters";
  if (quarters) {
    JsonArray q = o["q"].to<JsonArray>();
    for (int i = 0; i < 3; ++i) { JsonArray qi = q.add<JsonArray>(); for (uint8_t v : l[i].quarters) qi.add(v); }
  }
}

void lightsFromJson(JsonObjectConst o, LightDef l[3]) {
  for (int i = 0; i < 3; ++i) {
    l[i].text = (const char*)(o["t"][i] | "");
    l[i].kind = (const char*)(o["k"][i] | (l[i].text.indexOf('\n') >= 0 ? "long" : "short"));
    l[i].graphic = (const char*)(o["g"][i] | "");
    if (l[i].kind == "off") { l[i].text = ""; l[i].graphic = ""; }
    l[i].on = l[i].kind != "off";
    l[i].size = o["s"][i] | 0;
    l[i].gmode = o["gm"][i] | 0; if (l[i].gmode > 2) l[i].gmode = 0;
    l[i].aFrom = o["af"][i] | 0; l[i].aLen = o["al"][i] | 0;
    if (l[i].aFrom > 600000) l[i].aFrom = 0;
    if (l[i].aLen > 600000) l[i].aLen = 0;
    l[i].bg = o["c"][i] | DEF_BG[i];
    l[i].ink = o["f"][i] | 0;
    if (l[i].bg > COLOUR_RANDOM) l[i].bg = DEF_BG[i];
    if (l[i].ink > COLOUR_RANDOM) l[i].ink = 0;
    const int hc = o["hc"][i] | 0, hf = o["hf"][i] | 0;
    l[i].hueBg = hc % 360; l[i].hueInk = hf % 360;
    static const uint8_t DEF_Q[3] = {60, 12, 3};
    for (int k = 0; k < 3; ++k) {
      const int v = o["q"][i][k] | (int)DEF_Q[k];
      l[i].quarters[k] = (v < 0 || v > COLOUR_RANDOM) ? DEF_Q[k] : v;
    }
  }
}

void frameFromJson(JsonObjectConst o, KeyFrame& f) {
  lightsFromJson(o, f.l);
  if (o["m"].is<int>() && !o["k"].is<JsonArrayConst>())    // legacy: mask without kinds
    for (int i = 0; i < 3; ++i) f.l[i].on = ((int)o["m"] >> i) & 1;
  uint32_t ms = o["ms"] | 1000;
  f.ms = ms < 100 ? 100 : ms > 600000 ? 600000 : ms;
  int t = o["mp3"] | 0;
  f.track = t < -2 ? -2 : t > 3000 ? 3000 : t;
  float fs = o["fade"] | 3.0f;
  f.fadeMs = fs < 0.2f ? 200 : fs > 60 ? 60000 : (uint16_t)(fs * 1000);
}

void setToJson(JsonObject o, const TextSet& s) {
  o["name"] = s.name;
  o["bpm"] = s.bpm;
  JsonArray fr = o["frames"].to<JsonArray>();
  for (const KeyFrame& f : s.frames) {
    JsonObject fo = fr.add<JsonObject>();
    lightsToJson(fo, f.l);
    fo["m"] = f.mask();
    fo["ms"] = f.ms;
    if (f.track) fo["mp3"] = f.track;
    if (f.track == -2) fo["fade"] = f.fadeMs / 1000.0f;
  }
  JsonArray loops = o["loops"].to<JsonArray>();
  for (const Loop& L : s.loops) {
    JsonObject lo = loops.add<JsonObject>();
    lo["from"] = L.from; lo["to"] = L.to; lo["count"] = L.count;
  }
}

std::vector<uint16_t> TextSet::playlist() const {
  std::vector<uint16_t> out;
  const int n = frames.size();
  int i = 0;
  while (i < n) {
    const Loop* L = nullptr;
    for (const Loop& x : loops) if ((int)x.from - 1 == i) { L = &x; break; }
    if (L && (int)L->to <= n && L->to >= L->from) {
      for (int c = 0; c < L->count; ++c)
        for (int k = i; k < (int)L->to; ++k) out.push_back(k);
      i = L->to;
    } else {
      out.push_back(i); ++i;
    }
    if (out.size() > 4000) break;   // sanity cap
  }
  return out;
}

void setFromJson(JsonObjectConst o, TextSet& s) {
  s = TextSet();
  s.name = (const char*)(o["name"] | "");
  if (s.name.length() > 40) s.name = s.name.substring(0, 40);
  const int bpm = o["bpm"] | 120;
  s.bpm = bpm < 10 ? 10 : bpm > 400 ? 400 : bpm;
  if (o["frames"].is<JsonArrayConst>()) {
    for (JsonObjectConst fo : o["frames"].as<JsonArrayConst>()) {
      KeyFrame f; frameFromJson(fo, f); s.frames.push_back(f);
      if (s.frames.size() >= SetStore::MAX_FRAMES) break;
    }
  } else {
    KeyFrame f; frameFromJson(o, f);     // legacy single-frame ("lights") set
    f.ms = 1000;
    s.frames.push_back(f);
  }
  // Loop sections: 1-based inclusive, sorted, non-overlapping, inside the set.
  for (JsonObjectConst lo : o["loops"].as<JsonArrayConst>()) {
    Loop L; L.from = lo["from"] | 0; L.to = lo["to"] | 0; L.count = lo["count"] | 1;
    if (L.from < 1 || L.to < L.from || L.to > s.frames.size() || L.count < 1) continue;
    if (L.count > 1000) L.count = 1000;
    bool overlap = false;
    for (const Loop& x : s.loops) if (!(L.to < x.from || L.from > x.to)) overlap = true;
    if (overlap) continue;
    s.loops.push_back(L);
    if (s.loops.size() >= SetStore::MAX_LOOPS) break;
  }
  std::sort(s.loops.begin(), s.loops.end(), [](const Loop& a, const Loop& b) { return a.from < b.from; });
}

void SetStore::load() {
  Preferences p;
  p.begin(NS, false);   // read-write so the namespace is created on first boot
  size_t n = p.getBytesLength("sets");
  String json;
  if (n > 0) {
    std::vector<char> buf(n + 1);
    p.getBytes("sets", buf.data(), n);
    buf[n] = 0;
    json = buf.data();
  }
  p.end();
  _sets.clear();
  if (json.isEmpty()) {
    TextSet ex;                              // first boot: one example set
    ex.name = "Example";
    KeyFrame f;
    f.l[0].text = "STOP"; f.l[1].text = "WAIT"; f.l[2].text = "GO";
    for (int i = 0; i < 3; ++i) f.l[i].bg = DEF_BG[i];
    f.ms = 10000;
    ex.frames.push_back(f);
    _sets.push_back(ex);
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, json)) return;
  for (JsonObjectConst o : doc["sets"].as<JsonArrayConst>()) {
    TextSet t; setFromJson(o, t);
    if (!t.frames.empty()) _sets.push_back(t);
    if (_sets.size() >= MAX_SETS) break;
  }
}

String SetStore::toJson() const {
  JsonDocument doc;
  JsonArray arr = doc["sets"].to<JsonArray>();
  for (const TextSet& s : _sets) setToJson(arr.add<JsonObject>(), s);
  String out;
  serializeJson(doc, out);
  return out;
}

bool SetStore::save() {
  String json = toJson();
  if (json.length() > MAX_JSON_BYTES) return false;
  Preferences p;
  p.begin(NS, false);
  p.putBytes("sets", json.c_str(), json.length());
  p.end();
  return true;
}
