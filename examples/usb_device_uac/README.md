# USB UAC Example (LilyGo T5-P4 E-Paper)

This example turns the T5-P4 E-Paper into a USB Audio Class (UAC) device.

- USB speaker playback (host -> T5-P4)
- USB microphone capture (T5-P4 -> host)
- Host-driven volume and mute control

Internally it uses the onboard ES8311 codec and enables the speaker amplifier through PCA9535 IO expander (IO5).

## Hardware Required

- LilyGo T5-P4 E-Paper (`esp32p4`)
- USB Type-C data cable
- Speaker/headphone connected to the board audio path

## Configuration

Use `idf.py menuconfig`:

- `Example Configuration` -> select `LilyGo T5-P4 E-Paper`
- `USB Device UAC` -> configure speaker/microphone channels and sample rate

Note: microphone channel count is supported up to 2 channels.

## Build and Flash

```bash
idf.py set-target esp32p4
idf.py build flash monitor
```

## Expected Behavior

After flashing, connect the board USB port to a PC.
The PC should enumerate a new USB audio device. Select it as input/output to play audio to the board speaker and record from the board microphone.
