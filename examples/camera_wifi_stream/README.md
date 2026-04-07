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
