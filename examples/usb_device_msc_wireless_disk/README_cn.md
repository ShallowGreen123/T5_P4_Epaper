# USB MSC 无线磁盘（T5-P4 E-Paper）

本示例把 T5-P4 E-Paper 适配成：

- USB MSC 磁盘（在电脑文件管理器中可见）
- Wi-Fi 网页文件管理器（支持上传/下载/删除）

两条通路共用同一份外置 TF 卡 FAT 文件系统。

## 硬件要求

- 开发板：LilyGo T5-P4 E-Paper（`esp32p4`）
- 板载 TF 卡槽已插入可用 TF 卡
- USB 数据线连接开发板 USB 接口
- 板载 ESP32-C6 已烧录 `esp-hosted` slave 固件
  - 参考：`docs/esp-hosted-c6-Slave.md`

## 默认存储与引脚

示例默认使用 T5-P4 的 SDSPI：

- `MISO = GPIO44`
- `SCK  = GPIO45`
- `MOSI = GPIO46`
- `CS   = GPIO47`

挂载路径为 `/disk`。

## 编译

```bash
idf.py set-target esp32p4
idf.py build
```

## 烧录

```bash
idf.py -p <PORT> flash monitor
```

## 运行行为

1. 启动后先挂载 TF 卡 FAT 文件系统。
2. 启动 TinyUSB MSC，对主机导出该磁盘。
3. Wi-Fi 使用 AP/STA 自动回退策略：
   - 配置了 STA 账号时：AP+STA；
   - 未配置 STA 时：仅 AP。
4. 浏览器访问 `http://192.168.4.1` 可进行文件管理。

## Wi-Fi 配置

可通过两种方式配置：

- `menuconfig -> USB MSC Device Demo -> Wi-Fi Settings`
- 网页 `settings` 页面（保存到 NVS 后重启生效）

NVS 键保持与原网页逻辑兼容：
`wifimode`、`apssid`、`appasswd`、`stassid`、`stapasswd`。

## 说明

- 访问 `/reset_msc` 会触发 USB 重新枚举，用于刷新主机侧文件视图。
- 若 hosted Wi-Fi 初始化失败，请优先检查 C6 固件和 SDIO 链路。
- 若 TF 卡挂载失败，请检查卡状态与 SDSPI 引脚配置。
