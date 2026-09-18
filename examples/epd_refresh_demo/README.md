# T5-P4 E-Paper Refresh Demo

This example targets the **LILYGO T5-P4 E-Paper V0.3** with **ESP-IDF 6.0.1**
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

The screen first shows a quality test page containing 16 ordered-dither levels,
checkerboards, one-pixel lines, geometry, and text. It then continuously plays a
seamless 240-frame animation with a moving block, bouncing ball, scan line,
progress bar, frame counter, and measured producer FPS. The demo uses an inverted
black-background/white-foreground presentation. The quality page is only shown at
startup so the animation loop has no waveform-settle or page-change pause.

## Build and flash

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
