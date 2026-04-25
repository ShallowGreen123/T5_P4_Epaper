# hdmi_video_renderer

## What This Example Does

This example mounts the SD card, loads an MP4 file, decodes it, and renders the video to an external HDMI monitor through the board's LT8912B HDMI bridge.

Audio playback is disabled by default, so the stock configuration is a video-only player.

## Prerequisites

- A LilyGo T5-P4 board configured for the HDMI path.
- An HDMI monitor connected before or during boot.
- A microSD card inserted in the onboard slot.
- A playable MP4 file on the card. The default file name is `test_video.mp4`, and you can change it in `idf.py menuconfig` under `HDMI MP4 Player Configuration`.

## Build and Flash

```bash
idf.py -C examples/hdmi_video_renderer set-target esp32p4
idf.py -C examples/hdmi_video_renderer menuconfig
idf.py -C examples/hdmi_video_renderer build
idf.py -C examples/hdmi_video_renderer -p <PORT> flash monitor
```

## Expected Log Output

You should see lines similar to:

```text
I (...) main: Starting HDMI MP4 Player application
I (...) main: Display timing: 800x600, DSI lane bitrate: <...> Mbps
I (...) main: Allocated <N> external decode buffers (<size> bytes each)
I (...) main: Stream adapter initialized with RGB888/BGR at 800x600 (video-only)
I (...) main: Starting loop playback of /sdcard/test_video.mp4
I (...) main: Media info: <width>x<height>, <fps> fps, duration: <ms> ms
```

If the file is missing, you will instead see `MP4 file not found` in the log.

## Troubleshooting

- If the monitor stays blank, verify the HDMI cable, monitor resolution support, and LT8912B display bring-up.
- If the SD card mounts but playback never starts, check the file name in `menuconfig` and confirm the MP4 is present on the card.
- If playback stutters, try a lower-resolution file or simplify the decode path before enabling audio.