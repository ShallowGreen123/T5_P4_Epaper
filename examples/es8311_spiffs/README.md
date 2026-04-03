# ES8311 SPIFFS Playback Example

This example is a pure `esp-idf` project for `esp32p4`.

It will:

- build a SPIFFS image from the files inside [`spiffs`](./spiffs)
- mount the `storage` SPIFFS partition at runtime
- find the first supported audio file in SPIFFS
- decode the audio stream
- play it through the onboard ES8311 codec and speaker

Default settings:

- Target: `esp32p4`
- Partition table: `partitions_16MB.csv`
- SPIFFS partition label: `storage`
- SPIFFS mount point: `/spiffs`

Build and flash:

```bash
idf.py -C examples/es8311_spiffs build
idf.py -C examples/es8311_spiffs -p PORT flash monitor
```

The SPIFFS image is flashed automatically together with the firmware.
