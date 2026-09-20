#include "SetStore.h"
#include <ArduinoJson.h>
#include <Preferences.h>

static const char* NS = "stoplight";

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
  _intervalSec = 60;
  if (json.isEmpty()) {
    // First boot: one example set the user can delete.
    TextSet ex;
    ex.text[0] = "STOP"; ex.text[1] = "WAIT"; ex.text[2] = "GO";
    _sets.push_back(ex);
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, json)) return;
  _intervalSec = doc["interval"] | 60;
  for (JsonObject o : doc["sets"].as<JsonArray>()) {
    TextSet t;
    for (int i = 0; i < 3; ++i) {
      t.text[i] = (const char*)(o["t"][i] | "");
      t.size[i] = o["s"][i] | 0;
      t.colour[i] = o["c"][i] | (i == 0 ? 48 : i == 1 ? 52 : 12);
      t.ink[i] = o["f"][i] | 0;
      t.graphic[i] = (const char*)(o["g"][i] | "");
    }
    t.anim = (const char*)(o["a"] | "all");
    t.unitMs = o["p"] | 1000;
    _sets.push_back(t);
    if (_sets.size() >= MAX_SETS) break;
  }
}

String SetStore::toJson() const {
  JsonDocument doc;
  doc["interval"] = _intervalSec;
  JsonArray arr = doc["sets"].to<JsonArray>();
  for (const TextSet& s : _sets) {
    JsonObject o = arr.add<JsonObject>();
    JsonArray t = o["t"].to<JsonArray>();
    JsonArray sz = o["s"].to<JsonArray>();
    JsonArray col = o["c"].to<JsonArray>();
    JsonArray ink = o["f"].to<JsonArray>();
    JsonArray gr = o["g"].to<JsonArray>();
    for (int i = 0; i < 3; ++i) { t.add(s.text[i]); sz.add(s.size[i]); col.add(s.colour[i]); ink.add(s.ink[i]); gr.add(s.graphic[i]); }
    o["a"] = s.anim;
    o["p"] = s.unitMs;
  }
  String out;
  serializeJson(doc, out);
  return out;
}

void SetStore::save() {
  String json = toJson();
  Preferences p;
  p.begin(NS, false);
  p.putBytes("sets", json.c_str(), json.length());
  p.end();
}
