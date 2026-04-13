# USB Hub 双摄示例（T5-P4 E-Paper）

本示例基于 [usb_host_uvc](https://components.espressif.com/components/espressif/usb_host_uvc)，用于在 T5-P4 上接入外部 **USB UVC** 摄像头（可通过 USB Hub 扩展多路），并通过网页预览 MJPEG 画面。

- 仅支持 MJPEG 预览
- 支持 USB Hub 多摄
- 网页支持保存当前帧

## 边界说明

本示例是 **USB UVC Host** 路线，明确不包含：

- 板载 MIPI 摄像头（`OV2710` / `SC2336` / `OV5645`）
- `SGM38121` 摄像头电源控制流程

如需 `OV2710 + SGM38121`，请使用 `examples/camera_wifi_stream`。

## 硬件要求

- 开发板：LilyGo T5-P4 E-Paper（`esp32p4`）
- 外接 UVC 摄像头（可选外接供电 USB Hub）
- 连接方式：
  - 开发板 USB 输入/供电口接主机供电
  - 开发板 USB OTG 口接 UVC 摄像头或 USB Hub
- T5-P4 使用 Wi-Fi 前提：板载 ESP32-C6 已烧录 `esp-hosted` slave 固件
  - 参考：`docs/esp-hosted-c6-Slave.md`

ESP32-P4 的 USB Host 信号为：`USB_DP=GPIO50`，`USB_DM=GPIO49`。

## 编译与烧录

```bash
idf.py set-target esp32p4
idf.py build
idf.py -p <PORT> flash monitor
```

## 网页访问

- 默认 AP SSID：`ESP-USB-UVC-Demo`
- 默认 AP IP：`192.168.4.1`
- 浏览器访问：`http://192.168.4.1`

Safari 浏览器不建议用于该示例的流媒体页面。

## 前端源码

见 [frontend_source](./frontend_source)。

