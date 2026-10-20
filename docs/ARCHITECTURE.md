# Architecture

## Pipeline

```
web page (phone)  --JSON/RLE-->  WebUI (core 0)  --sets, drafts-->  SetStore (NVS)
                                                     |
                                                     v  (mutex)
                                  StopLightApp (core 1, Arduino loop)
                                     | prepare(): one Prepared frame per keyframe (layouts, sprites, colours)
                                     | loop(): keyframe timing, loop count/seconds, sprite frames, drafts
                                     v
                                  Face::draw / drawSpriteBoxes  --Canvas-->  DisplayCanvas -> SnakeDisplay -> HUB75 DMA
                                                                          \-> BitmapCanvas -> BMP / RLE (previews)
```

## Modules

| File | Role | Arduino-free? |
| --- | --- | --- |
| `SnakeDisplay.*` | Wraps ESP32-HUB75-MatrixPanel-DMA. 128×384 canvas over a 768×64 DMA chain, odd panels rotated 180°. `Options` (clock, min refresh, double buffer), `setPixel2` (0..3 levels), `show()`. `PANEL_COUNT` build flag. | no |
| `PanelPins.h` | The 14 GPIOs for the S3 in the adapter board rev 2.x (bench-verified). | no |
| `Canvas.h` | `Rgb2` colour = 0bRRGGBB (0..63), helpers, `Canvas` interface. | yes |
| `Font.h`, `fonts/StopFonts.h` | Glyph format + generated DejaVu Sans Bold bitmaps, 12 sizes. | yes |
| `TextLayout.*` | UTF-8 decode, nikud strip, simple bidi (Hebrew runs reversed, brackets mirrored), word split with forced breaks (`\n`), exhaustive line-break search per font size (largest size, fewest lines, most balanced), ink-centred placement inside a circle, `maxFontSize` cap. | yes |
| `Sprite.h`, `sprites/Sprites.h` | 1-bpp animated pictograms: figures at 64×96 (standing, ready, breakdance, walking, running, hug, high five, bow, DJ, jump, sun salutation, bike, dancing) and the four "eclipse" discs at 128×128 (a small or full-size disc crossing the light left→right or top→bottom, 24 frames). `Face::drawSprite` clips every frame to the circle, so a frame may be the whole 128×128 light. | yes |
| `Face.*` | Draws three 128 px circles with text or sprite, per-light colours, lit mask (unlit = dim 2 px ring), `drawSpriteBoxes` for cheap frame updates. Font/sprite lookup. | yes |
| `Preview.*` | `BitmapCanvas` (4 bpp, per-image palette ≤16) → BMP (tools) or RLE stream (page). | yes |
| `DisplayCanvas.h` | Canvas → SnakeDisplay. | no |
| `SetStore.*` | `TextSet` + JSON persistence in NVS (`Preferences`, blob "sets"). | no |
| `StopLightApp.*` | Plays keyframe sets through the expanded playlist, prepares frames just in time (two slots), sprite timing, live draft, recursive mutex held for the whole render pass, full vs sprite-box renders; starts a frame's audio via `Mp3Player`. | no |
| `Mp3Player.*` | Serial driver for the MP3 module on UART1 (play, stop, volume, linear fade-out, file count, start-up sequence, reply parsing); polled from the render loop. A play during a fade cancels it and restores the saved volume. | no |
| `AnimStore.*` | Uploaded animations in LittleFS (`/a/<id>.anm`): list (cached in RAM), load the frame table, validate an upload, paint a frame (formats 1/2 decoded on the spot; format 3 by two background decoder tasks into PSRAM frame rings, see CLAUDE.md). | no |
| `AnimV3.*` | Portable format 3 decoder (context-model range coder): per-clip `V3Reader`, per-decoder `V3Scratch`, streaming row output. Encoder: `tools/anim_v3.py`; JS twin: `tools/anim_v3.js`. Masked clips (header flag bit 1) code only each light's circle + 2 px. | yes |
| `SerialLink.*` | Animation upload/list/delete/read-back/rename over the UART and native USB sockets ("#ANIM" lines plus CRC-checked packets with resend), plus diagnostics (PERF, BENCH, PLAY), polled from the web task. | no |
| `WebUI.*`, `WebPage.h` | SoftAP + captive DNS + HTTP API + the single-page UI (PROGMEM, self-contained, no external assets). | no |
| `main.cpp` | Display init, store, app, AP; web task on core 0, render loop on core 1. | no |

## Data model

A set (`TextSet`, JSON in NVS and in the API) is a named keyframe sequence.
Light arrays are always indexed top, middle, bottom.

```json
{"name":"Morning",
 "frames":[
   {"t":["STOP","המתן",""],          // texts (UTF-8), ignored for a graphic or an off light
    "k":["short","long","off"],      // short/long = which text box the page shows; off = light dark
    "g":["","",""],                  // sprite id per light, "" = text
    "s":[0,36,0],                    // max font em size per light, 0 = automatic
    "c":[48,52,12],                  // circle colours, Rgb2 0bRRGGBB (0 = black); 64 = random, never black
    "f":[0,0,64],                    // ink colours; 64 = random, never equal to the circle
    "m":3, "ms":800, "mp3":2},       // m = lit mask (derived from k on the device), ms = duration,
                                      // mp3: 0 = leave sound as is, -1 = stop, -2 = fade out over "fade" seconds
                                      //      (default 3), n = play track n when the frame starts
    // optional per light, for g = "u:<id>" (uploaded animation):
    // "gm":[0,1,2] 0 original colours, 1 silhouette (ink on circle colour), 2 inverted;
    // "af"/"al": play only [af, af+al) ms of the clip, al 0 = to the end.
    // A 128x384 animation on any light fills the whole sign for that frame.
   ...],
 "loops":[{"from":6,"to":10,"count":10},  // loop sections, 1-based inclusive, non-overlapping
          {"from":12,"to":14,"count":4}]}
```

Playback expands the loops into a playlist (`TextSet::playlist()`): frames
1–5, 6–10 ten times, 11, 12–14 four times, then the next set (a lone set
starts over). Frames are laid out just in time, the current one plus the
following one, so a long set costs no extra RAM. Random colours are rolled
when a frame is prepared (so a looped frame re-rolls every pass) and never
repeat either of the two previous random picks. A legacy set without
`frames` loads as one frame. Store: `{"sets":[...]}`, capped at 32 KB of
JSON (`SetStore::MAX_JSON_BYTES`), 40 sets, 64 frames and 16 loops per set.

## HTTP API (all on the AP, http://192.168.4.1/)

| Call | Effect |
| --- | --- |
| `GET /` | the page |
| `GET /api/state` | `{current, frame, live, heap, heapMin, heapBlock, sets, sizes, sprites, mp3, settings:{offDark}}` (`frame` = 0-based frame being shown) |
| `GET /api/check?text=&light=&size=` | `{ok,size,lines,maxFit}` or `{ok:false,error,maxFit}` |
| `GET /api/preview.rle?t0..t2&s0..s2&c0..c2&f0..f2&g0..g2&k0..k2&m=&k=` | RLE frame (`kN=off` = light dark, `m` lit mask, `k` elapsed ms for sprite frames). Unlit lights follow the stored `offDark` setting; `stopcli` and the harness take it as `o=1` |
| `GET /api/preview.bmp?...` | same as 4-bpp BMP |
| `POST /api/sets` (set JSON) | add; 400 with reason if a text does not fit or storage is full |
| `PUT /api/sets/{i}` (set JSON) | replace set i (the page's Edit) |
| `DELETE /api/sets/{i}` | remove |
| `POST /api/show/{i}` | show now, restart timer |
| `POST /api/live` (frame JSON: t,k,g,s,c,f,m) / `DELETE /api/live` | show a draft frame on the LEDs without saving / back to sets (auto-clears after 60 s idle) |
| `POST /api/settings {offDark:bool, rotate180:bool}` | global settings, each optional: `offDark` = unlit light completely dark instead of a dim outline ring (NVS `offdark`, `Face::setOffDark`); `rotate180` = whole image rotated 180° for a sign hung the other way up (NVS `rot180`, default true since 2026-10-19, `SnakeDisplay::setRotated`, applied in `mapToChain`); `colourRotated` = panels whose colour inputs are shifted one step (NVS `colrot`, default true since 2026-10-19, `Options::colourRotated` swaps the R/G/B pins at init), so the device replies `{restart:true}` and restarts. The other two redraw at once |
| `GET /api/anims` | `{anims:[{id,full,frames,ms,bytes}], free, total}` (also `anims`/`animFree`/`animTotal` in `/api/state`) |
| `POST /api/anims?name=` (multipart file) | store an .anm (validated, written to `/a/.upload`, renamed under the playback lock) |
| `POST /api/anims/{id}/rename {name}` | rename; stored sets that use it follow; 400 if the name is taken |
| `GET /api/anims/{id}.anm`, `DELETE /api/anims/{id}` | the raw file (the page's preview decodes it) / remove |
| `POST /api/mp3/play/{n}`, `POST /api/mp3/stop`, `POST /api/mp3/volume {volume}`, `POST /api/mp3/rescan` | sound module; state carries `mp3:{present,tracks,volume,error}` |

RLE format: `'R','L', W u16, H u16, nPal u8, nPal×RGB888, then u16 LE runs
(index<<12 | length ≤4095)`.

## Rendering costs (S3, 2 panels)

Full face ≈ 49k `setPixel2` calls (tens of ms); sprite box ≈ 6k. A full redraw
happens on every keyframe change, set change or draft change (and is written
to both DMA buffers when double-buffered); sprite frames only repaint their
boxes. Refresh itself is DMA
(108 Hz on six panels at 10 MHz, 325 Hz on two).
