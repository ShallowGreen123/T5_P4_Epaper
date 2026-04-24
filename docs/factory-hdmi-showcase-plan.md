# Factory HDMI Showcase 实施计划

## 目标

在 `examples/factory` 中新增一个 `HDMI` 页面，把电子纸作为控制台，把 HDMI 作为动态展示屏。

第一版先完成可稳定产测的 HDMI 核心能力：

- 检测 HDMI / LT8912B 是否可用
- 输出测试图案
- 输出动态画面证明链路在持续刷新
- 在电子纸上显示状态、帧率、错误信息和控制按钮
- 离开页面或点击 Stop 后安全关闭 HDMI 输出

后续再把 Camera、Audio、SD Video 接入为 Showcase 子模式。

## 用户体验

Home 页面新增 `HDMI` tile。

进入 `HDMI Showcase` 后：

- 电子纸显示控制台：
  - Power
  - Bridge
  - Mode
  - FPS
  - Frame count
  - Free PSRAM
  - Last error
- HDMI 显示动态画面：
  - 彩条
  - 灰阶阶梯
  - 对焦网格
  - 移动方块 / 扫描线
  - 左上角状态文字：`T5-P4 HDMI Showcase`

页面按钮：

- `Start`
- `Pattern`
- `Motion`
- `Stop`

## 功能分期

### Phase 1: HDMI 硬件产测

目标：先保证 HDMI 链路能稳定点亮。

实现内容：

- 新增 factory HDMI 页面入口
- 初始化 HDMI 电源 rails
- 初始化 LT8912B
- 输出 800x600 RGB888 测试图案
- 在电子纸显示 ready / not ready
- 支持 Stop 关闭 HDMI 输出

验收标准：

- 不插 HDMI 时页面不卡死
- 插入 HDMI 后可以显示测试图案
- 电子纸 UI 仍可返回 Home
- Stop 后 HDMI 电源关闭

### Phase 2: 动态 Showcase

目标：让 HDMI 输出不只是静态测试图。

实现内容：

- 独立 FreeRTOS task 绘制 HDMI frame
- 双 RGB888 framebuffer
- 使用 `esp_lcd_panel_draw_bitmap()` 提交整帧
- 注册 `on_color_trans_done` callback
- semaphore 等待刷新完成
- 统计 FPS、frame count、flush timeout

动态画面：

- 移动矩形
- 扫描线
- 色条
- 灰阶阶梯
- 对焦网格

验收标准：

- 连续运行 10 分钟无明显内存下降
- 目标帧率 20-30 FPS
- HDMI 画面持续运动
- 切换 Pattern / Motion 不重启页面

### Phase 3: Camera Preview

目标：HDMI 显示摄像头实时预览，电子纸保留拍立得结果。

实现内容：

- 复用 `factory_camera` 初始化和抓帧能力
- HDMI 显示 live preview
- 电子纸页面提供 Capture 按钮
- Capture 后电子纸显示 dither 后的黑白/灰阶照片
- HDMI 可显示 live color 与 e-paper preview 对比

验收标准：

- 摄像头预览不会破坏电子纸 UI
- 退出 HDMI 页面后摄像头资源释放
- Camera 原页面仍可正常使用

### Phase 4: Audio Visualizer

目标：HDMI 显示麦克风波形/频谱，电子纸显示音频测试结果。

实现内容：

- 复用 `factory_audio` 监测任务或拆出共享采样接口
- HDMI 绘制实时 waveform
- HDMI 绘制 peak / noise / clip 指标
- 电子纸显示 Mic / Speaker / Loopback PASS 状态

验收标准：

- 音频页面和 HDMI Audio 模式不互相抢占 I2S
- Stop 后音频采样停止
- 安全音量限制仍生效

### Phase 5: SD Video

目标：播放 `/sdcard/factory.mp4` 作为展会/工厂演示素材。

实现内容：

- 复用 `examples/hdmi_video_renderer` 的 `app_stream_adapter`
- SD 卡挂载后查找 `/sdcard/factory.mp4`
- HDMI 播放视频
- 电子纸提供 Play / Pause / Stop / Loop 状态

验收标准：

- 文件不存在时显示明确错误，不阻塞页面
- 视频分辨率和 HDMI 输出分辨率不匹配时提示用户
- Stop 后释放 decoder / SD / framebuffer 相关资源

## 新增文件

### `examples/factory/main/port/factory_hdmi.h`

职责：

- 对 UI 暴露 HDMI 初始化、模式切换、状态查询 API
- 不包含 LVGL UI 逻辑

建议 API：

```cpp
bool factory_hdmi_init(void);
void factory_hdmi_deinit(void);
bool factory_hdmi_start(factory_hdmi_mode_t mode);
void factory_hdmi_stop(void);
void factory_hdmi_set_mode(factory_hdmi_mode_t mode);
const factory_hdmi_state_t *factory_hdmi_get_state(void);
```

### `examples/factory/main/port/factory_hdmi.cpp`

职责：

- HDMI 电源时序
- LT8912B 初始化
- framebuffer 分配和释放
- HDMI render task
- 测试图案绘制
- 状态统计

### `examples/factory/main/ui/screen_hdmi.cpp`

职责：

- 电子纸上的 HDMI Showcase 控制台
- Start / Pattern / Motion / Stop 按钮
- 定时刷新状态信息
- 页面销毁时停止 HDMI

### 可选：`examples/factory/main/port/factory_io_expander.h/.cpp`

建议新增共享 PCA9535 管理层，避免 touch/audio/HDMI 各自维护 PCA9535 句柄。

职责：

- 初始化 PCA9535
- 设置单个 pin 电平
- 读取单个 pin 电平
- 统一管理输出方向和默认电平

建议 API：

```cpp
bool factory_io_expander_init(void);
esp_err_t factory_io_expander_set_pin(uint8_t pin, bool high);
esp_err_t factory_io_expander_get_pin(uint8_t pin, bool *high);
```

## 修改文件

### `examples/factory/main/port/factory_types.h`

新增页面 ID：

```cpp
FACTORY_PAGE_HDMI,
```

新增 HDMI 模式：

```cpp
typedef enum {
    FACTORY_HDMI_MODE_PATTERN,
    FACTORY_HDMI_MODE_MOTION,
    FACTORY_HDMI_MODE_CAMERA,
    FACTORY_HDMI_MODE_AUDIO,
    FACTORY_HDMI_MODE_SD_VIDEO,
} factory_hdmi_mode_t;
```

新增状态结构：

```cpp
typedef struct {
    bool initialized;
    bool powered;
    bool ready;
    bool running;
    factory_hdmi_mode_t mode;
    uint16_t width;
    uint16_t height;
    uint32_t frame_count;
    uint32_t fps;
    uint32_t free_psram;
    const char *status_text;
    const char *last_error;
} factory_hdmi_state_t;
```

### `examples/factory/main/ui/ui_screens.h`

新增：

```cpp
scr_lifecycle_t *factory_screen_hdmi_lifecycle(void);
```

### `examples/factory/main/ui/ui.cpp`

注册页面：

```cpp
scr_mgr_register(FACTORY_PAGE_HDMI, factory_screen_hdmi_lifecycle());
```

### `examples/factory/main/ui/screen_home.cpp`

新增菜单项：

```cpp
{LV_SYMBOL_VIDEO, "HDMI", FACTORY_PAGE_HDMI, 0},
```

如果当前 LVGL symbol 没有合适的视频图标，可以先用 `LV_SYMBOL_IMAGE`。

### `examples/factory/main/port/factory_port.cpp`

新增页面描述：

```cpp
{
    FACTORY_PAGE_HDMI,
    "HDMI",
    "LT8912B HDMI bridge showcase and diagnostics.",
    "The factory HDMI page drives an external monitor with RGB888 test patterns, motion diagnostics, and later camera/audio/SD showcase modes.",
    "LIVE_HDMI"
},
```

### `examples/factory/main/idf_component.yml`

新增依赖：

```yaml
espressif/esp_lcd_lt8912b:
  version: ">=0.1.3,<1.0.0"
```

### `examples/factory/main/Kconfig.projbuild`

新增菜单：

```kconfig
menu "HDMI"

config FACTORY_HDMI_ENABLE
    bool "Enable HDMI Showcase page"
    default y

config FACTORY_HDMI_TARGET_FPS
    int "HDMI target FPS"
    depends on FACTORY_HDMI_ENABLE
    default 30
    range 1 60

config FACTORY_HDMI_TASK_STACK_SIZE
    int "HDMI render task stack size"
    depends on FACTORY_HDMI_ENABLE
    default 6144
    range 4096 16384

endmenu
```

### `examples/factory/sdkconfig.defaults`

建议新增：

```ini
CONFIG_BSP_LCD_DPI_BUFFER_NUMS=2
CONFIG_BSP_LCD_COLOR_FORMAT_RGB888=y
CONFIG_BSP_LCD_TYPE_HDMI=y
CONFIG_BSP_LCD_HDMI_800x600_60HZ=y
```

第一版固定 800x600，稳定后再开放 1024x768、1280x720、1280x800、1920x1080。

## HDMI 初始化策略

不要直接在 UI 层初始化 HDMI。

推荐流程：

1. UI 调用 `factory_hdmi_start(mode)`
2. `factory_hdmi` 获取共享 I2C bus：`factory_display_get_i2c_bus()`
3. 通过 PCA9535 打开 HDMI 相关电源：
   - `FACTORY_PCA_1V8_EN`
   - `FACTORY_PCA_HDMI_EN`
   - `FACTORY_PCA_HDMI_RST`
4. 初始化 MIPI DSI bus
5. 创建 LT8912B panel IO：
   - main address
   - CEC address
   - AVI address
6. 创建 LT8912B panel
7. `esp_lcd_panel_reset()`
8. `esp_lcd_panel_init()`
9. 多次查询 `esp_lcd_panel_lt8912b_is_ready()`
10. 分配 HDMI framebuffer
11. 启动 render task

## 渲染策略

第一版不把 HDMI 注册成第二个 LVGL display，避免和电子纸 LVGL display 互相影响。

推荐直接使用：

```cpp
esp_lcd_panel_draw_bitmap(panel, 0, 0, width, height, buffer);
```

buffer 格式：

- RGB888
- 800 x 600 x 3 bytes
- 单帧约 1.44 MB
- 双 buffer 约 2.88 MB
- 使用 PSRAM 分配

绘制内容由 `factory_hdmi.cpp` 内部完成。

## 资源管理

启动时：

- 创建 HDMI panel
- 分配 framebuffer
- 创建 render task
- 注册 panel callback

停止时：

- 通知 render task 退出
- 等待 task 结束
- 删除 panel / panel IO / DSI bus
- 释放 framebuffer
- 关闭 HDMI 电源
- 更新状态为 stopped

页面退出时必须调用 `factory_hdmi_stop()`。

## 风险点

### 共享 I2C

factory 当前显示、触摸、音频、摄像头都可能使用同一条 I2C。

处理策略：

- HDMI 必须复用 `factory_display_get_i2c_bus()`
- 不要在 HDMI 模块中重新 `i2c_new_master_bus()`
- PCA9535 建议抽成共享 `factory_io_expander`

### PCA9535 状态覆盖

touch 初始化时已经配置了 PCA9535 多个输出 pin。

处理策略：

- 不要写整个 port 覆盖所有输出
- 只改 HDMI 相关 pin
- 最好通过共享 IO expander 模块维护 shadow state

### 内存占用

800x600 RGB888 双 buffer 约 2.88 MB。

处理策略：

- 只在 Start 后分配
- Stop 后释放
- 使用 `heap_caps_aligned_calloc()`
- 状态页显示 free PSRAM

### 与电子纸 LVGL 共存

电子纸已经注册了 LVGL display driver。

处理策略：

- HDMI MVP 不注册 LVGL display
- HDMI 独立 task 直接绘制 framebuffer
- UI 状态通过普通 LVGL timer 刷新

### Camera / Audio 资源竞争

Camera 和 Audio 已有独立页面。

处理策略：

- Phase 1/2 不碰 Camera / Audio
- Phase 3/4 再拆共享接口
- 同一时间只允许一个模块拥有 camera/audio runtime

## 验收清单

- Home 页面出现 HDMI tile
- 进入 HDMI 页面不自动卡死
- 不插 HDMI 时显示明确状态
- 插 HDMI 后可以 Start
- HDMI 显示 Pattern
- HDMI 显示 Motion 动画
- FPS 和 frame count 持续更新
- Stop 后 HDMI 画面停止，电源关闭
- 返回 Home 后 render task 退出
- 连续 Start / Stop 10 次不崩溃
- 连续运行 10 分钟无明显内存泄漏
- Camera、Audio、SD、Battery 页面仍可正常进入

## 推荐落地顺序

1. 新增 `FACTORY_PAGE_HDMI` 和空页面
2. 新增 `factory_hdmi` 状态结构与 stub API
3. 页面接入状态刷新和按钮
4. 接入 PCA9535 HDMI 电源时序
5. 接入 LT8912B 初始化
6. 分配 RGB888 framebuffer 并输出静态 Pattern
7. 增加 render task 和 Motion 模式
8. 增加 Stop / deinit 清理路径
9. 做 Start / Stop 循环测试
10. 再进入 Camera / Audio / SD Video 扩展

尚未做实机 HDMI 画面验证，下一步建议上板测试 Pattern/Motion、Start/Stop 循环和不插 HDMI 时的 ready 状态。