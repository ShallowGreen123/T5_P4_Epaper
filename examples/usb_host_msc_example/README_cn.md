# usb_host_msc_example

## 示例功能

这个示例让 LilyGo T5-P4 E-Paper 作为 USB Host MSC 主机运行，把外接 U 盘挂载到 `/usb`，并通过 Wi-Fi 提供一个网页文件管理界面。

它和 [usb_device_msc_wireless_disk](../usb_device_msc_wireless_disk/README.md) 的区别是：本示例访问的是外接 USB 存储设备，而不是把板子自身暴露成 USB 磁盘。它和 [usb_host_hub_dual_camera](../usb_host_hub_dual_camera/README.md) 的区别是：本示例面向 USB Mass Storage 设备，而不是 UVC 摄像头。

## 硬件要求

- LilyGo T5-P4 E-Paper 开发板
- 一个 FAT32 格式的 U 盘
- 正确的 USB OTG 连接线
- 如果要使用 Wi-Fi，需要板载 ESP32-C6 已刷入兼容的 `esp-hosted` slave 固件

ESP32-P4 的 USB Host OTG 引脚为：

- `USB_DM = GPIO49`
- `USB_DP = GPIO50`

## 编译和烧录

```bash
idf.py -C examples/usb_host_msc_example set-target esp32p4
idf.py -C examples/usb_host_msc_example build
idf.py -C examples/usb_host_msc_example -p <PORT> flash monitor
```

## 网页访问

- 默认 AP SSID：`ESP-Host-MSC-Demo`
- 默认密码：空
- 默认地址：`http://192.168.4.1`

网页支持：

- 浏览 U 盘中的文件和目录
- 上传文件到 U 盘
- 从 U 盘下载文件
- 删除 U 盘中的文件
- 把 AP / STA Wi-Fi 配置保存到 NVS

![file_web_page](./.static/file_web.jpg)

## 预期日志

启动后可以看到类似日志：

```text
I (...) app_wifi: Initializing Wi-Fi via esp_wifi_remote/esp_hosted on the onboard ESP32-C6
I (...) app_wifi: wifi_init_softap finished.SSID:ESP-Host-MSC-Demo password:
I (...) usb_host_msc_example: USB Host MSC installed. Waiting for USB storage on USB OTG (GPIO49/GPIO50).
I (...) file_server: Starting HTTP Server
I (...) app_wifi: got ip:192.168.4.1
```

插入 U 盘后可以看到：

```text
I (...) usb_host_msc_example: MSC device connected on USB OTG
I (...) usb_host_msc_example: USB storage mounted at /usb
```

如果启动时没有插 U 盘，网页仍然可以打开，只是会提示当前没有可用磁盘。

## 排查建议

- 网页能打开但无法列目录时，先确认 U 盘已经被识别成 USB MSC 设备，并且文件系统格式受支持。
- Wi-Fi 无法启动时，优先检查板载 ESP32-C6 的 `esp-hosted` 固件和 `menuconfig` 中的 AP / STA 配置。
- USB 枚举不稳定时，优先更换更短的 OTG 线，或改用带独立供电的 USB Hub。
- 如果你需要 exFAT，请先在 ESP-IDF 的 FATFS 配置中手动启用它。
