# usb_host_hub_dual_camera

## What This Example Does

This example runs the ESP32-P4 as a USB host, opens one or more external UVC cameras, and serves their MJPEG frames through a web UI. A USB hub can be used when you want to attach more than one camera.

## Prerequisites

- A LilyGo T5-P4 board with the USB OTG path available.
- One or more external USB UVC cameras. A powered USB hub is recommended for multi-camera setups.
- Correct OTG cabling between the board and the camera or hub.
- If you want Wi-Fi access, the onboard ESP32-C6 must be flashed with compatible `esp-hosted` slave firmware.
- Optional: configure STA or SoftAP settings in `idf.py menuconfig`.

## Build and Flash

```bash
idf.py -C examples/usb_host_hub_dual_camera set-target esp32p4
idf.py -C examples/usb_host_hub_dual_camera build
idf.py -C examples/usb_host_hub_dual_camera -p <PORT> flash monitor
```

## Expected Log Output

You should see lines similar to:

```text
I (...) app_uvc: USB Host installed. Waiting for devices on USB OTG (GPIO49/GPIO50).
I (...) usb_monitor: USB monitor ready. Waiting for devices on the board USB OTG port.
I (...) usb_monitor: UVC camera with MJPEG support detected on OTG.
I (...) app_wifi: got ip:192.168.x.x
I (...) app_https: Received request for URI: /
```

When cameras stream successfully, the log also prints per-camera frame and mode selection messages.

## Troubleshooting

- If a camera is detected but rejected, confirm it is a real UVC device with MJPEG support because this demo does not handle every USB video format.
- If enumeration is unstable, use a powered hub and verify OTG wiring and power budget.
- If the web page loads but no image appears, check camera mode selection logs and make sure the selected stream index matches a valid MJPEG mode.