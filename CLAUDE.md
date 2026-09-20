# TehilaStoplight — guide for agents

A traffic-light sign built from six 128×64 P2 HUB75E LED panels (stacked
vertically, snake-chained, 128×384 pixels), driven by an ESP32-S3 in a HUB75
adapter board. It shows three coloured circles with text (English/Hebrew) or
animated figures, managed from a phone over the ESP32's own WiFi access point.

Read this file first. `docs/ARCHITECTURE.md` has the module map, data model
and API; `docs/HARDWARE.md` has wiring and bench findings. There is no git
repository in this folder.

## Current state (2026-09-20)

- Controller: **ESP32-S3-DevKitC-1** in a Seengreat/WatangTech "RGB Matrix
  Adapter Board (E)" **rev 2.2**. Pin map in `src/PanelPins.h` (the only one).
- Build env **`s3`** (six panels) or **`s3-2`** (bench with only the top two
  panels attached; that is what is flashed right now).
- Colour depth 2 bits/channel (64 colours), double-buffered.
- Wired end to end and tested on hardware: text sets, per-text size caps,
  per-light colours (circle and ink), built-in figure sprites, light
  animations, live-on-LEDs editing, rotation, pixel-exact preview.
- Not done: uploading images/GIFs from the page (design agreed, see TODO).

## Build, flash, monitor (from WSL)

PlatformIO lives in `~/.local/bin/pio` (pipx):

```
export PATH="$HOME/.local/bin:$PATH"
pio run -e s3-2        # or -e s3
```

**WSL cannot see USB serial ports.** Flash and monitor through Windows Python,
which has esptool 5 and pyserial installed:

```
python.exe -m esptool --chip esp32s3 --port COM14 --baud 460800 \
  --before default-reset --after hard-reset write-flash -z \
  --flash-mode keep --flash-freq keep --flash-size keep \
  0x10000 'C:\repos\TehilaStoplight\.pio\build\s3-2\firmware.bin'
```

A brand-new chip also needs `0x0 bootloader.bin 0x8000 partitions.bin 0xe000
boot_app0.bin` (boot_app0.bin from
`~/.platformio/packages/framework-arduinoespressif32/tools/partitions/`).

Find the port first; it changes when the user replugs:
`powershell.exe -NoProfile -Command "Get-PnpDevice -PresentOnly -Class Ports | Select FriendlyName"`.
The S3 has two USB sockets: **UART** (CH343, serial log works, COM14 last time)
and **USB** (native; flashing works, log does not since CDC-on-boot is off).

Read the log with a short pyserial script:

```
python.exe - <<'EOF'
import serial, time
s = serial.Serial('COM14', 115200, timeout=1); t0 = time.time(); buf = b''
while time.time() - t0 < 8: buf += s.read(4096)
s.close(); print(buf.decode('utf-8', 'replace'))
EOF
```

A healthy boot prints the DMA refresh line, "Free heap ...", and
"AP 'StopLight' up". The device API cannot be reached from WSL even when the
PC is on the `StopLight` network (something on Windows intercepts the route);
use the local harness for API work.

## Local harness (no hardware needed)

```
python3 tools/local_server.py          # http://localhost:8080/
```

Serves the exact page from `src/WebPage.h` and the same JSON API; rendering
comes from `tools/stopcli`, compiled from the same C++ as the firmware (rebuilt
automatically when sources change). Sets go to `tools/local_sets.json`;
"Show now" and live drafts print to the terminal. Stop it with
`fuser -k 8080/tcp`; **never `pkill -f local_server`** from a Bash tool call,
it matches the calling shell.

`tools/stopcli` subcommands: `sizes`, `anims`, `sprites`,
`check <light> <text> [maxSize]`, and `preview|rle|ppm key=value...` (keys are
the web API query keys, e.g. `t0=STOP g2=dancing k=440 c0=3 f0=63 m=7`).
To eyeball a layout: `stopcli ppm ... > x.ppm`, convert to PNG with Pillow,
view with the Read tool.

## Conventions and gotchas

- **Portable core vs Arduino.** `Canvas.h`, `Font.h`, `Sprite.h`,
  `TextLayout.*`, `Face.*`, `Preview.*`, `Animation.*` must stay free of
  Arduino headers so the harness compiles with plain g++ (`PROGMEM` shims are
  in `Font.h`). Firmware-only: `SnakeDisplay.*`, `DisplayCanvas.h`,
  `SetStore.*`, `StopLightApp.*`, `WebUI.*`, `WebPage.h`, `main.cpp`.
- Arduino defines macros `B1`, `B0`… and `bit()`: never use those as
  identifiers (pins are `r1_pin`, sprite pixels are `pixel()`).
- Generated, do not edit by hand: `src/fonts/StopFonts.h`
  (`tools/gen_font.py`, DejaVu Sans Bold, 12 sizes 11–56 px, ASCII + Hebrew)
  and `src/sprites/Sprites.h` (`tools/gen_sprites.py`, jointed-pose figures at
  64×96). Both generators need Pillow under WSL `python3`.
- Rendering runs on **core 1** (Arduino `loop()`), the web server + captive
  DNS in a task on **core 0**. `StopLightApp` guards shared state with a
  mutex. Web handlers must not render; they set state and the render loop
  picks it up (that is how live drafts avoid queueing behind each other).
- Double buffering: a full redraw is written to **both** DMA buffers before
  the cheap sprite-box repaints, else the stale buffer shows on alternate
  frames. `StopLightApp::render()` does this.
- Text limits: 240 chars per field on the page, 720 bytes on the device,
  up to 8 lines, exhaustive line-break search up to 14 words then greedy.
- Preview for the page is `/api/preview.rle` (~3 KB run-length frames,
  decoded onto a canvas); frames are cached client-side by
  (lit mask, sprite frame indices). The BMP endpoint exists for tools.
- The HUB75 library applies a CIE1931 gamma table by default, which maps
  level 1 of 3 (value 85) to 0: orange showed as red. `-DNO_CIE1931` in
  `platformio.ini` keeps the 2-bit levels linear. Do not remove it.
- Two panels on a six-panel build show the *last* two panels of the image
  (data shifts through the chain), hence `PANEL_COUNT`.
- A Claude Doc "P2 HUB75 Snake Display" holds the design history; update it
  only if the user asks.

## Verifying a change

1. `g++ -std=c++11 -O2 -Wall -Isrc tools/stopcli.cpp src/TextLayout.cpp src/Face.cpp src/Preview.cpp src/Animation.cpp -o tools/stopcli`
   then a render (`./tools/stopcli ppm t0=STOP t1=המתן t2=GO > x.ppm`).
2. Harness smoke test with curl (`/api/state`, `/api/check`,
   `/api/preview.rle`, POST `/api/sets`, POST/DELETE `/api/live`).
3. `pio run -e s3-2` (or `s3`), flash, read the boot log, ask the user what
   the panels show.

## TODO / ideas the user has raised

- **Upload images / animated GIFs from the page.** Agreed design: the browser
  decodes and scales (fit-inside or fill-and-crop), quantises to 2-bit RGB
  with optional dithering, keeps transparency, and sends a small raster file
  (1 byte/pixel, 0x40 = transparent, per-frame delays) as a multipart upload;
  the ESP32 stores it in LittleFS (~1.5 MB partition), lists/deletes it, and
  streams one frame at a time when rendering; uploaded graphics join the
  per-light "Text / graphic" dropdown next to the built-in sprites. The
  harness needs the same endpoints (`tools/gfx/`).
- The "On your marks" sprite comes out small because all frames of a sprite
  share one scale and the sprinting pose is wide.
- The six-panel build (`s3`) compiles but has not been run on all six panels
  since the S3 bring-up.
