# M5GFX EPD Test

This ESP-IDF example drives the T5-P4 E-Paper panel through the managed `m5stack/m5gfx` component declared in `main/idf_component.yml`.

It uses the same ED047TC1-style grayscale waveform path as the T5S3 reader project. The EPD data bus and PCA9535/TPS651851 wiring are shared by the T5-P4 V0.2 and V0.3 boards:

- 1440 x 720 panel
- 8-bit EPD data bus on GPIO27..GPIO34
- CKH/STH/LEH/CKV/STV on GPIO24/GPIO25/GPIO26/GPIO13/GPIO48
- PCA9535 + TPS651851 power sequence
- Horizontal mirror compensation matching the reference FastEPD `MIRROR_X` panel flag

Before queuing a waveform refresh, the example performs a TPS651851 power preflight. It waits for the four panel rails reported by `PWR_GOOD`, preserves the required VCOM2 register bit, and aborts the refresh after a single diagnostic timeout instead of retrying the power sequence for every waveform frame.

The power preflight also observes the TPS65185x minimum 1.8 ms WAKEUP-to-I2C delay and validates `REVID` before writing power registers. An invalid ID such as `0x00` or `0xFF` indicates that U6 is not accessible; check U6 power and WAKEUP, as well as another device using the fixed TPS address `0x68`.

![](./run.jpg)

Build from this directory:

```sh
idf.py set-target esp32p4
idf.py build flash monitor
```
