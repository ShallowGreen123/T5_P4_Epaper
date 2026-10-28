# fastEPD_lvgl_demo

## What This Example Does

This example runs an `LVGL` user interface on the T5-P4 E-Paper panel using `FastEPD`. It also tries to bring up the GT911 touch controller so you can interact with the UI directly on the board.

## Prerequisites

- A LilyGo T5-P4 E-Paper board.
- PSRAM enabled as required by the example defaults.
- Optional touch support through the onboard GT911 controller.
- No network setup is required; the Wi-Fi screen is present in the UI, but scanning is intentionally disabled in this standalone version.

## Build and Flash

```bash
idf.py -C examples/fastEPD_lvgl_demo set-target esp32p4
idf.py -C examples/fastEPD_lvgl_demo build
idf.py -C examples/fastEPD_lvgl_demo -p <PORT> flash monitor
```

## Expected Log Output

You should see lines similar to:

```text
I (...) fastEPD_lvgl: Free SPIRAM before setup: <bytes>
I (...) fastEPD_lvgl: GT911 ready
I (...) fastEPD_lvgl: epaper w = 1440, h = 720
```

If touch is unavailable, the example continues and prints a warning instead of aborting.

## Troubleshooting

- If the example runs but touch does not work, check the GT911 path and look for `GT911 init ... failed` warnings.
- If framebuffer allocation fails, confirm PSRAM is enabled and the board boots with the expected memory configuration.
- If e-paper refresh looks wrong, review panel mode, dithering, and rotation settings in the source or project config.