# hdmi_video_renderer_lvgl

## What This Example Does

This example drives an HDMI monitor with an `LVGL` demo instead of video playback. It allocates a full-screen LVGL draw buffer in PSRAM, converts the output to RGB888, and flushes frames through the LT8912B HDMI path.

The startup path submits a diagnostic frame immediately after display initialization, then starts LVGL without resetting the LT8912B MIPI receiver or adding a fixed delay. Keeping the video timing continuous avoids forcing slower HDMI monitors to detect the input a second time before the demo appears.

## Prerequisites

- A LilyGo T5-P4 board configured for HDMI output.
- An HDMI monitor connected to the board.
- PSRAM enabled as required by the example defaults.
- Optional: choose the startup demo in `idf.py menuconfig` under `HDMI LVGL Demo Configuration`.

## Build and Flash

```bash
idf.py -C examples/hdmi_video_renderer_lvgl set-target esp32p4
idf.py -C examples/hdmi_video_renderer_lvgl menuconfig
idf.py -C examples/hdmi_video_renderer_lvgl build
idf.py -C examples/hdmi_video_renderer_lvgl -p <PORT> flash monitor
```

## Expected Log Output

You should see lines similar to:

```text
I (...) hdmi_lvgl: Starting HDMI LVGL Demo Runner
I (...) hdmi_lvgl: Free SPIRAM before init: <bytes>
I (...) hdmi_lvgl: Display timing: 800x600, DSI lane bitrate: <...> Mbps
I (...) hdmi_lvgl: Allocated LVGL draw buffer=<...> bytes, HDMI flush buffer=<...> bytes
I (...) hdmi_lvgl: Starting LVGL Benchmark demo
```

During runtime, the log also prints periodic `Flushed LVGL frame` messages.

## Troubleshooting

- If initialization fails immediately, confirm the project is still configured for `CONFIG_BSP_LCD_TYPE_HDMI=y` and RGB888 output.
- If the screen stays black, re-check the HDMI monitor, cable, and board power.
- If PSRAM allocation fails, keep the default memory settings and avoid shrinking available external RAM.
