# usb_device_msc_wireless_disk

## What This Example Does

This example exposes the same FAT filesystem through two paths at once:

- USB Mass Storage Class, so a PC can access the card as a removable disk.
- A browser-based Wi-Fi file manager, so you can upload, download, and delete files over HTTP.

## Prerequisites

- A LilyGo T5-P4 E-Paper board.
- A microSD card inserted in the onboard slot.
- A USB data cable connected to the board.
- The onboard ESP32-C6 flashed with compatible `esp-hosted` slave firmware for Wi-Fi.
- Optional: configure AP or STA settings in `menuconfig` or through the web UI.

## Build and Flash

```bash
idf.py -C examples/usb_device_msc_wireless_disk set-target esp32p4
idf.py -C examples/usb_device_msc_wireless_disk build
idf.py -C examples/usb_device_msc_wireless_disk -p <PORT> flash monitor
```

## Expected Log Output

You should see lines similar to:

```text
I (...) app_main: Mounting FAT filesystem
I (...) app_main: using external sdcard
I (...) app_main: USB MSC initialization DONE
I (...) file_server: Starting HTTP Server
I (...) app_wifi: wifi_init_softap finished.SSID:<ssid> password:<password>
I (...) app_wifi: got ip:192.168.4.1
```

During uploads and downloads, the HTTP server also prints file activity such as `Starting upload`, `File writing complete`, and `Sending file`.

## Troubleshooting

- If the card does not appear over USB, check the SD mount logs first because the MSC layer depends on the filesystem coming up cleanly.
- If Wi-Fi does not start, verify the ESP32-C6 hosted firmware and the AP or STA settings stored in NVS.
- If the PC file explorer does not refresh after remote changes, use the built-in MSC reset path to force USB re-enumeration.