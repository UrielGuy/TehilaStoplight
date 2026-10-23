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
- Sound: a DIYables "MP3 Player Module" (3.5 mm jack, microSD) on UART1,
  GPIO 12 (ESP32 TX → module RX) / GPIO 13 (ESP32 RX ← module TX), module
  powered from a separate 5 V supply; every frame can start a track, stop
  sound, or fade it out over N seconds (`mp3`/`fade` in the frame JSON; a
  new track mid-fade restores the saved volume), global volume in NVS and on
  the page's top card. 10-byte checksummed
  frames (`Mp3Player.cpp`); the chip only reports the file count, never
  names; tracks are 1-based in copy order. Firmware runs without the module.
  Verified 2026-10-04: file count 3 reported, track playback heard. The
  `s3-mp3test` env is a console-driven bring-up build (commands l q r p s v
  x w 8 9 b, see main.cpp) worth keeping for the next hardware problem.
- Global setting "Unlit lights" on the page's top card: dim outline ring
  (default) or completely off (`/api/settings {offDark}`, NVS `offdark`,
  `Face::setOffDark`; the harness and `stopcli` take `o=1`), and
  "Orientation" normal / rotated 180° (`rotate180`, NVS `rot180`, default
  **true**: on 2026-10-19 the whole image showed upside down, so the canvas
  is rotated in `SnakeDisplay::mapToChain`; switch it on the page rather
  than touching `FIRST_PANEL_FLIPPED`), and "Panel colour wiring"
  standard / shifted (`colourRotated`, NVS `colrot`, default **true** since
  2026-10-19 when red showed blue and green showed red: the pin table in
  `PanelPins.h` stays the maker's, `SnakeDisplay::begin` shifts R/G/B pins
  for the other panel batch; changing it restarts the sign). The page
  reads all three from `settings` in `/api/state`.
- Beat animations (2026-10-23), drawn by the board: a set has a tempo
  (`bpm`, default 120); any colour can jump its hue N degrees per beat
  (`hc`/`hf` per light, palette popup "Hue jump"; `rgb2Hue` in Canvas.h is
  integer maths, 18 hues at full brightness); graphics `fx:quarters` (4
  colours turning clockwise per beat, `q` = quarters 2-4) and `fx:pulse`
  (75% disc, full size on the beat, easing back by 7/8 of the beat). The
  beat grid starts with the set and restarts at a frame that starts an MP3
  track (follows the song); hue jumps / quarters count beats since their
  frame started (`Face::Beat`). `Face::drawLight` now composes whole rows
  (text via `drawLayoutRow`, sprites clipped to the circle) so a per-beat
  redraw never flickers; `StopLightApp::loop` redraws only the lights whose
  `Face::beatState` changed (`renderBeat`). The page paints the beat over
  the server's start image client-side (`paintBeat`, a mirror of the C++:
  keep in step); `python3 tools/beat_jstest.py` (needs Windows node.exe)
  checks it pixel for pixel against `stopcli`.
- Uploaded animations (2026-10-19): `tools/anim_studio.py` (Windows Python,
  http://localhost:8090/) opens a GIF, a video or a YouTube link (yt-dlp,
  only the first 60 s, video only, at most 480p by the shorter side so
  portrait Shorts keep 480 px across; `--download-seconds` and
  `--download-res` on the CLI. Since 2026-10-20 `_fetch_start` fetches just
  the start of the file with range requests sized from its bitrate and cuts
  the minute out by stream copy: 3.7 s instead of 37 s for Big Buck Bunny.
  yt-dlp's own range cut goes through ffmpeg, which reads one long stream
  that YouTube paces at ~2x playback speed whatever the resolution, and
  `force_keyframes_at_cuts` re-encoded it on top; that path is now only the
  fallback for formats without range support. yt-dlp
  needs a binary literally named ffmpeg.exe on PATH, so `ffmpeg_dir()`
  copies imageio's into the git-ignored `tools/gfx/bin/`),
  crop box + in/out times, converts with `tools/anim_convert.py` (also a
  CLI: `--crop x,y,w,h` fractions, `--start/--duration`, `--shape
  circle|full`, `--upload`), previews, and uploads to the sign. Format in
  `src/AnimStore.h` (RLE, 1 byte/pixel = Rgb2 + silhouette bit). The sign
  keeps them in LittleFS (`/a/<id>.anm`, 4.7 MB) and plays them per light
  (`g` = `u:<id>`, 128×128 clipped to the circle) or over the whole sign
  (a 128×384 file on any light replaces the face). Per use: `gm` 0 original
  colours / 1 silhouette in the light's ink on its circle colour / 2
  inverted, `af`/`al` play only that ms window. The page has an
  "Your animations" card (upload .anm, delete) and paints them into the
  preview client-side. Needs pip packages on Windows Python: yt-dlp,
  imageio-ffmpeg, numpy, Pillow, websocket-client (for ui_flow).
- Uploads also work over the USB cable, on either socket (`src/SerialLink.*`,
  "#ANIM" line protocol documented in its header: packets of ≤2 KB with
  offset + CRC32, the board answers ACK/NAK/POS with its position and the PC
  resends from there). The native USB link loses a byte or a reply now and
  then, so do not simplify this to a plain stream. The studio lists sign ports
  by USB id (303A native, 1A86 CH343) next to WiFi; the CLI takes
  `--upload usb` or `--upload COM19`. Ports are opened with DTR/RTS low so
  the board does not reset. Measured 2026-10-19: 418 KB in 12–18 s over the
  native socket. `USBSerial` must keep `setTxTimeoutMs(50)` (0 silently
  drops replies when the driver lock is busy) and `setDebugOutput(false)`
  (otherwise the ESP log also goes to that socket).
- Animation files are **format 3** since 2026-10-19 evening: a context-model
  binary arithmetic coder over the 7-bit pixels (`src/AnimV3.*`, encoder
  `tools/anim_v3.py`, browser decoder `tools/anim_v3.js` pasted into
  `WebPage.h` and `anim_studio.py`; all three must stay in step, the header's
  `priorBytes` = 7449 guards the model layout). 32% smaller than format 2 on
  the user's eight clips (deflate variants with bigger windows, frame groups,
  palettes, PNG filters, deltas all stayed within 10%: the clips are 4x4
  Bayer-dithered, see `tools/anim_codec_lab.py` for the table). Whole-sign
  clips have independent frames (K=1); one-light clips use temporal frames
  with keyframes every 8. Formats 1 and 2 still play (synchronously, on the
  render core). The converter keeps format 2 when it is smaller (tiny clips:
  the format 3 model costs 7.4 KB per file).
  **Masked clips** (2026-10-20, header flag bit 1, the default for every
  128-wide clip of 128-row tiles): only the pixels inside each light's
  circle plus a 2 px margin (diameter 132, `maskSpan` / `mask_array`) are
  coded; the rest decodes as black because the sign's cover hides it (also
  the strips between the circles of a whole-sign clip). Hidden pixels carry
  the context flags tf 0 / sf 2. Blacking the corners without skipping them
  saved only 4% and grew some clips (the circle edge costs bits); skipping
  saved 11% on the user's clips (whole-sign clips 9-17%, static one-light
  video clips ~1%). All eight clips on the board were replaced with masked
  versions that day (backup in `tools/gfx/backup/20261020-144643`, test
  vectors in `.pio/v3m`). `anim_recompress.py` treats unmasked format 3 as
  old and re-encodes it.
  **Decoding format 3 is slow on the ESP32**: ~95 ms of CPU per whole-sign
  frame (~80 cycles per coding decision is the going rate for this CPU; -O2,
  IRAM and register-resident coder state only bought 20%), more than the
  83 ms frame period. So format 3 frames are decoded by two background
  tasks (one per core, `AnimStore::beginDecoders`, 17.5 KB `V3Scratch` each
  in internal RAM) into a ring of three PSRAM frame buffers per clip slot
  (`V3Slot`, three slots, state in PSRAM), up to two frames ahead of the
  play position; the render core only copies a ready frame (`paintV3`) and
  `drawFrame` returns false while a frame is still decoding (the app then
  keeps `_lastKey` invalid and retries next loop). Measured: 12.0 fps with
  no skipped or late frames for whole-sign alone, three one-light clips at
  once, and mixed. Diagnostics over the cable: `#ANIM PERF` (decode times,
  playback counters, render/decoder stage markers, heap, boot heap
  samples), `#ANIM BENCH <id> <frame>`, `#ANIM PLAY a,b,c` / `STOP`.
  PSRAM: `board_build.arduino.memory_type = qio_opi` + `-DBOARD_HAS_PSRAM`
  (8 MB embedded, works). `setup()` calls `heap_caps_malloc_extmem_enable(512)`:
  the SDK reserves no internal RAM once PSRAM is on and sent only allocations
  of 4 KB+ to PSRAM, so web replies fragmented internal RAM until phones
  could not join (2026-10-23: 32 KB free, largest block 4 KB). Now idle
  internal heap ~82 KB, ~80 KB while clips play. `#ANIM PERF` reports
  `web` {stations, loops, maxPauseMs, rssi[]} for WiFi trouble; decoding
  barely delays the web task (max pause 4 ms idle, 7 ms during a
  whole-sign clip).
  `tools/anim_recompress.py [usb|COMx|http://...] [--dry-run]` backs up the
  board's files to `tools/gfx/backup/<time>/`, re-encodes older formats,
  checks the pixels match, and uploads them back under the same names
  (serial read-back via `#ANIM GET`, ~7.5 KB/s; needs the 4 KB `USBSerial`
  TX buffer). Verified copies of the user's clips from 2026-10-19 are in
  `.pio/samples` (format 2) and `.pio/v3`.
- Storage view: the sign's page lists each animation's size and share of
  the 4.7 MB with a usage bar (`animTotal` next to `animFree` in
  `/api/state`); the studio has an "On the sign" card (stacked bar per
  animation, the converted clip striped, whether it fits) fed by
  `ac.sign_list`, which returns `free`/`total` over WiFi (`/api/anims`) and
  over the cable (`#ANIM SPACE`). The serial reply readers skip stale replies
  to earlier commands: on the native USB link a retried GET left extra BLK
  lines queued that the next PUT took as its answer (2026-10-20).
- Backups: `tools/anim_backup.py [usb|COMx|http://...] [--to DIR]` copies
  every animation to `tools/gfx/backup/<time>/` with an index.json of
  CRC32s; `--restore DIR [sign] [--replace]` uploads such a folder to a sign
  and skips names it already has. Written 2026-10-20, not yet run end to
  end. Its read-back `ac.fetch_serial` keeps three GETs in flight and sends
  a POS when replies pause: one GET at a time, the replies lagged one behind
  (the native socket held the board's last reply back until it sent more),
  which is most of what looked like lost replies. 30 KB/s instead of 7.5
  (car-race, byte-identical); `anim_recompress.py` still reads one at a time.
- Uploads that replace a clip which would not fit next to its old copy
  delete the old copy at the start (PUT / HTTP `size=`): writing into a
  full LittleFS stalled the board for ~30 s. The first PUT after boot on a
  nearly full file system can also take seconds (the PC waits 25 s).
- The studio shows progress bars (download, preview preparation, frame
  decoding, colour conversion, upload) by polling `/api/progress?t=<token>`;
  the CLI prints the same as a progress line. The studio can rotate the
  source (90/180/270, ffmpeg transpose before the crop; the preview video is
  remade turned so the crop box matches). `--port N` runs a second studio
  (the user often has one open on 8090: never kill it).
- Naming: the studio's Convert writes `tools/gfx/work/<source id>.anm`;
  the name is chosen in the upload card. Before uploading it lists the
  sign's animations (`ac.sign_list`, WiFi or cable) and asks before
  replacing one with the same name; after an upload a copy is kept as
  `tools/gfx/<name>.anm`. The sign's page also asks before replacing, and
  has Rename per animation (`POST /api/anims/<id>/rename {name}`: refuses a
  taken name, renames the file and rewrites `u:<old>` in every stored set
  under the playback lock). `/api/anims/<id>.anm` is served `no-cache`.
- **Smooth playback (2026-10-19 investigation).** Symptom: uploaded
  animations froze for a moment now and then (spiral-yellow-pink). Not the
  decompression: measured with the `#ANIM PERF` counters (per-frame open /
  read / inflate / pixel-paint times, histogram, worst frame, plus skipped and
  late frames and the max gap; `#ANIM PERF RESET`; `#ANIM PLAY <id>` shows an
  animation as a live draft for tests, `#ANIM STOP`; `.pio/perf.py` and
  `perf2.py` in the session drove them). Findings and fixes:
  1. Painting cost 75 ms of an 89 ms whole-sign frame (12 fps budget 83 ms):
     every pixel went through the library's `drawPixelRGB888` (gamma maths,
     bounds checks, two row lookups). `SnakeDisplay::writeRow` now writes a
     whole canvas row straight into the DMA bit-planes (chain position once
     per row, 64-entry plane table; the library's private `fb` is reached via
     an explicit-instantiation trick, no library edit). `Canvas::setRow` is the
     portable hook; `Face::draw` and `AnimStore::drawFrame` emit rows. Frame
     time 89 -> 24 ms. Also fixes a colour bug: the library's NO_CIE1931
     rounding turned level 2 (170) into level 3.
  2. Opening the file for every frame cost 8 ms: `AnimStore` keeps up to
     three files open (`closeAll()` under the playback lock before any
     replace / rename / delete).
  3. Page polling (`/api/state` every 2 s) lists the animations by reading
     every file header; **any LittleFS flash access stalls the other core**
     (flash cache disabled during SPI flash ops), stretching frames to 80 ms
     and skipping frames. The list is now cached in RAM (`s_listValid`,
     dropped by `closeAll()`). Uploads still stall playback while they write;
     that is accepted.
  Result on all six clips: 12.0 fps shown, no skipped/late frames, max frame
  26-29 ms, even with the page polling every second. The 84 ms "max gap" is
  the frame delay itself.
- **Partition table** is `partitions.csv` (nvs/app0 at the default offsets,
  the second OTA slot became LittleFS). When flashing a board that still has
  the default table, also write `0x8000 .pio\build\s3\partitions.bin`.

## Offline editor (tools/sign_editor.py, 2026-10-23)

`python.exe tools/sign_editor.py` (default port 8095, `--port`, `--workspace`,
`--no-browser`; never 8080/8090) is a local web app for when
the WiFi is hard to reach: **Download** pulls everything from the sign once
(WiFi or a USB port, picked in the editor card), the sign's own page
(`src/WebPage.h`, read at every request) then edits a workspace folder
offline, and **Upload to the sign…** shows a plan (add / replace / delete
animations with sizes and whether they fit, the sets, the settings) and
pushes it. The card is injected at serve time from
`tools/sign_editor_inject.html` (it wraps the page's `timeline`,
`mp3Options`, `render` and `fetch`; no WebPage.h change).
- Workspace (git-ignored `tools/gfx/editor/`): `workspace.json` (sets,
  settings, volume, music folder, target), `anims/`, `board.json` (what the
  sign had at the last download / upload: sizes, CRC32s, sets, settings),
  `incoming/` (a download in progress, resumed per file), `trash/<time>/`
  (what a download replaced or a delete removed).
- Plan order: deletes of animations the sign's current sets do not use,
  then uploads (smallest first, shrinking replacements first; a replacement
  that does not fit deletes the old copy first, as SerialLink PUT and the HTTP
  `size=` do), then the sets, then the remaining deletes (pulled forward only
  when needed for room), then settings (`colourRotated` last: the sign
  restarts). Each finished step is written to `board.json`, so a broken
  upload just runs again. Over WiFi the sets go as DELETE/PUT/POST per set
  (only those that differ, compared with `sign_link.canon_set`, a Python
  mirror of setFromJson + setToJson: keep it in step with SetStore.cpp).
- Cable protocol additions in `SerialLink` (needs the firmware of
  2026-10-23): `SUM <id>` (size + CRC32 of a stored file), `SETS` + `GET .sets
  <off> <len>` (snapshot of the stored sets), `SPUT <size>` + DATA + END
  (replace every set, checked like the page's Add; ≤48 KB in, 32 KB stored;
  PSRAM buffers), `CONF` / `CONF {json}` (offDark, rotate180, volume,
  colourRotated). Without SUM (older firmware) the plan compares sizes.
- Music: the card takes the folder that mirrors the SD card; files (.mp3,
  .wav) play as tracks 1, 2, 3… in name order, so copy them to the card in
  that order. `/api/state` reports their count as `mp3.tracks`; the preview's
  "play the whole set" plays / stops / fades them in the browser at each
  frame start (`/music/<n>`), volume = setting/30.
- Text engine (2026-10-23): `tools/stoplight_render.py`, a pure-Python
  (numpy) port of what tools/stopcli does: TextLayout.cpp (utf8, bidi with
  bracket mirroring, exhaustive line breaks up to 14 words then greedy,
  float32 fills, ink centring, maxFit, the error strings), Face::drawLight
  (doubled-unit circles, dim ring / offDark, sprites, ink == bg rule,
  quarters / pulse / hue jumps) and BitmapCanvas (RLE with first-use palette
  and the 16-colour cap, BMP, PPM). Fonts and figures are parsed from
  src/fonts/StopFonts.h and src/sprites/Sprites.h at start-up (~80 ms), so
  regenerated headers need no change there. No WSL, no C++: a preview frame
  renders in ~0.4 ms on Windows Python, a new layout in 0.1-2 ms (cached).
  Same CLI as stopcli (`sizes`, `sprites`, `check`, `rle`, `preview`, `ppm`).
  **Keep it in step with the C++**: `python3 tools/render_parity_test.py`
  (WSL; builds tools/stopcli) compares both byte for byte on ~4200 checks and
  ~1400 renders (`--quick` for a short run); it must print "all identical".
- The animation studio lists "into the offline editor" as an upload target
  (http://localhost:8095), and the card imports clips left in tools/gfx.
- Tests: `python3 tools/seriallink_test.py` (the real SerialLink.cpp +
  SetStore.cpp built for the PC with the stand-ins in tools/seriallink_sim/,
  clean and lossy links), `python3 tools/sign_editor_test.py` (plan unit
  tests, WiFi round trip with the harness as the sign, cable round trip with
  the simulated board), `python.exe tools/sign_editor_ui.py <editor url>
  <music dir> <target>` (headless Edge: no JS errors, sound timing, plan).
  `tools/local_server.py` takes `STOPLIGHT_SETS` / `STOPLIGHT_GFX` to act as
  a second, throwaway sign.

Pip packages for the editor (Windows Python): numpy, pyserial (cable). WSL,
g++ and Pillow are not needed.

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
The S3 has two USB sockets: **UART** (CH343, serial log works; COM14/COM16 so
far) and **USB** (native; flashing works, log does not since CDC-on-boot is
off). If only the native socket is available, `pio run -e s3-usbconsole` is
the same firmware with the console on that socket, but reading its boot
lines over the native port proved unreliable; asking the user to replug the
UART socket is quicker.

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
  (`tools/gen_font.py`, DejaVu Sans Bold, 12 sizes 11–56 px, ASCII + Hebrew;
  Hebrew letters from `tools/fonts/heshbon-bold-alefalefalef.otf`, scaled to
  DejaVu's cap height on a shared baseline, quarter-pixel grid fit; since
  2026-10-23 the Latin letters A–Z/a–z come from `tools/fonts/High-School.ttf`
  (GeronimoFonts, FontStruct Non-Commercial License; unicase, so lower case
  shows capitals), its capitals scaled to exactly DejaVu's cap height, grid fit
  that also penalises strokes thinner than cap/5, even 64/1024 em letter gaps.
  It has no digits, punctuation or space: those stay DejaVu. Its K/X look
  like H, R like A and V like U on the panel, most of all at small sizes)
  and `src/sprites/Sprites.h` (`tools/gen_sprites.py`, jointed-pose figures at
  64×96 plus the procedural 128×128 "eclipse" discs; a pose may carry a
  `second` figure and extra `shapes` for props such as the bike). Both
  generators need Pillow under WSL `python3`; `SHEET_DIR=<dir>` writes the
  contact sheet `sprites.png` there (view it with the Read tool).
- Rendering runs on **core 1** (Arduino `loop()`), the web server in a task
  on **core 0** (DNS answers from the AsyncUDP task, `AccessPoint.*`; phones'
  connectivity checks get "online", not a captive-portal redirect, since
  2026-10-23). `StopLightApp::loop()` holds a recursive mutex
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
  preview canvas is allocated once in `WebUI::begin` and `reset()` per image:
  its 16-entry palette otherwise fills up with random colours and everything
  new draws black), keep both task stacks at 16 KB, and check
  `heap`/`heapMin`/`heapBlock` in `/api/state` after a stress run (a 90 s
  concurrent typing+preview run on 2026-09-28 bottomed at 73 KB).
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
   `_validate`), `tools/stopcli.cpp` mirrors preview/check, and
   `tools/stoplight_render.py` (the offline editor's engine) ports the same
   C++ to Python: run `tools/render_parity_test.py`. Keep them in step.
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
- `python.exe tools/ui_flow.py [url]` drives the real page in headless Edge
  over the DevTools protocol (select a frame, duplicate, edit, change a loop,
  name, save) and prints any JS exception; edit its step list for other flows.
- **Never leave test data on the device.** Fetch a set before touching it,
  restore it afterwards, and never overwrite the user's set with test values
  (frame durations were lost that way on 2026-09-28).

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

- Converting GIFs directly on the phone (in-browser decoding) was not done;
  conversion happens on the PC in the animation studio.
- Uploaded animations were tested in the harness and headless browsers but
  not yet watched on the LEDs (2026-10-19, no WiFi dongle that day).
- The "On your marks" sprite comes out small because all frames of a sprite
  share one scale and the sprinting pose is wide.
