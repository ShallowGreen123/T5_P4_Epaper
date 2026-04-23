# 即时定格相机 — 实施计划

## 概述

为 `examples/factory` 项目添加"即时定格相机"功能。用户从 Home 屏幕进入 Camera 页面，按下快门按钮后，摄像头抓取一帧画面，经过 ISP 处理和 Bayer 抖动，渲染到电子纸屏幕上，并播放模拟快门音效，模拟拍立得的"咔嚓-显影"体验。

### 核心约束

| 约束 | 说明 |
|---|---|
| 显示介质 | E-Paper，全刷约 2-5 秒，partial ~1s |
| 摄像头接口 | MIPI CSI，通过 ESP32-P4 ISP |
| 传感器支持 | OV2710（默认）、SC2336、OV5645 |
| 图像格式目标 | 1bpp（黑/白）或 3bpp（黑/白/红），1440×720 |
| I2C 总线 | 与触屏/电池/音频共享，SDA=GPIO7 SCL=GPIO8 |

---

## 实施步骤

### Phase 1: 摄像头驱动层

新增两个模块，封装摄像头硬件操作的完整生命周期。

#### 1.1 `factory_camera.h` / `factory_camera.cpp`

```
examples/factory/main/port/factory_camera.h
examples/factory/main/port/factory_camera.cpp
```

**职责：** SGM38121 电源管理、MIPI CSI 视频设备初始化、单帧捕获、资源释放。

**关键函数：**

| 函数 | 说明 |
|---|---|
| `factory_camera_power_on()` | 通过 SGM38121 使能 AVDD1=1.8V、AVDD2=2.8V |
| `factory_camera_power_off()` | 关闭摄像头电源 rails |
| `factory_camera_init()` | 启动 XCLK → `esp_video_init()` → open V4L2 设备 → 协商格式 → mmap 缓冲区 → STREAMON |
| `factory_camera_capture(void *out_buf, size_t buf_size, uint32_t *out_len)` | DQBUF 取一帧 → 拷贝到外部缓冲区 → QBUF 归还 |
| `factory_camera_deinit()` | STREAMOFF → munmap → close → `esp_video_deinit()` → 停止 XCLK |
| `factory_camera_is_detected()` | 探测 SCCB 地址 0x36/0x30/0x3C 确认传感器在线 |

**流程：**

```
factory_camera_power_on()
  → factory_display_get_i2c_bus() 获取共享 I2C
  → i2c_master_bus_add_device() 添加 SGM38121
  → 写寄存器配置 AVDD1/AVDD2 电压
  → 写 ENABLE 寄存器
  → vTaskDelay(20ms) 等待稳定
  → i2c_master_bus_rm_device() 移除 SGM38121 设备（不销毁总线）

factory_camera_init()
  → esp_cam_sensor_xclk_allocate/start (如需要 XCLK)
  → esp_video_init() 初始化 MIPI CSI
  → open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDWR)
  → ioctl VIDIOC_S_FMT 协商为 RGB565 / UYVY
  → ioctl VIDIOC_REQBUFS 申请缓冲区
  → mmap 缓冲区
  → ioctl VIDIOC_STREAMON 启动流

factory_camera_capture(out_buf, buf_size, out_len)
  → ioctl VIDIOC_DQBUF 取出帧
  → memcpy 到 out_buf
  → ioctl VIDIOC_QBUF 归还

factory_camera_deinit()
  → VIDIOC_STREAMOFF
  → munmap + close
  → esp_video_deinit()
  → esp_cam_sensor_xclk_stop/free

factory_camera_power_off()
  → 写 SGM38121 ENABLE=0
```

**参考源：**
- `examples/camera_wifi_stream/main/main.c` — SGM38121 上电、V4L2 流、JPEG 编码
- `examples/camera_id_detect/main/main.c` — 传感器检测、SCCB 通信
- 共享 I2C 总线通过 `factory_display_get_i2c_bus()` 获取

---

#### 1.2 `factory_camera_processor.h` / `factory_camera_processor.cpp`

```
examples/factory/main/port/factory_camera_processor.h
examples/factory/main/port/factory_camera_processor.cpp
```

**职责：** 将摄像头原始帧转换为 e-paper 兼容的 1bpp 帧缓冲。

**处理管线：**

```
RGB565 / UYVY 帧 (e.g. 1280×720)
  → 色彩空间转换 (RGB → Grayscale 8-bit)
  → 缩放 (适配 EPD 分辨率)
  → Bayer 有序抖动 (8-bit → 1bpp)
  → 输出帧缓冲 (1440×720/8 = 129,600 bytes)
```

**核心函数：**

| 函数 | 说明 |
|---|---|
| `factory_camera_process_frame(const void *input, uint32_t width, uint32_t height, uint32_t pixel_format, void *output_1bpp, uint32_t out_width, uint32_t out_height)` | 全处理管线 |
| `factory_camera_convert_to_grayscale(const void *input, uint8_t *gray, uint32_t width, uint32_t height, uint32_t pixel_format)` | 色彩转换 |
| `factory_camera_scale_nearest(const uint8_t *src, uint32_t src_w, uint32_t src_h, uint8_t *dst, uint32_t dst_w, uint32_t dst_h)` | 最近邻缩放 |
| `factory_camera_dither_bayer4(const uint8_t *gray, uint8_t *output_1bpp, uint32_t width, uint32_t height)` | Bayer 抖动 |

**Bayer 抖动矩阵**（复用 `factory_display.cpp` 中的 `kBayer4`）：

```
{ 0,  8,  2, 10}
{12,  4, 14,  6}
{ 3, 11,  1,  9}
{15,  7, 13,  5}
```

对每个像素 (x, y)，比较 grayscale 值与 `kBayer4[y%4][x%4]`，大于则为白（1），否则为黑（0）。

**色彩空间转换：**

- **RGB565 → Gray：** `Y = 0.299*R + 0.587*G + 0.114*B`，用整数近似实现
- **UYVY → Gray：** 提取 Y 分量即可（UYVY 每个像素有一个 Y 值）

**缩放策略：**

- 如果摄像头输出是 1280×720，无需缩放（与 EPD 宽高比一致）
- 如果摄像头输出是其他分辨率，使用最近邻插值
- 未来可以考虑利用 ISP 硬件 scaler 在 V4L2 驱动层完成缩放

**内存占用：**

| 用途 | 大小 | 位置 |
|---|---|---|
| 原始帧缓冲区 (1280×720 RGB565) | ~1.8 MB | PSRAM |
| 灰度中间缓冲 (1280×720) | ~0.9 MB | PSRAM |
| 1bpp 输出 (1440×720/8) | ~130 KB | PSRAM |
| JPEG 编码输出 (SD 保存用) | ~512 KB | PSRAM |

---

### Phase 2: 修改已有文件

#### 2.1 `factory_types.h` — 新增页面枚举

在 `factory_page_id_t` 中新增入口：

```diff
  FACTORY_PAGE_AUDIO,
+ FACTORY_PAGE_CAMERA,
```

#### 2.2 `main/CMakeLists.txt` — 添加组件依赖

```diff
  REQUIRES
      ...
+     esp_cam_sensor
+     esp_video
+     esp_sccb_i2c
```

注意：SGM38121 控制直接通过 I2C 写寄存器完成，不需要独立组件。

#### 2.3 `ui_screens.h` — 声明生命周期函数

```c
scr_lifecycle_t *factory_screen_camera_lifecycle(void);
```

#### 2.4 `ui.cpp` — 注册页面

在 `factory_ui_init()` 中注册：

```c
scr_mgr_register(FACTORY_PAGE_CAMERA, factory_screen_camera_lifecycle());
```

#### 2.5 `screen_home.cpp` — 添加菜单入口

在 `kMenuItems[]` 中新增：

```c
{LV_SYMBOL_IMAGE, "Camera", FACTORY_PAGE_CAMERA, 0},
```

使用 `LV_SYMBOL_IMAGE` 作为相机图标（LVGL 内置符号），未来可替换为自定义位图。

---

### Phase 3: 相机 UI 页面

#### 3.1 `screen_camera.cpp`

```
examples/factory/main/ui/screen_camera.cpp
```

遵循与 `screen_audio.cpp` 相同生命周期模式：`screen_lifecycle_t` → `create` / `entry` / `exit` / `destroy`

**UI 布局（1440×720）：**

```
┌──────────────────────────────────────────┐
│  ← Back         Camera             快门 │  ← 状态栏
├──────────────────────────────────────────┤
│                                          │
│             照片预览区域                   │
│       (上次拍摄的 1bpp 抖动结果)           │
│   全屏显示，保持宽高比，居中排列            │
│                                          │
│                                          │
├──────────────────────────────────────────┤
│  [Capture]  [Save to SD]  [Auto Mode]    │  ← 操作按钮行
│  状态: "Ready | Capturing... | Done ✓"    │
└──────────────────────────────────────────┘
```

**页面状态机：**

```
         ┌──────────────────────────────────────┐
         │                                      │
         v                                      │
    ┌─────────┐  点 Capture   ┌──────────┐      │
    │  IDLE   │ ────────────→ │CAPTURING │      │
    │         │               │ (抓帧+    │      │
    │ Ready   │               │  处理+    │      │
    │         │               │  渲染EPD) │      │
    └─────────┘               └──────────┘      │
         ▲                       │              │
         │                       │ 完成          │
         │              ┌────────v───┐          │
         │              │  SHOWING   ├──────────┘
         │              │  Done      │  点 Capture
         └──────────────┴────────────┘
```

**定时刷新：**

使用 `lv_timer`（350ms 间隔），类似 `screen_audio.cpp` 的 `refresh_timer_cb`：
- 定时器回调轮询摄像头捕获状态
- 更新状态文本（"Capturing frame...", "Processing...", "Rendering to EPD...", "Done ✓"）
- 捕获和渲染不在 LVGL 任务中同步执行，避免阻塞 UI

**后台处理策略：**

1. 点击 Capture → 禁用按钮（防止重复点击）
2. 创建一次性 FreeRTOS 任务或使用信号量：
   - 执行 `factory_camera_power_on()`
   - 执行 `factory_camera_init()`
   - 调用 `factory_camera_capture()`
   - 调用 `factory_camera_process_frame()`
   - 执行 `factory_camera_deinit()`
   - 执行 `factory_camera_power_off()`
   - 将结果 1bpp 数据写入 e-paper framebuffer（通过 `factory_display` API）
   - 触发 e-paper 刷新
   - 置完成标志
3. 定时器检测到完成标志 → 更新 UI → 重新启用按钮

**操作按钮：**

| 按钮 | 功能 |
|---|---|
| **Capture** | 触发拍照流程（上电 → 抓帧 → 处理 → 渲染 → 断电） |
| **Save to SD** | 将当前帧以 JPEG 格式保存到 SD 卡（需要 SD 已挂载） |
| **Auto Mode** | 可选：定时自动拍照模式（每 N 分钟拍一张） |

**快门音效：**

- 在 Capture 按钮按下时，通过 `factory_audio` 播放一段短促的 PCM 样本
- 或者使用 ES8311 DAC 输出一个方波脉冲模拟快门声
- 如果音频资源被占用，可以跳过此功能

**EPD 渲染策略：**

1. 先使用 partial refresh 快速显示照片（~1s）
2. 如果开启 full quality 模式，再执行一次 full refresh 清除残影（~3s）
3. 使用 `factory_display` 现有的 API 操作 framebuffer

---

### Phase 4: 配置与编译

#### 4.1 sdkconfig 调整

需要 enable 的关键配置项：

```
CONFIG_CAMERA_OV2710=y
CONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE=y
CONFIG_ESP_VIDEO_ENABLE_ISP=y
```

#### 4.2 Kconfig.projbuild（可选）

新增摄像头相关菜单选项：

```
menu "Factory Demo Camera Configuration"
    config FACTORY_CAMERA_SENSOR
        int "Camera sensor selection (0=OV2710, 1=SC2336, 2=OV5645)"
        default 0

    config FACTORY_CAMERA_CAPTURE_WIDTH
        int "Capture width"
        default 1280

    config FACTORY_CAMERA_CAPTURE_HEIGHT
        int "Capture height"
        default 720

    config FACTORY_CAMERA_PLAY_SHUTTER_SOUND
        bool "Play shutter sound via ES8311"
        default y
endmenu
```

---

### 文件清单

| 操作 | 文件 | 说明 |
|---|---|---|
| **新增** | `main/port/factory_camera.h` | 摄像头驱动头文件 |
| **新增** | `main/port/factory_camera.cpp` | 摄像头驱动实现 |
| **新增** | `main/port/factory_camera_processor.h` | 图像处理头文件 |
| **新增** | `main/port/factory_camera_processor.cpp` | 图像处理实现 |
| **新增** | `main/ui/screen_camera.cpp` | 相机 UI 页面 |
| **修改** | `main/port/factory_types.h` | 添加 FACTORY_PAGE_CAMERA 枚举 |
| **修改** | `main/CMakeLists.txt` | 添加摄像头组件依赖 |
| **修改** | `main/ui/ui_screens.h` | 声明生命周期函数 |
| **修改** | `main/ui/ui.cpp` | 注册相机页面 |
| **修改** | `main/ui/screen_home.cpp` | 添加菜单入口 |
| **修改** | `sdkconfig` | enable 摄像头/ISP/视频配置 |

---

### 实施顺序（推荐）

| 步骤 | 内容 | 预计文件数 |
|---|---|---|
| 1 | `factory_camera.h/.cpp` — 电源 + 初始化 + 抓帧 | 2 |
| 2 | `factory_camera_processor.h/.cpp` — 灰度 + 缩放 + 抖动 | 2 |
| 3 | 修改 `factory_types.h` + `CMakeLists.txt` | 2 |
| 4 | `screen_camera.cpp` — UI 页面 + 状态机 | 1 |
| 5 | 修改 `ui_screens.h` + `ui.cpp` + `screen_home.cpp` | 3 |
| 6 | 更新 `sdkconfig`，编译调试 | 1+ |

---

### 风险和注意事项

1. **共享 I2C 总线：** 摄像头 SGM38121 和 SCCB 需要添加到已有 I2C 总线。操作完成后应移除设备（而非销毁总线），避免影响触屏/电池/音频。

2. **MIPI CSI 资源冲突：** 确认 e-paper 使用 SPI 接口而非 MIPI DSI，避免与 MIPI CSI 共享硬件资源。

3. **初始化延迟：** `esp_video_init()` 和摄像头上电可能耗时 >500ms。在 entry 阶段初始化，exit 阶段反初始化，避免影响其他页面。

4. **内存压力：** 摄像头需要 ~4MB PSRAM 用于 V4L2 缓冲区。确保 PSRAM 已初始化且足够（通常 8MB 以上）。

5. **EPD 刷新时间：** 全刷约 2-5 秒，partial 约 1 秒。"咔嚓→显影"总体验约 3-8 秒，符合拍立得的慢速预期。

6. **XCLK 引脚：** 如果传感器需要外部时钟，需确认 XCLK GPIO 引脚号并在 board_config.h 中定义。

7. **音频资源竞争：** 如果音频页面正在播放，快门音效可能需要等待或跳过。建议先检查 `factory_audio_is_ready()`。

---

### 用户体验设计

**预期交互流程：**

```
1. 用户在 Home 点击 "Camera" 图标
2. 进入相机页面，看到 "Ready" 状态和上次照片（首次为空白）
3. 用户点击 [Capture] 按钮
4. 听到 "咔嚓" 快门声
5. 状态变为 "Capturing..." (抓帧中)
6. 状态变为 "Processing..." (抖动转换中)
7. 状态变为 "Rendering..." (EPD 刷新中)
8. ~3-5秒后，照片在电子纸上显现
9. 状态变为 "Done ✓"
10. 用户可再次拍照或返回 Home
```

**关键体验特性：**
- 按钮点击后立即禁用，防止重复触发
- 状态文本实时更新，让用户知道进度
- 快门音效提供即时听觉反馈（摄像头没有机械快门）
- EPD 刷新期间显示等待状态，避免用户困惑

---

### 未来扩展

- **3bpp (黑/白/红) 模式：** 支持三色 e-paper 渲染，红色突出显示特定区域
- **连拍模式：** 连续拍摄多张，选择最佳的一张
- **滤镜效果：** 在灰度转换前应用边缘检测或反色等效果
- **WiFi 分享：** 将照片通过 WiFi 发送到手机
- **定时拍照：** 设定延时（3s/5s/10s）后自动拍摄
- **照片画廊：** SD 卡上的照片浏览和重新显示
