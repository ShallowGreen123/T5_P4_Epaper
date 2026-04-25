# factory

## What This Example Does

This example is a touch-driven factory test and hardware showcase for the T5-P4 E-Paper board. It brings up the display and touch stack, then exposes pages for features such as battery, SD card, audio, camera, HDMI, and Wi-Fi.

## Prerequisites

- A LilyGo T5-P4 E-Paper board.
- Optional peripherals depending on the page you want to test:
  battery, SD card, camera module, HDMI monitor, and Wi-Fi environment.
- Default touch and shared I2C wiring are already configured for the board.
- Optional: use `idf.py menuconfig` to enable or disable pages such as HDMI and camera, or to tune battery and display refresh settings.

## Build and Flash

```bash
idf.py -C examples/factory set-target esp32p4
idf.py -C examples/factory build
idf.py -C examples/factory -p <PORT> flash monitor
```

## Expected Log Output

You should see lines similar to:

```text
I (...) factory_main: factory example start
I (...) factory_display: display ready: 1440x720 <mode>
I (...) factory_touch: PCA9535 ready
I (...) factory_touch: touch ready
I (...) factory_main: BOOT button full refresh enabled on GPIO35
```

Depending on which pages you open, you may also see logs for battery ICs, SD card mount, camera detection, HDMI bring-up, or Wi-Fi scans.

## Troubleshooting

- If the UI never appears, start with the display and touch logs before debugging the optional feature pages.
- If a specific page fails, test that peripheral in its dedicated example first, then return to `factory`.
- If the screen ghosts heavily during testing, use the BOOT button to request a full e-paper refresh.