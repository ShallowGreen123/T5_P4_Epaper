# ES8311 Mic Speak Example

This example has been converted to a pure `esp-idf` project, and the default target chip is `esp32p4`.

After boot, the application will:

- Initialize I2C on `GPIO7/8`
- Drive `PCA9535` `IO5` high to enable the audio amplifier `SHUTDOWN` line
- Initialize the `ES8311`
- Capture audio from the onboard microphone over `I2S` and play it back through the speaker in real time

I2S pins:

- `MCLK`: `GPIO43`
- `BCLK`: `GPIO42`
- `LRCK`: `GPIO40`
- `DOUT`: `GPIO39`
- `DIN`: `GPIO41`

Build and flash:

```bash
idf.py -C examples/es8311_mic_speak build
idf.py -C examples/es8311_mic_speak -p PORT flash monitor
```

You can adjust the following options in `menuconfig`:

- Speaker volume
- Microphone gain
