# icm20948

## What This Example Does

This example uses the Espressif Component Registry package `espp/icm20948^1.0.36` to initialize an `ICM-20948` 9-axis sensor on the LilyGo T5-P4 E-Paper board and print the readings to the serial monitor.

It outputs:

- 3-axis accelerometer data
- 3-axis gyroscope data
- 3-axis magnetometer data
- die temperature

## Board Assumptions

- Shared I2C bus: `SDA=GPIO7`, `SCL=GPIO8`
- The sensor address is expected to be one of `0x29` or `0x69`
- For compatibility with the `ICM-20948` datasheet and the `espp` driver, the example also falls back to `0x68` if needed

The repository pin map currently mentions `0x29/0x69`, while the `ICM-20948` datasheet and `espp/icm20948` use the normal address pair `0x68/0x69`. Because of that mismatch, the example probes `0x29`, then `0x69`, then `0x68`, and only accepts a device whose `WHO_AM_I` register returns `0xEA`.

## Build and Flash

```bash
idf.py -C examples/icm20948 set-target esp32p4
idf.py -C examples/icm20948 build
idf.py -C examples/icm20948 -p <PORT> flash monitor
```

## Runtime Output

After boot, you should see logs similar to:

```text
I (...) icm20948: Starting ICM20948 example on T5-P4 E-Paper
I (...) icm20948: Address probe order: 0x29 -> 0x69 -> 0x68
I (...) icm20948: Candidate 0x69 responded with WHO_AM_I=0xEA
I (...) icm20948: ICM20948 ready at 0x69, WHO_AM_I=0xEA, AK09916 ID=0x4809
I (...) icm20948: #1 dt=200.0ms accel[g] x=... y=... z=... gyro[dps] x=... y=... z=... mag[uT] x=... y=... z=... temp[C]=...
```

## Configuration

The example exposes two menuconfig items:

- `ICM20948_I2C_FREQ_HZ`
- `ICM20948_SAMPLE_PERIOD_MS`

## Troubleshooting

- If none of `0x29`, `0x69`, or `0x68` work, the example will scan the whole I2C bus and print every detected device.
- If the sensor ACKs but `WHO_AM_I` is not `0xEA`, the responding device is not an `ICM-20948`.
- If you only see probe timeouts, check bus pull-ups, solder bridges, and whether the sensor module is actually populated.
