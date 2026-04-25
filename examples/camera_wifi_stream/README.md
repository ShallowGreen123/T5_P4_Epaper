# camera_wifi_stream

## What This Example Does

This example powers an onboard MIPI camera through `SGM38121`, captures frames on the ESP32-P4, encodes them as MJPEG, and serves a live stream over Wi-Fi.

It provides:

- `GET /` for a simple preview page
- `GET /stream` for the MJPEG stream

## Prerequisites

- A LilyGo T5-P4 E-Paper board with a supported camera module.
- The onboard ESP32-C6 must be flashed with compatible `esp-hosted` slave firmware.
- Wi-Fi SSID and password configured in `idf.py menuconfig`.
- Default camera setup targets `OV2710`; change the sensor settings in `menuconfig` if your hardware is different.
- Optional: adjust AVDD and DVDD rails in `Camera WiFi Stream Configuration` if your sensor requires them.

## Build and Flash

```bash
idf.py -C examples/camera_wifi_stream set-target esp32p4
idf.py -C examples/camera_wifi_stream menuconfig
idf.py -C examples/camera_wifi_stream build
idf.py -C examples/camera_wifi_stream -p <PORT> flash monitor
```

## Expected Log Output

You should see lines similar to:

```text
I (...) camera_wifi_stream: ESP32-P4 camera WiFi MJPEG stream example
I (...) camera_wifi_stream: SGM38121 CHIP_REV=0x80
I (...) camera_wifi_stream: video driver=<...> card=<...> bus=<...>
I (...) camera_wifi_stream: camera chip id: pid=0x2710
I (...) camera_wifi_stream: WiFi connected, IPv4=192.168.x.x
I (...) camera_wifi_stream: HTTP server ready
I (...) camera_wifi_stream: Open http://192.168.x.x/ or http://192.168.x.x/stream
```

When a browser connects to `/stream`, the log also reports MJPEG client connect and disconnect events.

## Troubleshooting

- If the example stops at camera initialization, verify `SGM38121` rail settings, the selected sensor, and camera ribbon seating.
- If Wi-Fi never comes up, make sure the ESP32-C6 hosted slave firmware is installed and the SSID is not empty.
- If the stream is unstable, lower `Maximum MJPEG stream FPS`, reduce resolution, or improve Wi-Fi signal quality.