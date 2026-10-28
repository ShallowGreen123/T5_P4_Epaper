# camera_wifi_stream

## What This Example Does

This example powers an onboard MIPI camera through `SGM38121`, captures frames on the ESP32-P4, encodes them as MJPEG, and serves a live stream over Wi-Fi.

It provides:

- `GET /` for a simple preview page
- `GET /stream` for the MJPEG stream

## Prerequisites

- A LILYGO T5-P4 E-Paper V0.3 board with ESP32-P4 revision v3.2 and a supported camera module. The default configuration requires revision v3.1 or later.
- The onboard ESP32-C6 must be flashed with compatible `esp-hosted` slave firmware.
- Wi-Fi SSID and password configured in `idf.py menuconfig`.
- ESP-IDF 6.1 or later (validated with 6.1).
- Default camera setup targets `OV2710`; change the sensor settings in `menuconfig` if your hardware is different.
- Optional: adjust AVDD and DVDD rails in `Camera WiFi Stream Configuration` if your sensor requires them.

## Build and Flash

```bash
idf.py -C examples/camera_wifi_stream set-target esp32p4
idf.py -C examples/camera_wifi_stream menuconfig
idf.py -C examples/camera_wifi_stream build
idf.py -C examples/camera_wifi_stream -p <PORT> flash monitor
```

When migrating an existing checkout, activate ESP-IDF 6.1 and run `idf.py -C examples/camera_wifi_stream fullclean` before rebuilding. Bootloader logging is limited to warnings and errors to fit before the existing partition table at `0x8000`.

## Expected Log Output

You should see lines similar to:

```text
I (...) camera_wifi_stream: ESP32-P4 camera WiFi MJPEG stream example
I (...) camera_wifi_stream: Frontlight off (GPIO53/GPIO52) before camera power-up
I (...) camera_wifi_stream: SGM38121 CHIP_REV=0x80
I (...) camera_wifi_stream: OV2710 ready: PID=0x2710 (attempt 1)
I (...) camera_wifi_stream: video driver=<...> card=<...> bus=<...>
I (...) camera_wifi_stream: camera chip id: pid=0x2710
I (...) camera_wifi_stream: First frame verified: 1280x720, raw=1843200 bytes, JPEG=<...> bytes
I (...) camera_wifi_stream: WiFi connected, IPv4=192.168.x.x
I (...) camera_wifi_stream: HTTP server ready
I (...) camera_wifi_stream: Open http://192.168.x.x/ or http://192.168.x.x/stream
```

When a browser connects to `/stream`, the log also reports MJPEG client connect and disconnect events.

## Troubleshooting

- If CMake reports that the bootloader source path does not match an earlier IDF version, run `idf.py -C examples/camera_wifi_stream fullclean` and rebuild in the IDF 6.1 environment.
- On the tested V0.3 board, omitting `factory`'s frontlight initialization left OV2710 returning `PID=0x0000` despite an ACK at `0x36`. Driving the active-high frontlight control GPIO53 low restored `PID=0x2710`; changing SCCB speed or adding a power-cycle delay did not. This example now holds both frontlight channels (GPIO53/GPIO52) low before camera power-up. The electrical mechanism was not measured; do not treat the symptom as proof that the camera module is absent.
- Camera rail voltages and sequencing follow `factory`, including a total 40 ms settling time. SGM38121 voltage and enable registers are read back to check the settings; these reads do not measure physical rail voltages.
- For the default OV2710 wiring (`RESET/PWDN=-1`), the example polls ID registers `0x300A/0x300B` for `0x2710` before initializing video. An address ACK alone is not treated as a ready sensor. A failed check logs the raw ID and I2C result and stops without rebooting. If explicit RESET/PWDN GPIOs are configured, `esp_video` handles their sequence and sensor detection.
- Before starting Wi-Fi, the example dequeues a frame with a two-second timeout and verifies JPEG encoding. `First frame verified` confirms actual image delivery in addition to sensor detection.
- The camera defaults follow `factory`: I2C 100 kHz on SDA7/SCL8, `AVDD1=1800 mV`, `AVDD2=2800 mV`, both DVDD rails disabled, and the existing shared I2C bus is passed to `esp_video`.
- If the example stops at camera initialization, verify `SGM38121` rail settings, the selected sensor, and camera ribbon seating.
- If Wi-Fi never comes up, make sure the ESP32-C6 hosted slave firmware is installed and the SSID is not empty.
- The smooth streaming defaults are JPEG quality 65 and 20 FPS. The output limiter now uses a time deadline to keep frame spacing regular, and the stream socket enables TCP_NODELAY to reduce small-chunk latency. If the link still cannot sustain the stream, lower `Maximum MJPEG stream FPS` to 15 or reduce JPEG quality to 55 in menuconfig.

## Hardware Validation

Validated on COM215 with ESP-IDF 6.1, ESP32-P4 v3.2 and OV2710 on 2026-10-27: sensor ID `0x2710`, 1280x720 RGB565 capture (1,843,200 bytes), and hardware JPEG encoding succeeded. The test configuration had an empty Wi-Fi SSID, so browser streaming was not exercised. Set the SSID/password under `Camera WiFi Stream Configuration` before using `/stream`.
