# TehilaStoplight — LED traffic-light sign

Six 128×64 P2 HUB75E panels stacked vertically (128×384 px), driven by an
ESP32-S3 in a HUB75 adapter board. Shows three coloured circles with text
(English or Hebrew) or animated figures, managed from a phone on the sign's own
WiFi (SSID `StopLight`, password `UriNelli`, http://192.168.4.1/).

**Start with `CLAUDE.md`** (build/flash/harness/gotchas), then
`docs/ARCHITECTURE.md` (modules, data model, API) and `docs/HARDWARE.md`
(wiring, pin maps, bench findings).

## Quick start

```
export PATH="$HOME/.local/bin:$PATH"
pio run -e s3-2                      # two-panel bench; -e s3 for all six panels
python3 tools/local_server.py        # the web UI locally, no hardware: http://localhost:8080/
```

Flashing goes through Windows Python (WSL has no USB serial), see `CLAUDE.md`.

## Local harness (no board, no WiFi switching)

```
python3 tools/local_server.py        # then open http://localhost:8080/
```

Serves the exact page from `src/WebPage.h` and the same API. Fit checks and
the preview image come from `tools/stopcli`, built automatically from the same
`TextLayout.cpp` / `Face.cpp` / `Preview.cpp` the firmware uses, so what you
see is what the LEDs will show. Sets are kept in `tools/local_sets.json`.
"Show now" and live drafts just print to the terminal. Needs `g++` and Python 3.

`tools/stopcli check <light> <text> [maxSize]` and
`tools/stopcli preview <t0> <t1> <t2> [s0 s1 s2]` also work on their own; `tools/stopcli ppm ...` gives a PPM and `tools/stopcli sizes` lists
the font sizes. A size argument caps the font (0 = automatic), which is what
the page's A−/A+ buttons set and what is stored as `s` next to `t` in a set.
