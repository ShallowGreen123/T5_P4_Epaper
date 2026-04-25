# camera_id_detect

## What This Example Does

This example powers the camera rails through `SGM38121`, probes supported camera sensors over SCCB/I2C, and reports which onboard MIPI camera is connected.

Supported sensors in the default build are:

- `SC2336`
- `OV2710`
- `OV5645`

## Prerequisites

- A LilyGo T5-P4 E-Paper board with one of the supported camera modules connected.
- Default shared I2C wiring is `SDA=GPIO7`, `SCL=GPIO8`.
- Default camera power controller address is `SGM38121=0x28`.
- Optional: adjust `RESET`, `PWDN`, `XCLK`, camera order, or rail voltages in `idf.py menuconfig`.

## Build and Flash

```bash
idf.py -C examples/camera_id_detect set-target esp32p4
idf.py -C examples/camera_id_detect build
idf.py -C examples/camera_id_detect -p <PORT> flash monitor
```

## Expected Log Output

You should see lines similar to:

```text
I (...) camera_id_detect: Camera ID detect example started
I (...) camera_id_detect: SGM38121 CHIP_REV=0x80
I (...) camera_id_detect: Enabling camera rails: AVDD1=1800 mV AVDD2=2800 mV
I (...) camera_id_detect: Trying OV2710 on SCCB address 0x36
I (...) camera_id_detect: Camera match: OV2710 addr=0x36 pid=0x2710
I (...) camera_id_detect: Detected camera model: OV2710
```

If no supported device responds, the example prints warnings and exits cleanly.

## Troubleshooting

- If `SGM38121 probe failed` appears, verify the shared I2C bus and camera power controller address.
- If the rails come up but no camera matches, check `RESET`, `PWDN`, `XCLK`, and SCCB wiring in `menuconfig`.
- If you see I2C timeouts, inspect pull-ups and camera cable seating before changing software settings.