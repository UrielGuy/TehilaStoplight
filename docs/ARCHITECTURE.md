# Architecture

## Pipeline

```
web page (phone)  --JSON/RLE-->  WebUI (core 0)  --sets, drafts-->  SetStore (NVS)
                                                     |
                                                     v  (mutex)
                                  StopLightApp (core 1, Arduino loop)
                                     | prepare(): TextLayout per light, sprite, colours, animation
                                     | loop(): rotation timer, animation mask, sprite frames, drafts
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
| `Sprite.h`, `sprites/Sprites.h` | 1-bpp animated pictograms 64×96 (standing, ready, dancing). | yes |
| `Face.*` | Draws three 128 px circles with text or sprite, per-light colours, lit mask (unlit = dim 2 px ring), `drawSpriteBoxes` for cheap frame updates. Font/sprite lookup. | yes |
| `Animation.*` | Light animations as step lists (mask, units). `animationMask(anim, unitMs, elapsedMs)`. | yes |
| `Preview.*` | `BitmapCanvas` (4 bpp, per-image palette ≤16) → BMP (tools) or RLE stream (page). | yes |
| `DisplayCanvas.h` | Canvas → SnakeDisplay. | no |
| `SetStore.*` | `TextSet` + JSON persistence in NVS (`Preferences`, blob "sets"). | no |
| `StopLightApp.*` | Current set, rotation, animation/sprite timing, live draft, mutex, full vs partial renders. | no |
| `WebUI.*`, `WebPage.h` | SoftAP + captive DNS + HTTP API + the single-page UI (PROGMEM, self-contained, no external assets). | no |
| `main.cpp` | Display init, store, app, AP; web task on core 0, render loop on core 1. | no |

## Data model

A set (`TextSet`, JSON in NVS and in the API):

```json
{"t":["STOP","המתן","GO"],   // texts (UTF-8), ignored for a light with a graphic
 "s":[0,36,0],               // max font em size per light, 0 = automatic
 "c":[48,52,12],             // circle colours, Rgb2 0bRRGGBB (red, orange, green)
 "f":[0,0,0],                // ink colours (text/figure), default black
 "g":["","","dancing"],      // sprite id per light, "" = text
 "a":"traffic", "p":800}     // light animation id and unit in ms
```

Store: `{"interval":60,"sets":[...]}`. Rotation every `interval` seconds
(5..86400) when there are ≥2 sets.

## HTTP API (all on the AP, http://192.168.4.1/)

| Call | Effect |
| --- | --- |
| `GET /` | the page |
| `GET /api/state` | `{interval, current, live, sets, sizes, animations, sprites}` |
| `GET /api/check?text=&light=&size=` | `{ok,size,lines,maxFit}` or `{ok:false,error,maxFit}` |
| `GET /api/preview.rle?t0..t2&s0..s2&c0..c2&f0..f2&g0..g2&m=&k=` | RLE frame (`m` lit mask, `k` elapsed ms for sprite frames) |
| `GET /api/preview.bmp?...` | same as 4-bpp BMP |
| `POST /api/sets` (set JSON) | add; 400 with reason if a text does not fit |
| `DELETE /api/sets/{i}` | remove |
| `POST /api/show/{i}` | show now, restart timer |
| `POST /api/interval {seconds}` | rotation interval |
| `POST /api/live` (set JSON) / `DELETE /api/live` | show a draft on the LEDs without saving / back to sets (auto-clears after 60 s idle) |

RLE format: `'R','L', W u16, H u16, nPal u8, nPal×RGB888, then u16 LE runs
(index<<12 | length ≤4095)`.

## Rendering costs (S3, 2 panels)

Full face ≈ 49k `setPixel2` calls (tens of ms); sprite box ≈ 6k. A full redraw
happens on set change, draft change or lit-mask change (and is written to both
DMA buffers); sprite frames only repaint their boxes. Refresh itself is DMA
(108 Hz on six panels at 10 MHz, 325 Hz on two).
