# M5GFX WiFi Auto Scan

这个示例在 `T5-P4 E-Paper V0.2` 上完成下面几件事：

- 上电后立即扫描周围 WiFi
- 在墨水屏上显示附近 WiFi 的名称和 RSSI
- 自动检测并优先连接下面两个目标 WiFi 里信号更强的一个
- 如果首选连接失败，会继续尝试另一个目标 WiFi
- 如果两个目标 WiFi 都没扫到，则每 10 秒重新扫描一次，只显示不连接
- 连接成功后，在屏幕上显示已连接的 SSID、RSSI 和 IP 地址

固定的目标 WiFi：

- `xinyuandianzi` / `AA15994823428`
- `LilyGo-AABB` / `xinyuandianzi`

## 依赖

- 屏幕驱动：`m5stack/m5gfx: ^0.2.23`
- Hosted WiFi：`espressif/esp_wifi_remote: ^1.5.0`

## 前提

- 板载 ESP32-C6 需要提前烧录兼容的 `esp-hosted` slave 固件
- 参考：
  - `docs/esp-hosted-c6-Slave.md`
  - `firmware/esp_hosted_slave_c6_sdio_2.12.3.bin`

## 编译和烧录

```bash
idf.py -C examples/m5gfx_wifi_auto_scan set-target esp32p4
idf.py -C examples/m5gfx_wifi_auto_scan build
idf.py -C examples/m5gfx_wifi_auto_scan -p COMx flash monitor
```

## 屏幕行为

- 扫描阶段：显示附近 AP 列表、两个目标 WiFi 的检测结果，以及下一步动作
- 连接成功：显示 `SSID / RSSI / IP`
- 连接丢失：提示断开原因，然后重新回到扫描流程
