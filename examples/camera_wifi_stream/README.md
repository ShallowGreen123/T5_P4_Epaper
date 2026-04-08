# camera_wifi_stream

Minimal ESP-IDF MJPEG streaming example for T5-P4 E-Paper. It powers the MIPI camera through SGM38121, starts ESP32-P4 MIPI-CSI capture, connects to WiFi through the onboard ESP32-C6 hosted link, and serves:

- `GET /` - simple page with a live image
- `GET /stream` - `multipart/x-mixed-replace` MJPEG stream

Build:

```sh
idf.py set-target esp32p4
idf.py menuconfig
idf.py build
```

Set `Camera WiFi Stream Configuration -> WiFi SSID/password` before flashing.

Streaming defaults are tuned for the ESP32-C6 hosted WiFi link: JPEG quality 88, a
12 fps MJPEG output cap, 4 capture buffers, and WiFi power save disabled. If the
picture is still soft, first adjust the lens focus, then try raising `JPEG quality`;
if it stutters, lower `Maximum MJPEG stream FPS`.

SGM38121 `DVDD1` and `DVDD2` default to `0 mV`, which keeps those outputs
disabled. Set non-zero DVDD targets in menuconfig only if your camera module
needs those rails.
