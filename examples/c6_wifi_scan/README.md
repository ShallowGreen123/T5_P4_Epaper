# c6_wifi_scan

`c6_wifi_scan` is a pure ESP-IDF example for the T5-P4 E-Paper board.
It uses `esp_wifi_remote` with `esp_hosted` so the ESP32-P4 can scan WiFi
through the on-board ESP32-C6 over SDIO.

## Requirements

- ESP-IDF 5.3 or newer
- Target set to `esp32p4`
- The on-board ESP32-C6 must already be flashed with `esp-hosted` slave firmware

You can refer to:

- `docs/esp-hosted-c6-Slave.md`
- `platformIO_P4/firmware/esp_hosted_esp32c6_slave_1.4.1.bin`

## Build

```bash
idf.py set-target esp32p4
idf.py build
```

## Flash

```bash
idf.py -p <PORT> flash monitor
```

## Expected Logs

When the hosted link is healthy, the monitor should show logs similar to:

- `esp_wifi_remote_init`
- `GPIOs: CLK[18] CMD[19] D0[14] D1[15] D2[16] D3[17] Slave_Reset[54]`
- `Received Slave ESP Init`

After that, this example prints one scan result table:

```text
Idx RSSI Ch Auth      BSSID              SSID
  1  -45  1 WPA2-PSK  aa:bb:cc:dd:ee:ff MyWiFi
```

If no AP is found, it will print that zero networks were returned instead of failing.

## Troubleshooting

- If hosted initialization never succeeds, check whether the ESP32-C6 slave firmware is present.
- If SDIO startup fails, check the board power and the default P4/C6 wiring.
- If scan start fails, inspect the first `ESP_ERROR_CHECK` failure in the serial log.
