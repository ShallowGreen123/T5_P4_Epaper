# HDMI Video Renderer (ESP32-P4 + MIPI DSI + LT8912)

## 功能

- SD 卡挂载点固定为 `/sdcard`（SPI 模式，40 MHz 上限）
- 开机扫描根目录：`test_video.mp4` / `test_video.avi`（区分大小写，优先 mp4）
- 通过 MIPI DSI 输出到 LT8912（I²C 地址 0x2C）并驱动 HDMI 显示器
- 音频轨道不输出（MP4 仅解析视频轨道；AVI 跳过音频 chunk）

## 编译与烧录

1. 安装 PlatformIO
2. 在项目根目录执行：

```bash
pio run -e esp32p4-dev
pio run -e esp32p4-dev -t upload
pio device monitor -e esp32p4-dev
```

## SD 卡准备

- 仅支持 FAT32
- 将视频文件放在 SD 根目录
  - `test_video.mp4` 或 `test_video.avi`

## 视频格式建议

- 当前实现面向 MJPEG 视频帧（容器 MP4/AVI）
- 输出分辨率固定为 1920×1080，RGB888

## 眼图测试方法

- HDMI 眼图与抖动测试在 LT8912 HDMI 输出端进行
- 软件侧保持 DSI lane bit-rate 配置为 1000 Mbps/lane，DPI 像素时钟 148.5 MHz

## 已知问题

- MP4/AVI 仅支持 MJPEG 视频帧；非 MJPEG 视频会报错并回到空闲状态
- SD 卡热插拔通过读写失败判定，恢复播放会从头开始

