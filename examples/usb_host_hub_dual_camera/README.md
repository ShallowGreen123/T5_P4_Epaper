# USB Hub Dual Camera (T5-P4 E-Paper)

This example uses [usb_host_uvc](https://components.espressif.com/components/espressif/usb_host_uvc) to open one or more external **USB UVC** cameras (through a USB hub if needed) and preview MJPEG frames from a web page.

- MJPEG preview only
- USB hub is supported (multi-camera)
- Current frame can be saved from the web UI

## Scope Boundary

This example is a **USB UVC host** path.

- It does **not** use onboard MIPI sensors (`OV2710`, `SC2336`, `OV5645`)
- It does **not** control camera rails through `SGM38121`

If you want onboard MIPI camera + `SGM38121` power flow, use `examples/camera_wifi_stream`.

## Hardware Requirements

- Board: LilyGo T5-P4 E-Paper (`esp32p4`)
- External UVC camera(s), optionally with a powered USB hub
- USB cabling:
  - Board USB input/power port connected to host power
  - Board USB OTG port connected to UVC camera or USB hub
- For Wi-Fi on T5-P4: onboard ESP32-C6 must already be flashed with `esp-hosted` slave firmware
  - See `docs/esp-hosted-c6-Slave.md`

USB signal mapping for ESP32-P4 host is `USB_DP=GPIO50`, `USB_DM=GPIO49`.

## Build and Flash

```bash
idf.py set-target esp32p4
idf.py build
idf.py -p <PORT> flash monitor
```

## Web Preview

- Default AP SSID: `ESP-USB-UVC-Demo`
- Default AP IP: `192.168.4.1`
- Open `http://192.168.4.1` in a browser

Safari is not recommended for this stream page.

## Frontend Source

See [frontend_source](./frontend_source).

## Example Log Snippet

```text
I (32025) uvc: Device connected
I (32110) uvc: Cam[1] uvc_stream_index = 0
I (32246) uvc: Opening the UVC[0]...
I (32330) uvc: Opening the UVC[1]...
```
