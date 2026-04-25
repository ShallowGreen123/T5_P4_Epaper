# es8311_mic_speak

## What This Example Does

This example captures audio from the onboard microphone through the `ES8311` codec and immediately plays it back to the speaker, creating a real-time mic-to-speaker loopback test.

## Prerequisites

- A LilyGo T5-P4 E-Paper board.
- Working onboard audio path, including `ES8311` and the amplifier enable line on `PCA9535` IO5.
- Default shared I2C is `GPIO7/8`.
- Optional: adjust speaker volume and microphone gain in `idf.py menuconfig`.

## Build and Flash

```bash
idf.py -C examples/es8311_mic_speak set-target esp32p4
idf.py -C examples/es8311_mic_speak build
idf.py -C examples/es8311_mic_speak -p <PORT> flash monitor
```

## Expected Log Output

You should see lines similar to:

```text
I (...) es8311_mic_speak: ES8311 microphone loopback example
I (...) board_es8311: I2C ready on SDA=7 SCL=8
I (...) board_es8311: Audio amplifier enabled through PCA9535 IO5
I (...) board_es8311: ES8311 ready at 16000 Hz, volume=<...>, mic_gain=<...>
I (...) board_es8311: Microphone loopback started
```

If audio transfer stalls, the log may also print I2S read, write, or underrun warnings.

## Troubleshooting

- If you hear nothing, check amplifier enable, codec initialization, and speaker wiring on the board.
- If `I2S read failed` or `I2S write failed` appears, review the I2S clock and pin configuration.
- If the loopback is noisy or too quiet, tune microphone gain and output volume in `menuconfig`.