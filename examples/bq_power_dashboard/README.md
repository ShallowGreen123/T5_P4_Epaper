# bq_power_dashboard

## What This Example Does

This example reads the `BQ25896` charger and the `BQ27220` fuel gauge over I2C, then renders their live status on the T5-P4 E-Paper panel with `LVGL` and `FastEPD`.

## Prerequisites

- A LilyGo T5-P4 E-Paper board with the default power-management hardware available.
- A battery connected if you want meaningful gauge data.
- USB power connected if you want to observe VBUS and charging state changes.
- Default I2C wiring is `SDA=GPIO7`, `SCL=GPIO8`, `BQ25896=0x6B`, `BQ27220=0x55`.
- Optional: adjust refresh periods or I2C settings in `idf.py menuconfig` under `BQ Power Dashboard`.

## Build and Flash

```bash
idf.py -C examples/bq_power_dashboard set-target esp32p4
idf.py -C examples/bq_power_dashboard build
idf.py -C examples/bq_power_dashboard -p <PORT> flash monitor
```

## Expected Log Output

You should see lines similar to:

```text
I (...) bq_power_dash: Power dashboard start: SDA=7 SCL=8 FREQ=400000 BQ25896=0x6B BQ27220=0x55
I (...) bq_power_dash: epaper panel ready
I (...) bq_power_dash: BQ25896 ready at 0x6B
I (...) bq_power_dash: BQ27220 ready at 0x55
```

If one of the chips is not detected, the dashboard still boots but shows warnings in the log.

## Troubleshooting

- If `BQ25896 ready` or `BQ27220 ready` never appears, verify the I2C bus, chip addresses, and board power rails.
- If you see `dashboard init failed` or e-paper initialization errors, check panel bring-up and PSRAM availability.
- If the numbers look wrong, connect a battery and USB power, then wait for a few refresh cycles.