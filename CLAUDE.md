# TehilaStoplight — guide for agents

A traffic-light sign built from six 128×64 P2 HUB75E LED panels (stacked
vertically, snake-chained, 128×384 pixels), driven by an ESP32-S3 in a HUB75
adapter board. It shows three coloured circles with text (English/Hebrew) or
animated figures, managed from a phone over the ESP32's own WiFi access point.

Read this file first. `docs/ARCHITECTURE.md` has the module map, data model
and API; `docs/HARDWARE.md` has wiring and bench findings.

Git: `main` tracks https://github.com/UrielGuy/TehilaStoplight (private).
Push with the token in `~/.github_token` on the command line
(`git push "https://x-access-token:$(tr -d '\r\n' < ~/.github_token)@github.com/UrielGuy/TehilaStoplight.git" main`);
the `origin` URL deliberately stores no credentials. Commit as
`Uriel Guy <2401677+UrielGuy@users.noreply.github.com>` (GitHub rejects the
real address). Commit only when the user asks.

## Current state (2026-09-28)

- Controller: **ESP32-S3-DevKitC-1** in a Seengreat/WatangTech "RGB Matrix
  Adapter Board (E)" **rev 2.2**. Pin map in `src/PanelPins.h` (the only one).
- Build env **`s3`** (six panels; flashed on 2026-09-28 to a fresh S3 with all
  six panels attached) or **`s3-2`** (bench with only the top two panels).
- A blank S3 on its native USB socket shows up in Windows as "USB DFU" with no
  COM port; use the UART socket (CH343, COM16 last time).
- On the installed sign every panel sits rotated 180° relative to the default
  assumption: `FIRST_PANEL_FLIPPED_DEFAULT=1` (default; `s3-2` bench sets 0).
  Colour pin order is the maker's table; the two loose bench panels need it
  rotated (see HARDWARE.md).
- Colour depth 2 bits/channel (64 colours). Single-buffered on the six-panel
  sign (the second 96 KB buffer starved the heap: concurrent page traffic
  crashed the device on 2026-09-28); double-buffered on the two-panel bench.
- A set is a named keyframe sequence (each frame: three lights, duration)
  with loop sections (frames 6–10 ×10, 12–14 ×4 …), up to 64 frames; per
  light short/long text, a figure, or Off (dark); per-light colours including
  random (never black, never one of the last two random picks); size caps;
  edit of stored sets; live-on-LEDs editing; pixel-exact animated preview.
  The earlier "lights + animation pattern" mode and the whole-set loop were
  removed on 2026-09-28 at the user's request. Verified on the sign: a
  14-frame set with two loop sections played the exact expanded playlist and
  handed over to the next set.
- The page keeps the editor draft in localStorage (`stoplight.editor`); a
  headless check is `msedge.exe --headless=new --dump-dom http://localhost:8080/`
  against the harness (see docs/ARCHITECTURE.md for the data model).
- Not done: uploading images/GIFs from the page (design agreed, see TODO).

## Build, flash, monitor (from WSL)

PlatformIO lives in `~/.local/bin/pio` (pipx):

```
export PATH="$HOME/.local/bin:$PATH"
pio run -e s3          # the sign (six panels); -e s3-2 = loose two-panel bench
```

**WSL cannot see USB serial ports.** Flash and monitor through Windows Python,
which has esptool 5 and pyserial installed:

```
python.exe -m esptool --chip esp32s3 --port COM16 --baud 460800 \
  --before default-reset --after hard-reset write-flash -z \
  --flash-mode keep --flash-freq keep --flash-size keep \
  0x10000 'C:\repos\TehilaStoplight\.pio\build\s3\firmware.bin'
```

Flashing or opening the serial port resets the board (the stored sets in
NVS survive; the live draft and current position do not).

A brand-new chip also needs `0x0 bootloader.bin 0x8000 partitions.bin 0xe000
boot_app0.bin` (boot_app0.bin from
`~/.platformio/packages/framework-arduinoespressif32/tools/partitions/`).

Find the port first; it changes when the user replugs:
`powershell.exe -NoProfile -Command "Get-PnpDevice -PresentOnly -Class Ports | Select FriendlyName"`.
The S3 has two USB sockets: **UART** (CH343, serial log works, COM16 last time)
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
"AP 'StopLight' up" (expect ~145 KB free after start on six panels).

**Reaching the device from the PC.** A USB WiFi dongle ("Wi-Fi 2", an
802.11n USB card) can join the AP while the main adapter keeps the internet:
`netsh wlan add profile filename='C:\repos\TehilaStoplight\.pio\StopLight.xml' interface='Wi-Fi 2'`
then `netsh wlan connect name=StopLight interface='Wi-Fi 2'` (profile XML is
in `.pio/`, regenerate it from the SSID/password if missing). Requests must
come from Windows (`curl.exe`, `python.exe`), not WSL; WSL's route and
sometimes the first Windows requests get intercepted by something that
answers with an nginx 301 / self-signed TLS. `tools/type_test.py <base> <text>
--live` imitates typing one character at a time. For crash hunting, log the
UART with a long-running pyserial script started *before* the test and stop
it before flashing (only one process can hold the COM port; a stale
`python3.13` process holding it must be killed with `Stop-Process`).

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

`tools/stopcli` subcommands: `sizes`, `sprites`,
`check <light> <text> [maxSize]`, and `preview|rle|ppm key=value...` (keys are
the web API query keys, e.g. `t0=STOP g2=dancing k=440 c0=3 f0=63 m=7`).
To eyeball a layout: `stopcli ppm ... > x.ppm`, convert to PNG with Pillow,
view with the Read tool.

## Conventions and gotchas

- **Portable core vs Arduino.** `Canvas.h`, `Font.h`, `Sprite.h`,
  `TextLayout.*`, `Face.*`, `Preview.*` must stay free of
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
  DNS in a task on **core 0**. `StopLightApp::loop()` holds a recursive mutex
  for its whole pass because web handlers (add/edit/show/delete) rebuild the
  prepared frames through `renderCurrent()`; a set added while another played
  crashed the device before that (2026-09-28). Live drafts are only stored by
  the web task and applied by the render loop.
- Double buffering: a full redraw is written to **both** DMA buffers before
  the cheap sprite-box repaints, else the stale buffer shows on alternate
  frames. `StopLightApp::render()` does this.
- Text limits: 240 chars per field on the page, 720 bytes on the device,
  up to 8 lines, exhaustive line-break search up to 14 words then greedy.
- Preview for the page is `/api/preview.rle` (~3 KB run-length frames,
  decoded onto a canvas); frames are cached client-side by
  (lit mask, sprite frame indices). The BMP endpoint exists for tools.
- Memory is the constraint on the full sign. Keep large buffers static (the
  preview canvas is allocated once in `WebUI::begin`), keep both task stacks at
  16 KB, and check `heap`/`heapMin`/`heapBlock` in `/api/state` after a stress
  run (a 90 s concurrent typing+preview run on 2026-09-28 bottomed at 73 KB).
- The HUB75 library applies a CIE1931 gamma table by default, which maps
  level 1 of 3 (value 85) to 0: orange showed as red. `-DNO_CIE1931` in
  `platformio.ini` keeps the 2-bit levels linear. Do not remove it.
- Two panels on a six-panel build show the *last* two panels of the image
  (data shifts through the chain), hence `PANEL_COUNT`.
- A Claude Doc "P2 HUB75 Snake Display" holds the design history; update it
  only if the user asks.

## Verifying a change

1. `g++ -std=c++11 -O2 -Wall -Isrc tools/stopcli.cpp src/TextLayout.cpp src/Face.cpp src/Preview.cpp -o tools/stopcli`
   then a render (`./tools/stopcli ppm t0=STOP t1=המתן t2=GO > x.ppm`).
2. Harness smoke test with curl (`/api/state`, `/api/check`,
   `/api/preview.rle`, POST `/api/sets`, POST/DELETE `/api/live`).
3. `pio run -e s3`, flash, read the boot log (or hit the API through the
   dongle), ask the user what the panels show.
4. For a page change: reload once on the phone (the page is in PROGMEM, no
   caching headers, but an open tab keeps the old script until reloaded).

## Adding a feature end to end (the usual path)

1. Model: `LightDef` / `KeyFrame` / `TextSet` in `SetStore.h`, JSON in
   `lightsToJson` / `lightsFromJson` / `setToJson` / `setFromJson`
   (`SetStore.cpp`). Keep old JSON loadable (defaults with `| x`).
2. Playback: `StopLightApp::prepareFrame` (what a light looks like) and
   `loop()` (timing). Rendering primitives live in `Face.*` (portable).
3. API: `WebUI.cpp` — `sendState`, `parseSet`/`checkLights`, `handlePreview`
   query keys (`t s c f g k m`), `handleLive`.
4. Page: `WebPage.h` (one file, HTML+CSS+JS). Editor state is the `ed` object
   (`name, frames[{lights[3]{k,t,s,c,f}, ms}], loops[{from,to,count}], cur,
   editing`), persisted to localStorage `stoplight.editor`; `buildLights()`
   redraws the three-light editor for the selected frame, `buildLoops()` the
   loop rows, `checkLight(i)` asks `/api/check`, `preview()` builds a timeline
   (`timeline()`), fetches `/api/preview.rle` per distinct frame and paints a
   canvas, `liveSend()` pushes `draftJson()` to `/api/live`, `submit(asNew)`
   posts `setJson()`, `render(state)` draws the set list. Sprites/sizes come
   from `/api/state` on first load.
5. Harness: `tools/local_server.py` mirrors the API (validation in
   `_validate`), `tools/stopcli.cpp` mirrors preview/check. Keep them in step.
6. Headless check of the page, then build + flash, then docs.

## Testing against the real device from this PC

- Join the USB WiFi dongle (interface name "Wi-Fi 2") to the AP:
  `netsh wlan connect name=StopLight interface='Wi-Fi 2'` (profile already
  stored; XML also in `.pio/StopLight.xml`). The PC gets 192.168.4.2.
  The dongle drops off Windows whenever the board resets if it shares the
  DevKit's USB hub; put it on another port.
- Call the API from Windows (`curl.exe`, `python.exe - <<EOF`), never from
  WSL. Hebrew text: run scripts through `python.exe -` with
  `sys.stdout.reconfigure(encoding='utf-8')`.
- `python.exe tools/type_test.py http://192.168.4.1 "text" --live` types a
  string one character at a time like the page does. For stress, run
  several request streams in threads (see git history of this session:
  live pushes every 150 ms + checks + previews + state polls for 90 s).
- `/api/state` carries `heap`, `heapMin`, `heapBlock` and `frame`; use them
  to confirm memory and playback without watching the panels.

## Known quirks

- Serial shows `send(): content length is zero` for every chunked preview
  reply and `request handler not found` for phone captive-portal probes:
  both harmless.
- The library prints a refresh rate computed from the requested enum, not
  the real divider (`S3_LCD_DIV_NUM`).
- "On your marks" sprite frames are small because all frames of a sprite
  share one scale and the sprint pose is wide.
- Live mode holds the LEDs until the switch is turned off, a set is added
  or saved, or 60 s pass without edits.

## TODO / ideas the user has raised

- **Upload images / animated GIFs from the page.** Design agreed with the user
  but not started (removed from the immediate plan when the keyframe editor
  took priority): the browser
  decodes and scales (fit-inside or fill-and-crop), quantises to 2-bit RGB
  with optional dithering, keeps transparency, and sends a small raster file
  (1 byte/pixel, 0x40 = transparent, per-frame delays) as a multipart upload;
  the ESP32 stores it in LittleFS (~1.5 MB partition), lists/deletes it, and
  streams one frame at a time when rendering; uploaded graphics join the
  per-light "Text / graphic" dropdown next to the built-in sprites. The
  harness needs the same endpoints (`tools/gfx/`).
- The "On your marks" sprite comes out small because all frames of a sprite
  share one scale and the sprinting pose is wide.
