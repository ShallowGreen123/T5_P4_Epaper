# HDMI LVGL Demo Runner

This example runs LVGL demos on the LilyGo T5-P4 HDMI path and outputs the UI through the LT8912B HDMI bridge.

The application is a dedicated LVGL demo runner. It does not mount an SD card, does not play MP4 files, and does not require any media file by default.

## Features

- HDMI output through the existing T5-P4 BSP display bring-up path.
- Default display mode: 800x600@60Hz, RGB888.
- LVGL v8.4 managed component.
- Menuconfig-selectable demos:
  - Benchmark (default)
  - Stress
  - Widgets
- Widgets demo uses slideshow mode so it is useful without touch, keyboard, or encoder input.

## Build And Flash

From the repository root:

```sh
idf.py -C examples/hdmi_video_renderer_lvgl set-target esp32p4
idf.py -C examples/hdmi_video_renderer_lvgl build
idf.py -C examples/hdmi_video_renderer_lvgl flash monitor
```

Connect an HDMI monitor before or during boot. The BSP initializes the LT8912B bridge and the selected LVGL demo starts automatically.

## Select A Demo

Open menuconfig:

```sh
idf.py -C examples/hdmi_video_renderer_lvgl menuconfig
```

Then select:

```text
HDMI LVGL Demo Configuration
  Select LVGL demo
```

Choose one of:

- `Benchmark`
- `Stress`
- `Widgets`

The default is `Benchmark`.

## HDMI Settings

The default HDMI setting is:

```text
Board Support Package (T5-P4 E-Paper)
  Display
    Use LT8912B HDMI output
    Select HDMI resolution: 800x600@60HZ
    Select LCD color format: RGB888
```

Higher HDMI resolutions can be selected from the BSP menu, but they use more PSRAM and may require performance tuning.
