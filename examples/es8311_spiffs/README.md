# es8311_spiffs

## What This Example Does

This example builds a SPIFFS image from the local `spiffs/` folder, mounts it at runtime, finds the first supported audio file, decodes it, and plays it through the onboard `ES8311` codec and speaker.

## Prerequisites

- A LilyGo T5-P4 E-Paper board.
- At least one supported audio file placed in `examples/es8311_spiffs/spiffs/` before building.
- The default partition table and SPIFFS settings from this example.
- Optional: make sure the onboard audio amplifier path is available.

## Build and Flash

```bash
idf.py -C examples/es8311_spiffs set-target esp32p4
idf.py -C examples/es8311_spiffs build
idf.py -C examples/es8311_spiffs -p <PORT> flash monitor
```

The SPIFFS image is generated and flashed together with the firmware.

## Expected Log Output

You should see lines similar to:

```text
I (...) es8311_spiffs: SPIFFS mounted: total=<...> used=<...>
I (...) es8311_spiffs: Listing files in /spiffs
I (...) es8311_spiffs: Selected audio file: /spiffs/<file>
I (...) es8311_spiffs: Playback started
I (...) es8311_spiffs: Playback completed
```

## Troubleshooting

- If SPIFFS mounts but no track is selected, confirm there is at least one supported audio file in the `spiffs/` folder before build time.
- If playback starts but you hear nothing, check codec initialization and the amplifier enable path.
- If you get `Unsupported audio file type`, try a known-good MP3 or other format supported by the bundled audio player.