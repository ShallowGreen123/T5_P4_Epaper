# usb_host_msc_example

## What This Example Does

This example runs the LilyGo T5-P4 E-Paper as a USB Host MSC client, mounts an external USB flash drive at `/usb`, and exposes a browser-based file manager over Wi-Fi.

Unlike [usb_device_msc_wireless_disk](../usb_device_msc_wireless_disk/README.md), this demo reads and writes an external USB storage device through the board USB OTG port. Unlike [usb_host_hub_dual_camera](../usb_host_hub_dual_camera/README.md), this demo targets USB Mass Storage Class devices instead of UVC cameras.

## Prerequisites

- A LilyGo T5-P4 E-Paper board.
- A USB flash drive formatted as FAT32. exFAT is possible if your ESP-IDF environment enables it.
- Correct OTG cabling between the board OTG port and the USB flash drive or powered hub.
- The onboard ESP32-C6 flashed with compatible `esp-hosted` slave firmware if you want Wi-Fi access.

The USB Host path uses the ESP32-P4 OTG pins `USB_DM=GPIO49` and `USB_DP=GPIO50`.

## Build and Flash

```bash
idf.py -C examples/usb_host_msc_example set-target esp32p4
idf.py -C examples/usb_host_msc_example build
idf.py -C examples/usb_host_msc_example -p <PORT> flash monitor
```

## Web UI

- Default AP SSID: `ESP-Host-MSC-Demo`
- Default AP password: empty
- Default URL: `http://192.168.4.1`

The web page supports:

- Listing files and directories on the mounted USB drive
- Uploading files to the USB drive
- Downloading files from the USB drive
- Deleting files from the USB drive
- Saving AP and STA Wi-Fi settings into NVS

![file_web_page](./.static/file_web.jpg)

## Expected Log Output

You should see lines similar to:

```text
I (...) app_wifi: Initializing Wi-Fi via esp_wifi_remote/esp_hosted on the onboard ESP32-C6
I (...) app_wifi: wifi_init_softap finished.SSID:ESP-Host-MSC-Demo password:
I (...) usb_host_msc_example: USB Host MSC installed. Waiting for USB storage on USB OTG (GPIO49/GPIO50).
I (...) file_server: Starting HTTP Server
I (...) app_wifi: got ip:192.168.4.1
```

After inserting a USB flash drive:

```text
I (...) usb_host_msc_example: MSC device connected on USB OTG
I (...) usb_host_msc_example: USB storage mounted at /usb
```

If the board is already running and no drive is connected, the web page still loads and reports that no disk is present yet.

## Troubleshooting

- If the page opens but file listing fails, check whether the USB flash drive really enumerated as MSC and whether it is formatted with a supported filesystem.
- If Wi-Fi does not start, verify the onboard ESP32-C6 `esp-hosted` firmware and your `menuconfig` AP or STA settings.
- If USB enumeration is unstable, use a shorter OTG cable or a powered USB hub.
- If you need exFAT, enable it in your ESP-IDF FATFS configuration first because it is not enabled by default.
