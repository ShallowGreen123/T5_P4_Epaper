# T5-P4 V0.2 顶层 BSP 与示例迁移计划

## Summary
- 在 `components/` 下新增单一顶层 BSP `t5_p4_board`，作为 V0.2 的唯一板级真值来源。
- 让每个 example 都能独立发现顶层 `components`；所有板级强相关示例切到新 BSP，所有示例必须 `idf.py build` 全通过。
- 保留 `FastEPD` 里的 `BB_PANEL_LILYGO_T5P4` 入口，但把它的面板引脚修正到 V0.2，并把触摸、音频、HDMI、C6 等非 EPD 控制从各示例私有逻辑收口到 BSP。
- 更新根目录 `readme.md`，明确仓库现在以 V0.2 为准，说明新 BSP、XL9555 依赖方式和新的示例启动说明。

## Public APIs / Interfaces
- 新 BSP 采用 C-first 接口，统一提供：
  - 板级初始化与共享 I2C 总线获取
  - XL9555 / PCA9535 逻辑引脚控制
  - GT911 复位、地址选择，以及可选的 `esp_lcd_touch_gt911` 构建辅助
  - ES8311 功放使能与音频路径切换
  - HDMI 使能 / 复位
  - C6 bootstrap / reset / wakeup
  - V0.2 双路前光 PWM 控制
- 保留 HDMI/UAC 示例现有依赖的兼容入口名，但兼容实现全部放在顶层 BSP 内，不再让 example 自带板级组件参与构建。
- 板内依赖统一下沉到 BSP manifest：
  - `sheldonix/esp_io_expander_xl9555: ^0.8.0`
  - `wngfra/esp_io_expander_pca9535: ^1.0.1`
  - `espressif/esp_lcd_touch_gt911`
- 触摸栈统一到 `esp_lcd_touch_gt911`；`epub_reader_t5_p4` 去掉失效的 SensorLib / XL9555 私有路径。

## Implementation Changes
- `components/t5_p4_board`
  - 固化 V0.2 板级映射：`GPIO6=XL9555 INT`、`EP_STV=48`、`LED1_PWM=53`、`LED2_PWM=54`、`XL9555@0x22`、`PCA9535@0x20`，且 PCA9535 仅保留 EPD/TPS 高 8 位职责
  - 实现共享 I2C、XL9555、PCA9535 的幂等初始化
  - 通过 `XL9555 P00` 实现 GT911 reset/address 序列，触摸中断仍走 `GPIO3`
  - 通过 `XL9555 P06/P07` 控音频，`P10/P11` 控 HDMI，`P13/P14` 控 C6
  - 暴露 `GPIO53/54` 双通道前光 PWM 控制，供 factory 等示例直接复用
- `components/fastepd`
  - 修正 `BB_PANEL_LILYGO_T5P4` 的 V0.2 面板定义
  - 继续复用现有 EPDiy V7 那套 EPD/TPS 回调，因为 V0.2 依然由 PCA9535 驱动这部分
  - 不再让旧 T5-P4 profile 隐式承担触摸 / 音频 / HDMI 的历史副作用
- Example 迁移
  - 所有 example 统一可见顶层 `components`，凡是用到板级能力的都显式依赖 `t5_p4_board`
  - E-Paper / touch 类示例：`factory`、`fastEPD_lvgl_demo`、`crosspoint_reader_t5_p4`、`epub_reader_t5_p4`、`bq_power_dashboard` 改用 BSP 的 I2C / 触摸复位与修正后的 FastEPD T5-P4 profile
  - 音频 / HDMI 类示例：`es8311_mic_speak`、`es8311_spiffs`、`hdmi_video_renderer`、`hdmi_video_renderer_lvgl`、`usb_device_uac` 改用 BSP 的 XL9555 控制，移除重复的 example 内板级实现
  - 摄像头链路：`camera_id_detect`、`camera_wifi_stream`、factory camera 页面把板级供电与固定 wiring 收口到 BSP；传感器识别 / 采流逻辑仍留在示例内
  - Hosted C6 类示例：`c6_wifi_scan`、`usb_device_msc_wireless_disk`、`usb_host_hub_dual_camera` 在启动 hosted 前先走 BSP 的 C6 bootstrap；日志和文档不再宣称 `GPIO54` 是 reset；hosted 所需的直连 reset 占位 GPIO 统一改成未占用的扩展板专用线 `GPIO22`
  - Factory 的“双背光”控制页改成 V0.2 前光双 PWM 语义，不再使用旧 `IO11/IO12`
  - 诊断类示例 `i2c_tools`、`pca9535`、`sgm38121`、`sd_card_test` 以全量可编译为硬目标，仅在文档或宏名仍暗示 V0.1 时做轻量修正

## Test Plan
- 按现有 CI 的 example 发现规则，对每个 example 跑 `idf.py build`，零失败为硬门槛。
- 在 V0.2 实板做板级强相关冒烟：
  - `factory`：前光 PWM、触摸、HDMI、音频、摄像头供电、FastEPD 初始化
  - `fastEPD_lvgl_demo` 或 `epub_reader_t5_p4`：EPD + GT911
  - `hdmi_video_renderer_lvgl`：LT8912B enable/reset
  - `es8311_mic_speak`：`XL9555 P06` 功放使能
  - `c6_wifi_scan`：BSP 拉起 C6 后完成 hosted 扫描
  - `camera_id_detect`：SGM38121 供电时序与传感器识别
- 所有 manifest / CMake 改完后，再跑一轮完整 examples build，确认依赖解析与独立构建链路一致。

## Assumptions And Defaults
- 本次改造后只支持 V0.2，不保留 V0.1 运行时分支。
- `docs/pinmap.md` 继续作为硬件真值来源，`readme.md` 只做入口说明并链接过去。
- Hosted/C6 路线不修改上游 `esp_hosted` 本体；真实的 C6 控制由 BSP 完成，`GPIO22` 仅作为 hosted 对“必须有直连 reset GPIO”这一限制的占位。
- 本轮不新增独立 `XL9555` 诊断示例；XL9555 的验证由新 BSP 和迁移后的板级示例覆盖。
