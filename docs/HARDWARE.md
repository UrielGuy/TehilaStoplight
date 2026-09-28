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

S3 socket, rev 2.x, the maker's table as printed, verified with the six
installed panels on 2026-09-28:

| Signal | GPIO | Signal | GPIO |
| --- | --- | --- | --- |
| R1 | 18 | R2 | 16 |
| G1 | 8 | G2 | 1 |
| B1 | 17 | B2 | 15 |
| A | 7 | B | 48 |
| C | 6 | D | 47 |
| E | 2 | CLK | 5 |
| LAT | 21 | OE | 4 |

The two loose bench panels (a different batch) have their colour inputs
rotated: with them, use R1 8, G1 17, B1 18, R2 1, G2 15, B2 16.

The adapter's ESP32 (non-S3) socket uses GPIOs 22–33, which do not exist or
are flash pins on an S3; never reuse that map here.

## Mounting of the installed sign

Chain panel 0 (fed by the adapter) is the top panel, but every panel is
mounted rotated 180° relative to the firmware's default assumption: the top
panel's input is on the right, the next one's on the left, and so on. The
firmware therefore flips the parity (`FIRST_PANEL_FLIPPED_DEFAULT=1`, the
default; the `s3-2` bench env sets 0). Symptom before the fix, 2026-09-28: the
two halves of each circle swapped and the lights in reverse order.

## Serial / flashing

- S3 UART socket = CH343, was COM14. Native USB socket = "USB Serial Device",
  was COM15; flashing works there, the log does not (CDC on boot is off).
- Ports come and go when the user replugs; enumerate before every flash.
- esptool 5.x on Windows Python 3.13; use the dashed option names
  (`write-flash`, `--flash-mode keep`). Bootloader at 0x0 on the S3.

## Memory on the full sign

With six panels one DMA buffer is 96 KB. Double buffering left ~77 KB of
heap and the device rebooted under concurrent web traffic (live typing plus
preview frames); single buffering gives ~145 KB after start. Sprite frames
repaint only their own box, so the single buffer does not tear visibly.

## Driver settings

- Pixel clock on the S3 is 160 MHz / `S3_LCD_DIV_NUM` (build flag in
  `platformio.ini`); the library's `HZ_20M` really means divider 7 = 22.9 MHz,
  and its printed refresh rate is computed from the requested enum, not the
  divider. Divider 6 = 26.7 MHz (~316 Hz on six panels) is in use; 10 MHz gave
  108 Hz and felt flickery. Divider 5 = 32 MHz is the next step and is at the
  edge of the panel driver chips' rating. `min_refresh_rate 60`; double
  buffering only on the two-panel bench.
- 2 bits/channel: 48 KB per bit-plane per buffer for six panels.
- Some 128×64 panels need the FM6126A init sequence
  (`cfg.driver = HUB75_I2S_CFG::FM6126A`); these do not.
