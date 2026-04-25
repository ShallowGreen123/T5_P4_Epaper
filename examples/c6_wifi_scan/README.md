# c6_wifi_scan

## What This Example Does

This example performs a one-shot Wi-Fi scan on the ESP32-P4 by forwarding Wi-Fi control to the onboard ESP32-C6 through `esp_wifi_remote` and `esp_hosted` over SDIO.

## Prerequisites

- A LilyGo T5-P4 E-Paper board.
- The onboard ESP32-C6 must already be flashed with compatible `esp-hosted` slave firmware.
- ESP-IDF with `esp32p4` target support.
- Optional reference files:
  `docs/esp-hosted-c6-Slave.md` and `firmware/esp_hosted_slave_c6_sdio_2.12.3.bin`.

## Build and Flash

```bash
idf.py -C examples/c6_wifi_scan set-target esp32p4
idf.py -C examples/c6_wifi_scan build
idf.py -C examples/c6_wifi_scan -p <PORT> flash monitor
```

## Expected Log Output

You should see lines similar to:

```text
I (...) c6_wifi_scan: ESP32-P4 hosted WiFi scan example
I (...) c6_wifi_scan: Expect hosted SDIO logs for CLK[18] CMD[19] D0[14] D1[15] D2[16] D3[17] Slave_Reset[54]
I (...) c6_wifi_scan: Starting one-shot WiFi scan via esp_wifi_remote/esp_hosted
I (...) c6_wifi_scan: Scan complete, AP count: <N>
I (...) c6_wifi_scan: Retrieved <N> AP record(s)
```

A table with RSSI, channel, authentication mode, BSSID, and SSID is printed after the scan.

## Troubleshooting

- If the scan never starts, confirm the ESP32-C6 slave firmware is flashed and matches the hosted stack version.
- If SDIO-related errors appear, re-check the hosted wiring assumptions and board support configuration.
- If the scan completes but returns zero APs, move closer to an access point and verify the antenna path on the board.