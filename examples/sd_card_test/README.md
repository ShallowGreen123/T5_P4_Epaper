# sd_card_test

`sd_card_test` is a standalone ESP-IDF example for checking the T5-P4 E-Paper
uSD slot over SDSPI.

It tests:

- SD card type and protocol information
- raw card capacity and sector size
- FAT filesystem total and free space
- file write, close, reopen, read, and content verification
- sequential file read/write throughput

## Default Pins

| Signal | GPIO |
| --- | --- |
| MISO | 44 |
| SCK | 45 |
| MOSI | 46 |
| CS | 47 |

The pins, mount point, test file names, SDSPI frequency, and speed test size can
be changed with `idf.py menuconfig` under `SD card test`.

## Build

```bash
idf.py set-target esp32p4
idf.py build
```

## Flash

```bash
idf.py -p <PORT> flash monitor
```

## Expected Logs

The monitor should print logs similar to:

```text
I (...) sd_card_test: Mounting SD card at /sdcard over SDSPI
Name: ...
Type: SDHC/SDXC
I (...) sd_card_test: Card type: SDHC/SDXC
I (...) sd_card_test: Card capacity: ...
I (...) sd_card_test: FAT filesystem: total=..., free=...
I (...) sd_card_test: File read/write verification passed
I (...) sd_card_test: SD write speed: ...
I (...) sd_card_test: SD read speed: ...
```

The test file is left on the card as `/sdcard/sdtest.txt`. The default uses an
8.3-style file name so it also works when FatFS long filename support is off.
The temporary speed test file `/sdcard/sdbench.bin` is removed after the read
test.
