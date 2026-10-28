# sgm38121

## What This Example Does

This example probes the `SGM38121` camera power-management IC, reads its registers, reports the current state, and can optionally apply board-specific rail settings for the onboard camera path.

## Prerequisites

- A LilyGo T5-P4 E-Paper board or another setup with an `SGM38121` at the expected address.
- Default I2C settings are `SDA=GPIO7`, `SCL=GPIO8`, `SGM38121=0x28`.
- Board assumptions in this example are `AVDD1 -> CAM_1V8` and `AVDD2 -> CAM_2V8`.
- Optional: enable boot-time register application in `idf.py menuconfig` if you want the example to drive the outputs instead of only reporting state.

## Build and Flash

```bash
idf.py -C examples/sgm38121 set-target esp32p4
idf.py -C examples/sgm38121 build
idf.py -C examples/sgm38121 -p <PORT> flash monitor
```

## Expected Log Output

You should see lines similar to:

```text
I (...) sgm38121_test: SGM38121 ESP-IDF test started
I (...) sgm38121_test: Scanning I2C bus on GPIO7/GPIO8
I (...) sgm38121_test: CHIP_REV = 0x80
I (...) sgm38121_test: Applying SGM38121 register-mode configuration
I (...) sgm38121_test: AVDD1 target 1800 mV (reg=0x...)
I (...) sgm38121_test: AVDD2 target 2800 mV (reg=0x...)
```

If boot-apply is disabled, the example reports that it is running in read-only mode.

## Troubleshooting

- If the probe fails, verify the I2C address, wiring, and board power state.
- If the chip responds but the voltages look wrong, review the board assumptions and configured target rails.
- If the output enable test behaves unexpectedly, confirm whether boot-apply is enabled and whether the camera rails are already in use by another test.