# pca9535

## What This Example Does

This example initializes the shared I2C bus, probes a `PCA9535` I/O expander, configures all 16 GPIOs as inputs, and periodically prints the current input state.

## Prerequisites

- A LilyGo T5-P4 E-Paper board or another setup with a reachable `PCA9535` device.
- Default example wiring expects `SDA=GPIO7`, `SCL=GPIO8`.
- Default device address is `0x20`.
- Optional external signals connected to the `PCA9535` pins if you want to see the input state change.

## Build and Flash

```bash
idf.py -C examples/pca9535 set-target esp32p4
idf.py -C examples/pca9535 build
idf.py -C examples/pca9535 -p <PORT> flash monitor
```

## Expected Log Output

You should see lines similar to:

```text
I (...) pca9535_example: Starting PCA9535 ESP-IDF polling example
I (...) pca9535_example: I2C config: port=0 SDA=7 SCL=8
I (...) pca9535_example: Scanning I2C bus on SDA=7 SCL=8
I (...) pca9535_example: Found device at 0x20
I (...) pca9535_example: PCA9535 initialized on address 0x20
```

After initialization, the example prints the current 16-bit input state on a fixed interval.

## Troubleshooting

- If the scan never finds `0x20`, verify the `A0/A1/A2` address pins and the physical wiring.
- If you see timeout warnings, check pull-ups and bus voltage levels first.
- If the input state never changes, confirm the pins are actually driven externally and not left floating.