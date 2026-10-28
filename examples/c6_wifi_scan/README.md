# c6_wifi_scan

## What This Example Does

This example repeatedly scans for Wi-Fi networks on the ESP32-P4 by forwarding Wi-Fi control to the onboard ESP32-C6 through `esp_wifi_remote` and `esp_hosted` over SDIO. Each scan is followed by a five-second delay.

## Prerequisites

- A LILYGO T5-P4 E-Paper V0.3 board with ESP32-P4 revision v3.2. The default configuration requires revision v3.1 or later.
- The onboard ESP32-C6 must already be flashed with `esp-hosted` 2.12.3 slave firmware; the host dependency is pinned to this version.
- ESP-IDF 6.1 or later (validated with 6.1).
- Optional reference files:
  `docs/esp-hosted-c6-Slave.md` and `firmware/esp_hosted_slave_c6_sdio_2.12.3.bin`.

## Build and Flash

```bash
idf.py -C examples/c6_wifi_scan set-target esp32p4
idf.py -C examples/c6_wifi_scan build
idf.py -C examples/c6_wifi_scan -p <PORT> flash monitor
```

When migrating an existing checkout, activate ESP-IDF 6.1 and run `idf.py -C examples/c6_wifi_scan fullclean` before the commands above. `set-target` regenerates the configuration from the updated defaults.

Bootloader logging is limited to warnings and errors to fit before the existing partition table at `0x8000`. Application scan logs remain enabled.

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

- If CMake reports that the bootloader source path does not match an earlier IDF version, run `idf.py -C examples/c6_wifi_scan fullclean` and rebuild in the IDF 6.1 environment. `idf.py reconfigure` alone does not clear the nested bootloader cache.
- If the scan never starts, confirm the ESP32-C6 slave firmware is flashed and matches the hosted stack version.
- If SDIO-related errors appear, re-check the hosted wiring assumptions and board support configuration.
- If the scan completes but returns zero APs, move closer to an access point and verify the antenna path on the board.
