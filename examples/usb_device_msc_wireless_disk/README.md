# USB MSC Wireless Disk (T5-P4 E-Paper)

This example turns the T5-P4 E-Paper into:

- a USB MSC disk (visible on the PC file explorer), and
- a Wi-Fi file manager (upload/download/delete via web page).

Both paths access the same FAT filesystem on the external microSD card.

## Hardware Requirements

- Board: LilyGo T5-P4 E-Paper (`esp32p4`)
- microSD card inserted in the onboard slot
- USB data cable connected to the board USB port
- Onboard ESP32-C6 flashed with `esp-hosted` slave firmware
  - See `docs/esp-hosted-c6-Slave.md`

## Default Storage and Pins

The example defaults to SDSPI on T5-P4:

- `MISO = GPIO44`
- `SCK  = GPIO45`
- `MOSI = GPIO46`
- `CS   = GPIO47`

Mount path is `/disk`.

## Build

```bash
idf.py set-target esp32p4
idf.py build
```

## Flash

```bash
idf.py -p <PORT> flash monitor
```

## Runtime Behavior

1. On boot, the SD card is mounted as FAT.
2. TinyUSB MSC starts and exports the SD card to the host.
3. Wi-Fi starts with AP/STA auto fallback:
   - if STA credentials are configured, mode is AP+STA;
   - if STA credentials are empty, mode is AP only.
4. Open `http://192.168.4.1` to manage files over HTTP.

## Wi-Fi Configuration

You can configure Wi-Fi in either way:

- `menuconfig -> USB MSC Device Demo -> Wi-Fi Settings`
- `settings` page in the web UI (saved to NVS, then reboot)

NVS keys are kept compatible with the original UI:
`wifimode`, `apssid`, `appasswd`, `stassid`, `stapasswd`.

## Notes

- `/reset_msc` triggers USB re-enumeration to refresh host-side file view.
- If hosted Wi-Fi init fails, check C6 firmware and SDIO link first.
- If SD mount fails, verify card insertion and SDSPI pin configuration.
