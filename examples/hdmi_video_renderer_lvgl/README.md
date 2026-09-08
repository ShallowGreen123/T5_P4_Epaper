# hdmi_video_renderer_lvgl

## What This Example Does

This example drives an HDMI monitor with an `LVGL` demo instead of video playback. The default configuration is 1920x1080 at 30 Hz. LVGL renders into two partial RGB565 draw buffers while the ESP32-P4 PPA converts the previous buffer directly into the RGB888 DPI frame buffer.

The HDMI/DPI output remains RGB888. RGB565 is only used for LVGL's internal rendering so the CPU and PSRAM move half as much draw-buffer data. The PPA path removes the full-screen CPU color conversion, the separate RGB888 flush buffer, and the second framebuffer that was unused by partial updates.

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
I (...) hdmi_lvgl: Display timing: 1920x1080, DSI lane bitrate: <...> Mbps
I (...) hdmi_lvgl: Allocated two 128-line LVGL draw buffers: <...> bytes total, color depth=16
I (...) hdmi_lvgl: Enabled asynchronous PPA RGB565-to-RGB888 partial flush path
I (...) hdmi_lvgl: Starting LVGL Benchmark demo
```

During runtime, the log prints the LVGL refresh submission rate every two seconds. This reflects the render/flush pipeline throughput, not the fixed 30 Hz HDMI scan timing.

## Performance Tuning

- `HDMI LVGL Demo Configuration -> LVGL partial draw buffer height` controls the two partial draw buffers. The 128-line default uses about 1 MB at 1920x1080 with RGB565.
- `LVGL configuration -> Color settings -> 16: RGB565` is recommended for 1080p. ARGB8888 is still supported by the PPA path but doubles LVGL draw-buffer traffic.
- Keep `Board Support Package -> Display -> Set number of frame buffers` at 1 for this partial-update path. Additional DPI framebuffers consume PSRAM but are not selected by these external LVGL buffers.
- The benchmark invalidates much more of the screen than a normal UI. Actual application performance depends strongly on the changed area and LVGL widget complexity.

## Troubleshooting

- If initialization fails immediately, confirm the project is still configured for `CONFIG_BSP_LCD_TYPE_HDMI=y` and RGB888 output.
- If the screen stays black, re-check the HDMI monitor, cable, and board power.
- If PSRAM allocation fails, reduce the LVGL partial draw buffer height while keeping two buffers enabled.
- PPA SRM cannot use PSRAM buffers when flash encryption is enabled on ESP32-P4. This example targets the normal unencrypted development configuration.
