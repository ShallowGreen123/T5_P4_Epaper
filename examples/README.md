# Examples 示例文档

这是 LILYGO T5-P4 E-Paper 开发板的 ESP-IDF 示例集合。所有示例都基于 **ESP-IDF 官方框架** 编译和运行。

## 📋 编译环境

需要一个 esp-idf 环境，该项目所有示例都是基于 esp-idf v5.4.3 开发，所以推荐 esp-idf 版本为 v5.4.3 及以上。

- [esp-idf 安装指南](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32/get-started/index.html#installation)

- ESP-IDF v5.x（推荐 v5.4.3 及以上）
- 主要芯片: ESP32-P4

## 🔧 通用编译步骤

### 方式 1：命令行（推荐）

```bash
# 切换到示例目录
cd examples/<example_name>

# 设置目标芯片（若未设置过）
idf.py set-target esp32p4

# 配置项目（可选，访问菜单配置）
idf.py menuconfig

# 编译
idf.py build

# 烧录（需指定串口）
idf.py -p COM3 flash monitor  # Windows
idf.py -p /dev/ttyUSB0 flash monitor  # Linux

# 仅监控串口
idf.py monitor -p COM3
```

### 方式 2：VSCode + IDF 扩展

1. 安装 `ESP-IDF` 扩展
2. 在命令面板选择 `ESP-IDF: Build`
3. 自动检测 CMakeLists.txt 并编译

---

## 🎯 示例分类

### 1️⃣ 基础驱动示例

#### **pca9535** - IO 扩展芯片
- **功能**: 读取 PCA9535 GPIO 扩展器的状态
- **依赖库**: esp-idf 内置 I2C 驱动
- **硬件**: I2C 总线（GPIO7: SDA, GPIO8: SCL）
- **用途**: IO 扩展、按键检测、LED 控制
- **编译**: `idf.py build` (自动默认 esp32p4)
- **注意**: 需要确认 I2C 硬件连接正常，推荐先从此示例开始验证基础通信

#### **i2c_tools** - I2C 工具集
- **功能**: 提供命令行工具进行 I2C 设备调试
- **依赖库**: 
  - ESP-IDF console 组件
  - I2C 驱动
- **工具**:
  - `i2cconfig`: 配置 I2C 总线（GPIO、频率）
  - `i2cdetect`: 扫描设备地址
  - `i2cget`: 读取寄存器
  - `i2cset`: 写入寄存器
  - `i2cdump`: 查看寄存器内容
- **编译**: `idf.py build`
- **使用**: 通过串口输入命令进行交互式调试
- **注意**: 内部默认 I2C 引脚可通过 menuconfig 修改

#### **sgm38121** - 电源管理芯片测试
- **功能**: 测试 SGM38121 电源芯片功能
- **依赖库**: esp-idf I2C、GPIO 驱动
- **用途**: 电源轨切换、摄像头供电控制
- **编译**: `idf.py build`
- **注意**: 此示例用于验证电源芯片响应，不实际供电

### 2️⃣ 存储示例

#### **sd_card_test** - SD 卡测试
- **功能**: SD 卡识别、容量获取、文件读写校验
- **依赖库**: 
  - esp-idf SDMMC 驱动
  - VFS（虚拟文件系统）
- **用途**: 验证 SD 卡硬件和读写性能
- **编译**: `idf.py build`
- **注意**: 
  - 需要插入有效的 microSD 卡
  - 支持 FAT32/exFAT 格式

#### **es8311_spiffs** - SPIFFS 文件系统 + 音频芯片
- **功能**: 使用 SPIFFS 文件系统存储数据
- **依赖库**: 
  - ESP-IDF SPIFFS 驱动
  - ES8311 音频编解码器驱动
- **用途**: 板载闪存分区管理、音频数据存储
- **编译**: `idf.py build`
- **注意**: 
  - SPIFFS 用于小文件、高频访问场景
  - 需要预留足够的 Flash 空间

### 3️⃣ 音频示例

#### **es8311_mic_speak** - 麦克风和扬声器
- **功能**: 实时录音播放循环测试
- **依赖库**: 
  - ESP-IDF I2S 驱动
  - ES8311 音频编解码器驱动
- **用途**: 验证音频输入输出通路
- **编译**: `idf.py build`
- **注意**: 
  - 需要连接麦克风和扬声器
  - I2S 总线配置在代码中

### 4️⃣ 显示示例

#### **fastEPD_lvgl_demo** - E-Ink 显示 + LVGL UI
- **功能**: 演示 LVGL 在 E-Ink 屏幕上的渲染
- **依赖库**:
  - `components/fastepd`: 快速电子墨水屏驱动
  - `components/lvgl`: LVGL 8.x UI 库
- **用途**: E-Ink 显示效果演示、文字图形渲染
- **编译**: `idf.py build`
- **注意**: 
  - 刷新速度受限于 E-Ink 特性（通常 1-2 秒）
  - 建议用于低频更新场景（电子书、仪表板）

#### **hdmi_video_renderer** - HDMI 视频输出（基础）
- **功能**: 原始 HDMI 视频输出演示
- **依赖库**: 
  - ESP-IDF MIPI-DSI 驱动
  - LT8912B HDMI 转换桥驱动
- **分辨率**: 800x600@60Hz, RGB888
- **编译**: `idf.py build`
- **注意**: 
  - 需要连接 HDMI 显示器
  - 为专用演示，不挂载 SD 卡

#### **hdmi_video_renderer_lvgl** - HDMI + LVGL Demo
- **功能**: 运行 LVGL Demo 并输出到 HDMI
- **依赖库**:
  - `components/lvgl`: LVGL 8.4 库
  - ESP-IDF MIPI-DSI、LT8912B 驱动
- **内置 Demo**:
  - Benchmark（性能测试）
  - Stress（压力测试）
  - Widgets（UI 组件展示，支持幻灯片）
- **编译**: 
  ```bash
  idf.py -C examples/hdmi_video_renderer_lvgl set-target esp32p4
  idf.py -C examples/hdmi_video_renderer_lvgl build
  ```
- **选择 Demo**: `idf.py -C examples/hdmi_video_renderer_lvgl menuconfig` → HDMI LVGL Demo Configuration
- **注意**: 
  - 连接 HDMI 显示器即可自动启动选定的 Demo
  - Widgets Demo 支持幻灯片（无需交互设备）

### 5️⃣ 摄像头示例

#### **camera_id_detect** - 摄像头型号识别
- **功能**: 识别连接的 MIPI 摄像头型号
- **支持型号**: SC2336, OV2710, OV5645
- **依赖库**:
  - `components/sensorlib`: Espressif 摄像头传感器库
  - SGM38121 驱动（供电管理）
- **编译**: `idf.py build`
- **输出示例**:
  ```
  I (1234) camera_id: Camera match: SC2336 addr=0x30 pid=0xCB3A
  I (1235) camera_id: Detected camera model: SC2336
  ```
- **注意**: 
  - 仅识别，不进行视频流处理
  - 需要 SGM38121 正确配置摄像头供电
  - I2C 地址: SGM38121@0x28

#### **camera_wifi_stream** - MIPI 摄像头 WiFi 直播
- **功能**: 通过 WiFi 实时输出摄像头 MJPEG 流
- **依赖库**:
  - ESP-IDF MIPI-CSI 驱动
  - 摄像头传感器驱动
  - WiFi 通过 ESP32-C6 Hosted 方式
- **端点**:
  - `GET /` - 网页实时预览
  - `GET /stream` - MJPEG 多部分流（用于 VLC 等播放器）
- **编译**: `idf.py build`
- **配置**: 
  ```
  idf.py menuconfig → Camera WiFi Stream Configuration
  ```
  设置 WiFi SSID 和密码
- **优化参数**:
  - JPEG 质量: 88（推荐）
  - 帧率限制: 12 fps（以 WiFi 带宽为准）
  - 采集缓冲: 4 个
- **注意**:
  - 焦距调整: 旋转镜头调焦环以获得清晰画面
  - 若画面模糊，先调焦距，再调 JPEG 质量
  - 若卡顿，降低帧率限制
  - SGM38121 默认 DVDD1/DVDD2 禁用（0 mV），非必需无需打开

#### **usb_host_hub_dual_camera** - USB UVC 摄像头（多摄像头）
- **功能**: 支持外接 USB UVC 摄像头，通过 USB Hub 接多个
- **依赖库**:
  - `usb_host_uvc`: Espressif USB UVC Host 组件
  - WiFi（通过 ESP32-C6 Hosted）
- **特性**:
  - MJPEG 预览
  - USB Hub 多摄像头支持
  - 网页保存当前帧
- **前置要求**:
  - ESP32-C6 已烧录 esp-hosted Slave 固件（见 `docs/esp-hosted-c6-Slave.md`）
- **硬件连接**:
  - USB 电源: 接板载 USB 输入口
  - USB OTG: 接摄像头或 USB Hub
- **编译**: 
  ```bash
  idf.py set-target esp32p4
  idf.py build
  ```
- **网页访问**: `http://192.168.4.1`（默认 AP）
- **注意**:
  - 不适用于板载 MIPI 摄像头（MIPI 用 `camera_wifi_stream`）
  - USB Hub 需供电
  - Safari 浏览器不推荐（兼容性问题）

#### **usb_host_msc_example** - USB Host U 盘文件管理
- **功能**: 通过 USB OTG 挂载外接 U 盘，并通过网页进行浏览、上传、下载和删除
- **依赖**:
  - `components/esp_msc_ota`: vendored USB Host MSC 组件
  - WiFi（通过板载 ESP32-C6 `esp-hosted`）
- **前置要求**:
  - 板载 ESP32-C6 已刷 `esp-hosted` slave 固件
  - USB OTG 口连接 U 盘或带供电的 USB Hub
- **默认访问**:
  - AP SSID: `ESP-Host-MSC-Demo`
  - URL: `http://192.168.4.1`
- **区别说明**:
  - 与 `usb_device_msc_wireless_disk` 不同：本示例是 Host 访问外接 U 盘
  - 与 `usb_host_hub_dual_camera` 不同：本示例面向 MSC 存储设备，不是 UVC 摄像头

### 6️⃣ WiFi 和网络示例

#### **c6_wifi_scan** - WiFi 扫描（通过 ESP32-C6）
- **功能**: 通过板载 ESP32-C6 扫描周围 WiFi 网络
- **依赖库**: WiFi 驱动、ESP-IDF 基础库
- **前置要求**: ESP32-C6 已烧录 `esp-hosted` Slave 固件
- **编译**: `idf.py build`（自动支持多芯片）
- **注意**: 仅进行扫描，不进行连接

### 7️⃣ USB 设备示例

#### **usb_device_msc_wireless_disk** - USB 大容量存储（无线磁盘）
- **功能**: 将 SD 卡 或 SPIFFS 作为 USB 磁盘公开给主机
- **依赖库**: 
  - ESP-IDF USB Device 驱动
  - SD 卡驱动
  - WiFi（可选）
- **编译**: `idf.py set-target esp32p4` (如需) 或 自动检测
- **用途**: 无线存储、文件传输
- **注意**: 连接 USB 主机（PC）

#### **usb_device_uac** - USB 音频设备（UAC）
- **功能**: 将 ESP32-P4 模拟为 USB 音频设备
- **依赖库**:
  - ESP-IDF USB Device UAC 驱动
  - ES8311 音频编解码器驱动
- **用途**: USB 摄像头/麦克风直播
- **编译**: `idf.py build`
- **注意**: 连接 USB 主机识别为音频设备

### 8️⃣ 集成应用示例

#### **bq_power_dashboard** - 电源管理仪表板
- **功能**: 集合 BQ25896（充电管理）+ BQ27220（电量计）显示仪表板
- **依赖库**:
  - `components/bq25896`: 充电芯片驱动
  - `components/bq27220`: 电量计芯片驱动
  - `components/fastepd`: E-Ink 显示
  - `components/lvgl`: UI 框架
- **用途**: 电池管理、充电状态监控
- **编译**: `idf.py build`
- **注意**: 需要完整的电源管理硬件

#### **epub_reader_t5_p4** - 电子书阅读器
- **功能**: EPUB 电子书阅读应用（基于 T5-P4 硬件）
- **依赖库**:
  - `components/fastepd`: E-Ink 显示驱动
  - `components/sensorlib`: 传感器库（环境传感）
- **用途**: 电子书渲染、翻页控制
- **编译**: `idf.py build`
- **注意**: 集成应用，功能复杂

#### **factory** - 工厂测试固件
- **功能**: 工厂生产测试、硬件功能验证
- **依赖库**: 
  - `components/fastepd`: E-Ink 显示
  - `components/lvgl`: UI 显示
  - `components/bq25896`: 充电管理
  - `components/bq27220`: 电量计
- **编译**: `idf.py build`
- **用途**: 生产线自动化测试
- **注意**: 包含多个外设的综合测试

### 9️⃣ 工具和配置

#### **c6_wifi_scan** 
参见 **WiFi 和网络示例** 部分

---

## ⚠️ 常见问题与注意事项

### 前置要求

- [ ] **ESP-IDF 安装**: 推荐 v5.4.3，确保 `IDF_PATH` 环境变量已设置
- [ ] **Python 依赖**: `pip install -r <idf_path>/requirements.txt`
- [ ] **串口驱动**: CH340 驱动（Windows 需手动安装）
- [ ] **权限**: Linux/Mac 需 `sudo` 或加入 dialout 组


### 常见错误排查

| 错误 | 原因 | 解决方案 |
|------|------|--------|
| `IDF_PATH not found` | 环境变量未设置 | 重新安装 IDF Tools 或手动设置 `IDF_PATH` |
| `CMake version too old` | CMake 版本 < 3.16 | `pip install cmake --upgrade` |
| `Port COM* not available` | 串口不存在或被占用 | 检查 USB 连接，查看设备管理器 |
| `sdkconfig mismatch` | 配置不匹配 | `idf.py fullclean` 后重新编译 |

### 性能与优化

- **编译加速**: 使用 `-j` 参数并行编译：`idf.py build -j8`
- **闪存优化**: 启用 LTO (Link Time Optimization)：menuconfig → Compiler options
- **内存优化**: 关闭不需要的组件，减少 RAM 占用

### 硬件连接验证

1. **I2C 通信**: 先运行 `i2c_tools` 或 `pca9535` 验证
2. **SD 卡**: 运行 `sd_card_test` 检查识别和读写
3. **音频**: 运行 `es8311_mic_speak` 验证音频通路
4. **显示**: 先测试 E-Ink（`fastEPD_lvgl_demo`）或 HDMI（`hdmi_video_renderer`）
5. **摄像头**: 先运行 `camera_id_detect` 识别，再运行 `camera_wifi_stream`

---

## 📚 扩展资源

- **官方文档**: https://docs.espressif.com/projects/esp-idf/
- **项目文档**: 
  - `../docs/esp-hosted-c6-Slave.md` - ESP32-C6 WiFi 从机配置
  - `../docs/pinmap.md` - 硬件引脚定义
  - `../docs/camera-sensor-power.md` - 摄像头供电时序
- **LilyGo 官方**: https://github.com/Xinyuan-LilyGO/

---

## 📝 示例速查表

| 功能 | 示例名称 | 难度 | 依赖硬件 |
|------|--------|------|--------|
| USB Host U 盘 | `usb_host_msc_example` | ⭐⭐ | USB U 盘 + WiFi 网页文件管理 |
| 基础 I2C 通信验证 | `pca9535` / `i2c_tools` | ⭐ | I2C 设备 |
| 电源管理 | `sgm38121` / `bq_power_dashboard` | ⭐⭐ | BQ 芯片 |
| 存储（SD/Flash） | `sd_card_test` / `es8311_spiffs` | ⭐ | SD 卡 或 Flash |
| 音频 I/O | `es8311_mic_speak` / `usb_device_uac` | ⭐⭐ | 麦克风 + 扬声器 |
| E-Ink 显示 | `fastEPD_lvgl_demo` / `epub_reader_t5_p4` | ⭐⭐ | E-Ink 屏幕 |
| HDMI 显示 | `hdmi_video_renderer` / `hdmi_video_renderer_lvgl` | ⭐⭐ | HDMI 显示器 |
| MIPI 摄像头 | `camera_id_detect` / `camera_wifi_stream` | ⭐⭐⭐ | MIPI 摄像头 + 网络 |
| USB UVC 摄像头 | `usb_host_hub_dual_camera` | ⭐⭐⭐ | USB 摄像头 + Hub |
| WiFi 扫描 | `c6_wifi_scan` | ⭐⭐ | ESP32-C6 固件 |
| 综合应用 | `factory` / `bq_power_dashboard` / `epub_reader_t5_p4` | ⭐⭐⭐⭐ | 多个硬件 |

---

**更新时间**: 2026年4月25日 | **项目**: LILYGO T5-P4 E-Paper Examples










