# camera_id_detect

`camera_id_detect` is a pure ESP-IDF example for the T5-P4 E-Paper board.
It powers the camera rails through `SGM38121`, then uses Espressif's
`esp_cam_sensor` drivers to identify one of these sensors:

- `SC2336`
- `OV2710`
- `OV5645`

The example only detects the sensor model and prints the result. It does not
start video streaming or initialize `esp_video`.

## Default assumptions

- Target chip is `esp32p4`
- Main I2C uses `SDA=GPIO7`, `SCL=GPIO8`
- `SGM38121` is at `0x28`
- `AVDD1 -> CAM_1V8`
- `AVDD2 -> CAM_2V8`
- Camera detect order is `SC2336 -> OV2710 -> OV5645`

If your board revision wires camera `RESET`, `PWDN`, or `XCLK`, you can override
those GPIOs in `menuconfig`. The defaults keep them disabled with `-1`.

## Build

```bash
idf.py build
```

This example defaults to `esp32p4`, so `idf.py set-target esp32p4` is not
required in a fresh build directory.

## Flash

```bash
idf.py -p <PORT> flash monitor
```

## Expected logs

Successful detection looks like:

```text
I (1234) camera_id: Camera match: SC2336 addr=0x30 pid=0xCB3A
I (1235) camera_id: Detected camera model: SC2336
```

If no supported camera is found, the example prints a summary plus targeted I2C
probe results for:

- `0x28` (`SGM38121`)
- `0x30` (`SC2336`)
- `0x36` (`OV2710`)
- `0x3C` (`OV5645`)

## menuconfig options

Use `idf.py menuconfig` to adjust:

- I2C SDA/SCL GPIOs
- I2C/SCCB frequency
- Camera rail enable on boot
- `AVDD1` / `AVDD2` target voltage
- Optional `RESET`, `PWDN`, `XCLK` GPIOs
- `XCLK` frequency

## Troubleshooting

- If `SGM38121` is not found at `0x28`, camera power may never come up.
- If no camera is detected, verify camera wiring and try configuring `RESET`,
  `PWDN`, or `XCLK` if your hardware needs them.
- If one of the camera addresses ACKs but detection still fails, the sensor may
  need control pins enabled before it will return a valid PID.
