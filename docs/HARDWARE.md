# Hardware notes

## Panels

Six 128×64 P2 HUB75E panels (1/32 scan, pin 8 = E). Mounted vertically:
panel 0 (the one whose IN socket the adapter feeds) at the top, upright;
panel 1 below it rotated 180° so its IN is on the right; and so on. The
firmware maps canvas (x, y) to chain (dx, dy) in `SnakeDisplay::mapToChain`
(odd panels: x and y mirrored). `FIRST_PANEL_FLIPPED` swaps the parity if the
first panel is mounted the other way.

Power: 5 V to every panel's screw terminals, common ground with the
controller. Budget ~4 A per panel at full white. The adapter board's own 5 V
input powers its HUB75 buffers: **with only the DevKit's USB attached the data
lines sit near 2.5 V and the panels stay dark** (seen 2026-09-19).

## Controller and adapter

ESP32-S3-DevKitC-1 in a Seengreat / WatangTech "RGB Matrix Adapter Board (E)"
rev 2.2 (maker wiki: seengreat.com/wiki/186). The adapter has sockets for both
the ESP32-DevKitC V4 and the S3-DevKitC-1; each socket has its own pin map and
board revisions differ. Only the S3 socket is used now.

S3 socket, rev 2.x, **as verified on the bench** (the wiki's colour rows are
rotated relative to what the panel shows: red came out blue; the colour pins
in `PanelPins.h` are rotated accordingly):

| Signal | GPIO | Signal | GPIO |
| --- | --- | --- | --- |
| R1 | 8 | R2 | 1 |
| G1 | 17 | G2 | 15 |
| B1 | 18 | B2 | 16 |
| A | 7 | B | 48 |
| C | 6 | D | 47 |
| E | 2 | CLK | 5 |
| LAT | 21 | OE | 4 |

The adapter's ESP32 (non-S3) socket uses GPIOs 22–33, which do not exist or
are flash pins on an S3; never reuse that map here.

## Serial / flashing

- S3 UART socket = CH343, was COM14. Native USB socket = "USB Serial Device",
  was COM15; flashing works there, the log does not (CDC on boot is off).
- Ports come and go when the user replugs; enumerate before every flash.
- esptool 5.x on Windows Python 3.13; use the dashed option names
  (`write-flash`, `--flash-mode keep`). Bootloader at 0x0 on the S3.

## Driver settings

- `HUB75_I2S_CFG` via `SnakeDisplay::Options`: clock 10 MHz (20 MHz possible
  with clean wiring), `min_refresh_rate 60`, double-buffered.
- 2 bits/channel: 48 KB per bit-plane per buffer for six panels; 108 Hz
  refresh on six panels at 10 MHz, 325 Hz on two.
- Some 128×64 panels need the FM6126A init sequence
  (`cfg.driver = HUB75_I2S_CFG::FM6126A`); these do not.
