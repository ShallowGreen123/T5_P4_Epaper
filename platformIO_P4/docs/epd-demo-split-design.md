# 墨水屏演示 Demo 拆分设计

## 1. 推荐工作流

### 1.1 一句话目标

面向板级 bring-up、驱动联调和回归验证，把 T5-P4 E-paper 的主显示链路拆成一组单目标 demo，证明它能稳定完成上电、黑白绘图、局刷、灰阶、图片显示和触摸联动。

成功标准：

- 首批基础 demo 能证明“能上电、能整刷、能局刷、能灰阶”。
- 第二批 demo 能证明“能显示图片、能读 SD、能做触摸反馈”。
- 最后一批 demo 用于稳定性和展示效果，不阻塞前面的基础验证。

### 1.2 范围边界

- 本轮模块名统一使用 `epd`。
- 本轮只拆墨水屏主显示链路，不把 `hdmi_video_renderer`、`lvgl_*` 这类 HDMI/LVGL 路径混进同一批 demo。
- 音频、C6、摄像头都不纳入本轮墨水屏 demo 设计。

### 1.3 已确认的硬件抽象

- 主显示方案使用 `FastEPD`，目标面板为 `BB_PANEL_LILYGO_T5P4`。
- EPD 并口与时序控制脚已确认：
  - `D0~D7 -> GPIO27~34`
  - `CKH -> GPIO24`
  - `STH -> GPIO25`
  - `LEH -> GPIO26`
  - `CKV -> GPIO13`
  - `STV -> GPIO36`
- 主 I2C 为 `GPIO7/8`。
- EPD 电源与控制链路依赖 `PCA9535` 和 `TPS651851`。
- 触摸控制器已确认是 `GT911 @ 0x5D`，中断 `GPIO3`，复位经 `PCA9535 IO0`。
- SD SPI 已确认是 `GPIO44/45/46/47`。
- 当前工程 `examples/` 为空，`platformio.ini` 通过 `src_dir` 切换示例，适合采用“一例一目录”的拆分方式。

### 1.4 推荐推进顺序

1. 先做 `smoke`，把上电链路和面板初始化单独跑通。
2. 再做 `func`，分别验证 1bpp 绘图、局刷、灰阶、抗锯齿文本、sprite 和内置图片。
3. 接着做 `integ`，把 SD 资源和触摸联动接进来。
4. 最后做 `robust` 和 `showcase`，补齐回归入口和展示效果。

### 1.5 现有参考来源

- `lib/FastEPD/examples/Arduino/getting_started`
- `lib/FastEPD/examples/Arduino/grayscale_test`
- `lib/FastEPD/examples/Arduino/antialias_font`
- `lib/FastEPD/examples/Arduino/sprite_demo`
- `lib/FastEPD/examples/Arduino/show_jpeg`
- `lib/FastEPD/examples/Arduino/sd_file_explorer`
- `lib/FastEPD/examples/Arduino/gif_player`
- `lib/FastEPD/examples/Arduino/LilyGoT5_Shutdown`

## 2. 目录结构

```text
project/
|- examples/
|  `- epd/
|     |- smoke_power_on/
|     |- func_bw_primitives/
|     |- func_partial_counter/
|     |- func_grayscale_ramp/
|     |- func_antialias_text/
|     |- func_sprite_canvas/
|     |- func_builtin_jpeg/
|     |- integ_sd_gallery/
|     |- integ_touch_canvas/
|     |- robust_power_cycle/
|     `- showcase_gif_partial/
|- firmware/
|- hardware/
|  |- schematic/
|  |- datasheet/
|  |- pinmap/
|  `- notes/
|- lib/
|  `- demo_core/
|     |- demo_config.h
|     |- demo_pins.h
|     |- demo_log.h
|     |- demo_check.h
|     |- demo_epd_board.h
|     |- demo_epd_board.cpp
|     |- demo_epd_modes.h
|     |- demo_epd_modes.cpp
|     |- demo_epd_assets.h
|     |- demo_epd_assets.cpp
|     |- demo_touch_gt911.h
|     |- demo_touch_gt911.cpp
|     |- demo_storage.h
|     `- demo_storage.cpp
|- scripts/
|- docs/
|  `- epd-demo-split-design.md
`- platformio.ini
```

命名说明：

- 模块目录直接使用 `epd`，和当前需求范围一致。
- `smoke / func / integ / robust / showcase` 直接表达测试层级。
- 单个 example 名只描述一个目标，不把多个行为混在一起。

## 3. example 拆分方案

| Example | 类型 | 演示目的 | 前置条件 | 关键步骤 | 成功判据 | 失败日志 / 排查入口 |
| --- | --- | --- | --- | --- | --- | --- |
| `examples/epd/smoke_power_on` | 冒烟 | 验证 EPD 电源链路、PCA9535、TPS651851 和面板初始化 | pinmap 已确认 | 初始化 I2C，拉起 EPD 供电，面板 begin，执行白屏/黑屏/白屏整刷 | 屏幕能稳定完成 3 次整刷，串口打印 init 与 full refresh 耗时 | `I2C probe fail`、`PCA init fail`、`TPS power good timeout`、`panel begin fail` |
| `examples/epd/func_bw_primitives` | 功能 | 验证 1bpp 基础图元和文本 | `smoke_power_on` 稳定 | 切到 1bpp，绘制线段、矩形、圆、文本、坐标网格，再整刷一次 | 图元清晰、文字不偏移、不出现整屏错位 | `draw test start` 后无刷新、边界裁切异常、整刷耗时异常长 |
| `examples/epd/func_partial_counter` | 功能 | 验证固定窗口局刷 | `func_bw_primitives` 稳定 | 预绘制静态底图，在固定矩形区域更新计数器或时钟，连续局刷 | 局部区域连续更新，非更新区域保持稳定，闪烁可控 | `partial update fail`、局刷区域越界、非目标区域被污染 |
| `examples/epd/func_grayscale_ramp` | 功能 | 验证 4bpp 灰阶模式切换和灰阶层次 | `smoke_power_on` 稳定 | 切到 4bpp，绘制 16 级灰阶条、灰阶块和标尺，再整刷 | 16 级灰阶可分辨，切回 1bpp 后不异常 | `switch 4bpp fail`、灰阶显示发花、模式切回失败 |
| `examples/epd/func_antialias_text` | 功能 | 验证灰阶文本和抗锯齿字体渲染 | `func_grayscale_ramp` 稳定 | 加载 AA 字体，绘制不同字号和灰度文本，对比普通字体 | 文本边缘平滑、字号和位置正确 | `font load fail`、文字边缘锯齿明显、灰度文字失真 |
| `examples/epd/func_sprite_canvas` | 功能 | 验证离屏绘制和 sprite 合成能力 | `func_bw_primitives` 稳定 | 创建 sprite / 离屏缓冲，先离屏绘制，再拷贝到主 framebuffer 刷新 | 局部元素可独立重绘，不破坏背景 | `sprite alloc fail`、sprite 合成错位、复制后画面破碎 |
| `examples/epd/func_builtin_jpeg` | 功能 | 验证内置 JPEG 从 Flash 解码到 EPD framebuffer | `func_grayscale_ramp` 稳定，`JPEGDEC` 可用 | 解码内置 JPEG，按目标位置写入 framebuffer，再整刷显示 | 图片位置、尺寸、灰度正常，串口有 decode 耗时 | `jpeg open fail`、`jpeg decode fail`、图片偏色或位置错误 |
| `examples/epd/integ_sd_gallery` | 联调 | 验证 SD 卡挂载、图片枚举和翻页显示 | `func_builtin_jpeg` 稳定，SD SPI 可用 | 挂载 SD，枚举指定目录图片，按键或定时翻页显示 | SD 可挂载，能连续显示多张图片并翻页 | `sd mount fail`、`open image fail`、图片列表为空、翻页卡死 |
| `examples/epd/integ_touch_canvas` | 联调 | 验证 GT911 触摸坐标与 EPD 局刷联动 | `func_partial_counter` 稳定，GT911 可读点 | 初始化触摸，轮询读点，在局刷区域画点、画线、显示坐标 | 触摸反馈与手指位置基本一致，局刷区域稳定 | `gt911 begin fail`、无触点、坐标漂移、触摸后刷新异常 |
| `examples/epd/robust_power_cycle` | 稳定性 | 验证多轮上电、刷新、关断、再上电的可靠性 | `smoke_power_on` 稳定 | 重复执行 power on、full refresh、power off，多轮记录结果 | 连续多轮无卡死，统计日志可定位失败轮次 | `power off fail`、重上电卡死、某轮刷新耗时突增 |
| `examples/epd/showcase_gif_partial` | 展示，可选 | 验证 GIF 小窗口动效与局刷结合 | `func_partial_counter` 与 `func_builtin_jpeg` 稳定，`AnimatedGIF` 可用 | 解码 GIF 帧，仅刷新变化行，保持 EPD 上电 | 小窗口动画能播放，串口可看到帧耗时 | `gif open fail`、帧解码失败、局刷闪烁失控 |

### 3.1 首批最小可交付

- `smoke_power_on`
- `func_bw_primitives`
- `func_partial_counter`
- `func_grayscale_ramp`

这 4 个 demo 已足够完成墨水屏主显示链路 bring-up。

### 3.2 第二批扩展

- `func_antialias_text`
- `func_sprite_canvas`
- `func_builtin_jpeg`
- `integ_sd_gallery`
- `integ_touch_canvas`

### 3.3 第三批回归和展示

- `robust_power_cycle`
- `showcase_gif_partial`

### 3.4 统一验证与交付规则

每个 example 都按同一套方式验证：

- 编译：切换对应 `src_dir` 后执行 `platformio run -e esp32-p4-evboard`
- 烧录：执行 `platformio run -e esp32-p4-evboard -t upload`
- 串口：执行 `platformio device monitor -b 115200`
- 日志：必须打印 `begin / step / pass / fail / end`
- 固件归档：统一放到 `firmware/epd/<date>/`

## 4. 公共层设计

### `demo_config.h`

- 放串口波特率、默认延时、超时、循环次数、默认测试区域等公共配置。
- 不允许每个 example 自己散落 magic number。

### `demo_pins.h`

- 放 EPD、GT911、SD、PCA9535、TPS651851 相关映射。
- 只维护一份板级定义，避免示例各写各的宏。

### `demo_log.h`

- 提供统一日志宏，例如 `DEMO_BEGIN`、`DEMO_STEP`、`DEMO_PASS`、`DEMO_FAIL`、`DEMO_END`。
- 保证串口日志格式一致，便于回归比对。

### `demo_check.h`

- 提供检查宏、超时等待、失败即返回等通用逻辑。
- 让每个 example 都能快速落到统一失败出口。

### `demo_epd_board.{h,cpp}`

- 负责板级显示 bring-up。
- 包括 I2C、PCA9535、TPS651851、EPD 电源时序和面板初始化。
- 对外建议提供：
  - `bool demoEpdBegin()`
  - `bool demoEpdPowerOn()`
  - `bool demoEpdPowerOff()`
  - `FASTEPD &demoEpd()`
  - `void demoEpdLogInfo()`

### `demo_epd_modes.{h,cpp}`

- 负责 1bpp/4bpp 模式切换、整刷和局刷包装。
- 把“模式状态”和“刷新窗口”从单个 example 中抽出去。

### `demo_epd_assets.{h,cpp}`

- 统一管理内置字体、JPEG、测试底图和演示素材。
- 第一版只保留少量必须资源，避免大文件分散在各示例目录。

### `demo_touch_gt911.{h,cpp}`

- 负责 GT911 初始化、轮询读点、坐标映射和基础去抖。
- 第一版优先轮询，不急着引入复杂中断状态机。

### `demo_storage.{h,cpp}`

- 负责 SD 挂载、文件过滤、图片路径枚举。
- 不负责具体渲染，保持存储层和显示层解耦。

## 5. 代码规范

- 保持 Arduino 风格，让 `setup()` 能直接读出完整演示流程。
- 不在 `loop()` 中隐藏复杂状态机，能在 `setup()` 跑完的演示就不要拆散。
- 所有阻塞动作必须带超时，并打印步骤与失败点。
- 配置集中到 `demo_config.h`，引脚集中到 `demo_pins.h`，重复逻辑下沉到 `lib/demo_core`。
- 一个 example 只承担一个主要目标，不把显示、触摸、存储、动效堆进同一个入口。
- 串口日志必须让人一眼看出“当前步骤、成功条件、失败位置”。
- 不把当前 demo 写成量产架构，优先保证可验证、可观察、可扩展。

## 6. 自动化建议

- 先补齐 `lib/demo_core` 的最小公共头文件：
  - `demo_config.h`
  - `demo_pins.h`
  - `demo_log.h`
  - `demo_check.h`
- 然后优先落地第一批 4 个基础 demo。
- 新增 `scripts/build_epd_examples.ps1`，按固定顺序批量编译：
  - `smoke_power_on`
  - `func_bw_primitives`
  - `func_partial_counter`
  - `func_grayscale_ramp`
- `platformio.ini` 的 `src_dir` 预留注释建议改成：

```ini
; src_dir = examples/epd/smoke_power_on
; src_dir = examples/epd/func_bw_primitives
; src_dir = examples/epd/func_partial_counter
; src_dir = examples/epd/func_grayscale_ramp
; src_dir = examples/epd/func_antialias_text
; src_dir = examples/epd/func_sprite_canvas
; src_dir = examples/epd/func_builtin_jpeg
; src_dir = examples/epd/integ_sd_gallery
; src_dir = examples/epd/integ_touch_canvas
; src_dir = examples/epd/robust_power_cycle
; src_dir = examples/epd/showcase_gif_partial
```

## 7. 可选脚手架

当前 skill 自带脚手架支持嵌套目录，因此可以直接生成这批墨水屏 demo 骨架：

```powershell
python '.agents/skills/embedded-workflow/scripts/scaffold_example.py' epd smoke_power_on --style nested
python '.agents/skills/embedded-workflow/scripts/scaffold_example.py' epd func_bw_primitives --style nested
python '.agents/skills/embedded-workflow/scripts/scaffold_example.py' epd func_partial_counter --style nested
python '.agents/skills/embedded-workflow/scripts/scaffold_example.py' epd func_grayscale_ramp --style nested
python '.agents/skills/embedded-workflow/scripts/scaffold_example.py' epd func_antialias_text --style nested
python '.agents/skills/embedded-workflow/scripts/scaffold_example.py' epd func_sprite_canvas --style nested
python '.agents/skills/embedded-workflow/scripts/scaffold_example.py' epd func_builtin_jpeg --style nested
python '.agents/skills/embedded-workflow/scripts/scaffold_example.py' epd integ_sd_gallery --style nested
python '.agents/skills/embedded-workflow/scripts/scaffold_example.py' epd integ_touch_canvas --style nested
python '.agents/skills/embedded-workflow/scripts/scaffold_example.py' epd robust_power_cycle --style nested
python '.agents/skills/embedded-workflow/scripts/scaffold_example.py' epd showcase_gif_partial --style nested
```

脚手架生成的 `main.cpp` 默认依赖：

- `demo_log.h`
- `demo_check.h`
- `demo_config.h`
- `demo_pins.h`

所以更顺手的落地顺序是：

1. 先补齐这 4 个公共头文件。
2. 再批量生成 `examples/epd/*` 骨架。
3. 最后按“基础 -> 扩展 -> 稳定性”的顺序逐个填充实现。
