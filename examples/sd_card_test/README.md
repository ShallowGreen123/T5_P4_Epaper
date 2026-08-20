# sd_card_test

## What This Example Does

This example verifies the onboard microSD slot over SDSPI. It mounts the card, prints card and filesystem information, performs a read/write verification test, and runs a simple sequential speed benchmark.

## Prerequisites

- A LilyGo T5-P4 E-Paper board.
- A microSD card inserted in the onboard slot.
- Default SDSPI pins are `MISO=GPIO44`, `SCK=GPIO45`, `MOSI=GPIO46`, `CS=GPIO47`.
- On board V0.3, SD power is enabled by the example through XL9555 P03 (`SD_VDD_EN`) before SDSPI initialization.
- Optional: change mount point, file names, frequency, or speed test size in `idf.py menuconfig` under `SD card test`.

## Build and Flash

```bash
idf.py -C examples/sd_card_test set-target esp32p4
idf.py -C examples/sd_card_test build
idf.py -C examples/sd_card_test -p <PORT> flash monitor
```

## Expected Log Output

You should see lines similar to:

```text
I (...) sd_card_test: Starting standalone SD card test
I (...) sd_card_test: Enabling SD card power through XL9555 P03 (SD_VDD_EN)
I (...) sd_card_test: SD card power is enabled and stable
I (...) sd_card_test: Initializing SDSPI bus
I (...) sd_card_test: Mounting SD card at /sdcard over SDSPI
I (...) sd_card_test: Card type: <type>
I (...) sd_card_test: File read/write verification passed
I (...) sd_card_test: SD write speed: <...> MiB/s
I (...) sd_card_test: SD read speed: <...> MiB/s
I (...) sd_card_test: SD card test finished successfully.
```

## Troubleshooting

- If mount fails, first confirm that XL9555 P03 is high and the card socket has SD VDD, then check card insertion, SDSPI pin mapping, and pull-ups.
- If the basic file test passes but speed is poor, lower the configured SD clock or try a different card.
- If long file names fail, review the FATFS long filename configuration used by your build.
