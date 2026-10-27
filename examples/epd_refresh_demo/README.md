# T5-P4 E-Paper Refresh Demo

This example targets the **LILYGO T5-P4 E-Paper V0.3** with **ESP-IDF 6.1**
and ESP32-P4 silicon revision **v3.2**.

It ports the raw parallel EPD scan path from the reference T5S3 GameBoy project:

- 8-bit EPD data bus on GPIO27..GPIO34
- CKH/STH/LEH/CKV/STV on GPIO24/GPIO25/GPIO26/GPIO13/GPIO48
- PCA9535 and TPS651851 for panel power and VCOM
- Native ESP-IDF `esp_lcd` I80 DMA, with no M5GFX dependency
- Two 1-bit framebuffers in PSRAM and two DMA line buffers in internal RAM
- A dedicated scan task, frame-boundary buffer swaps, and dirty-row driving
- Horizontal scan mirroring for the T5-P4 panel wiring
- 40 MHz I80 pixel clock and a frame-end scheduler yield to avoid CPU1 WDT starvation
- 40 fps scan target and 2.25 KiB of internal-RAM lookup tables for pixel drive states

The screen first shows a quality test page containing 16 ordered-dither levels,
checkerboards, one-pixel lines, geometry, and text. It then continuously plays a
seamless 240-frame animation with a moving block, bouncing ball, scan line,
progress bar, frame counter, and measured producer FPS. The demo uses an inverted
black-background/white-foreground presentation. The quality page is only shown at
startup so the animation loop has no waveform-settle or page-change pause.

Both frontlight channels (GPIO53 and GPIO52 on V0.3) are held low from startup.
During animation, press BOOT (GPIO35, active low) to run a full-screen black/white
refresh and then resume animation. The button has 30 ms debounce and triggers
once per press; holding it does not repeatedly refresh.

## Build and flash

Set `EPD_VIDEO_SPEED_OPTIMIZED` in `main/epd_video.h`, then rebuild and flash:

| Macro value | Scan calculation | Target | Lookup-table RAM |
| --- | --- | --- | --- |
| `1` (default) | Optimized lookup tables | 40 fps | 2.25 KiB |
| `0` | Original per-pixel calculation | 24 fps | None |

For example, `#define EPD_VIDEO_SPEED_OPTIMIZED 0` selects the version before
the speed optimization. Both modes retain frontlight-off behavior, BOOT refresh,
and the TPS wakeup fix. The startup log prints the selected mode and target.

```bash
idf.py -C examples/epd_refresh_demo set-target esp32p4
idf.py -C examples/epd_refresh_demo build
idf.py -C examples/epd_refresh_demo -p <PORT> flash monitor
```

Expected log fields include:

```text
scan=... fps submit=... fps avg=... ms rows=... active=... vsync=...
producer=... fps submitted=... vsync=...
quality page complete: draw=... us vsync=...
```

`scan` is the physical panel scan rate. `submit` and `producer` show how often a
new animation frame reaches a scan boundary. `active` is the number of rows
which still need waveform pulses, so it also exposes how quickly motion settles.
The 40 fps target is a scheduler limit, not a measured panel frame rate. Actual
speed depends on scan and rendering time; compare `scan`, `submit`, and `producer`
on the device. The lookup tables preserve the existing three-pass drive logic
and mirrored pixel order. Panel contrast and ghosting at the faster rate still
need to be checked on hardware.

## TPS651851 wakeup

The driver waits at least 2 ms after asserting WAKEUP before accessing I2C
(the TPS65185x datasheet, Figure 2, specifies a minimum of 1.8 ms). It then
retries revision reads for up to 100 ms, plus any in-flight I2C transaction.
This avoids relying on a two-tick FreeRTOS delay, which can last only about
1 ms at a 1 kHz tick rate.

If `read TPS revision failed` persists, the log now includes the I2C error and
PCA input levels for WAKEUP, PWRUP, and power-good before shutdown. In standby,
WAKEUP should be 1 and PWRUP should be 0; power-good may still be 0. Check the
TPS supply (VSYS), WAKEUP wiring, and I2C address 0x68 if it remains inaccessible.
The EPD supply does not require enabling USB OTG boost on V0.3.
