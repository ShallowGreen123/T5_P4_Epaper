# i2c_tools

## What This Example Does

This example starts an interactive serial console that exposes common I2C helper commands such as `i2cconfig`, `i2cdetect`, `i2cget`, `i2cset`, and `i2cdump`.

It is useful when you want to probe an I2C device without writing a dedicated application first.

## Prerequisites

- A supported ESP-IDF target. In this repository, it is commonly used with `esp32p4`.
- A serial monitor connection, because this example runs as a console application.
- An I2C device connected to the selected SDA and SCL pins.
- Optional: change the default console transport or I2C pins in `menuconfig`.

## Build and Flash

```bash
idf.py -C examples/i2c_tools set-target esp32p4
idf.py -C examples/i2c_tools build
idf.py -C examples/i2c_tools -p <PORT> flash monitor
```

## Expected Log Output

After boot, you should see a banner and a prompt similar to:

```text
==============================================================
|             Steps to Use i2c-tools                         |
==============================================================
i2c-tools>
```

Typical interactive commands are:

```text
i2c-tools> help
i2c-tools> i2cconfig
i2c-tools> i2cdetect
```

## Troubleshooting

- If you never see the `i2c-tools>` prompt, check the selected console interface and your serial monitor settings.
- If `i2cdetect` shows no devices, reconfigure the bus pins with `i2cconfig` and verify pull-ups and wiring.
- If register reads fail, double-check device address width and whether the target chip expects 8-bit or 16-bit register addresses.


i2cconfig  --port=0 --freq=10000 --sda=7 --scl=8