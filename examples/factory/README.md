# factory

## What This Example Does

This example is a touch-driven factory test and hardware showcase for the T5-P4 E-Paper board. It brings up the display and touch stack, then exposes pages for features such as battery, SD card, audio, camera, HDMI, and Wi-Fi.

## Prerequisites

- A LilyGo T5-P4 E-Paper V0.3 board.
- Optional peripherals depending on the page you want to test:
  battery, USB OTG load, SD card, camera module, HDMI monitor, and Wi-Fi environment.
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

## USB OTG Test

Select `USB OTG` directly on the main page, then choose a role:

- `Host`: with a healthy battery and no external VBUS, the board enables the
  BQ25896 5 V OTG output and starts the ESP-IDF USB Host stack. `PASS` means a
  USB Device was enumerated; the page shows its address, VID/PID, class, speed,
  and the number of connected devices.
- `Device`: the board disables OTG boost and exposes a factory HID keyboard
  interface. Connect the OTG port to a PC or another USB Host. `PASS` means the
  Host completed USB enumeration.

Role changes fully stop the current USB stack before starting the other one.
Leaving the page stops USB, disables OTG boost, and restores normal charging.

## Troubleshooting

- If the UI never appears, start with the display and touch logs before debugging the optional feature pages.
- If a specific page fails, test that peripheral in its dedicated example first, then return to `factory`.
- If the screen ghosts heavily during testing, use the BOOT button to request a full e-paper refresh.
