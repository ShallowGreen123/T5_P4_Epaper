# HDMI Video Renderer AI 工作流

## 1. 项目定位

这个项目本质上不是“通用播放器”，而是一个运行在 `ESP32-P4` 上、从 `SD 卡` 读取媒体文件、使用 `硬件 JPEG 解码` 输出到 `HDMI` 的嵌入式播放示例。

要让 AI 工具稳定生成它，必须先冻结下面这些边界条件：

- 芯片和框架固定为 `ESP32-P4 + ESP-IDF >= 5.4`
- 显示链路固定为 `ESP32-P4-Function-EV-Board + ESP-HDMI-Bridge`
- 文件输入固定为 `SD 卡`
- 视频解码固定为 `MJPEG`
- 音频输出基于 `esp_audio_codec`
- 容器解析依赖本地组件 `components/esp_extractor`
- `esp_extractor` 的核心解析能力来自预编译库 `components/esp_extractor/lib/esp32p4/libesp_extractor.a`

这意味着 AI 不应该把目标理解为“从零实现一个多格式播放器”，而应该理解为：

1. 生成一个 `ESP-IDF` 示例工程骨架
2. 接入现有 BSP、音频组件和本地 `esp_extractor`
3. 在应用层完成“抽帧 -> JPEG 解码 -> 必要时缩放 -> 刷到 HDMI”的管线

## 2. 当前项目的真实工作流程

### 2.1 模块分层

- [`main/main.c`](/d:/dgx/code/0_lilygo/T5_P4_E_Paper/examples/hdmi_video_renderer/main/main.c):68
  负责硬件初始化、缓冲区分配、SD 卡挂载、音频设备初始化、播放循环
- [`main/app_stream_adapter.c`](/d:/dgx/code/0_lilygo/T5_P4_E_Paper/examples/hdmi_video_renderer/main/app_stream_adapter.c):645
  负责抽帧任务、JPEG 硬解、PPA 缩放、显示回调、播放状态管理
- [`main/app_extractor.c`](/d:/dgx/code/0_lilygo/T5_P4_E_Paper/examples/hdmi_video_renderer/main/app_extractor.c):813
  负责封装 `esp_extractor`、文件 I/O、流信息解析、音频任务和音频解码
- [`components/esp_extractor/CMakeLists.txt`](/d:/dgx/code/0_lilygo/T5_P4_E_Paper/examples/hdmi_video_renderer/components/esp_extractor/CMakeLists.txt)
  将本地头文件和预编译提取库链接进工程

### 2.2 运行时数据流

1. `app_main()` 初始化 HDMI 显示、信号量和显示缓冲区。
2. 挂载 SD 卡，初始化音频 codec。
3. 用缓冲区、目标分辨率、音频设备句柄初始化 `app_stream_adapter`。
4. 调用 `app_stream_adapter_set_file()` 绑定媒体文件。
5. `app_stream_adapter_start()` 内部启动 `app_extractor_start()`，打开媒体文件并解析音视频流信息。
6. `extract_task` 循环调用 `app_extractor_read_frame()` 读取帧。
7. 视频帧进入 `extractor_frame_callback()`，被复制到 JPEG 缓冲区后进入硬件 JPEG 解码。
8. 如果视频分辨率和 HDMI 输出分辨率不一致，则走 `PPA` 缩放。
9. 解码后的 RGB 帧交给 `display_decoded_frame()`，再通过 `esp_lcd_panel_draw_bitmap()` 刷到 HDMI 面板。
10. 音频帧进入 `audio_queue`，由音频任务解码后写入 `esp_codec_dev_write()`。
11. 播放结束后，主循环用“帧计数长期不变”来判断 EOS，并重新绑定文件实现循环播放。

### 2.3 关键约束

- `main/app_extractor.c` 只接受 `MJPEG` 视频格式，发现 `H.264` 会直接报不支持。
  参考 [`main/app_extractor.c`](/d:/dgx/code/0_lilygo/T5_P4_E_Paper/examples/hdmi_video_renderer/main/app_extractor.c):696
- `main/app_stream_adapter.c` 的核心是“JPEG 解码 + 可选 PPA 缩放”，不是软解视频播放器。
  参考 [`main/app_stream_adapter.c`](/d:/dgx/code/0_lilygo/T5_P4_E_Paper/examples/hdmi_video_renderer/main/app_stream_adapter.c):359 和 [`main/app_stream_adapter.c`](/d:/dgx/code/0_lilygo/T5_P4_E_Paper/examples/hdmi_video_renderer/main/app_stream_adapter.c):284
- `README` 明确要求视频做成 MJPEG，并指出启用 LCD 内部缓冲时，视频分辨率必须和 HDMI 输出一致。
  参考 [`README.md`](/d:/dgx/code/0_lilygo/T5_P4_E_Paper/examples/hdmi_video_renderer/README.md):72 和 [`README.md`](/d:/dgx/code/0_lilygo/T5_P4_E_Paper/examples/hdmi_video_renderer/README.md):92
- `Kconfig` 暴露的真实配置入口只有文件名、LCD buffer、可选音频解码器和 A/V sync 开关。
  参考 [`main/Kconfig.projbuild`](/d:/dgx/code/0_lilygo/T5_P4_E_Paper/examples/hdmi_video_renderer/main/Kconfig.projbuild):4
- 当前工程默认配置是 `RGB888 + HDMI + 双缓冲 + PSRAM`
  参考 [`sdkconfig.defaults`](/d:/dgx/code/0_lilygo/T5_P4_E_Paper/examples/hdmi_video_renderer/sdkconfig.defaults)

## 3. AI 稳定生成的核心原则

### 3.1 不要一次性全量生成

最不稳定的方式是给 AI 一句“帮我写一个 ESP32-P4 HDMI 播放器”。  
最稳定的方式是把任务拆成固定阶段，并且每一阶段只允许生成少量文件。

### 3.2 把 `esp_extractor` 视为外部能力，不要重写 demuxer

稳定复现这个项目时，AI 只需要：

- 封装 `esp_extractor` 的调用
- 实现本地文件 I/O 适配
- 处理帧路由和音频队列

AI 不应该尝试：

- 自己实现 MP4 / AVI 解析器
- 自己实现 AAC / MP3 demux
- 用软件方式重写媒体框架

### 3.3 先做最小闭环，再做增强

稳定顺序应该是：

1. 先跑通 `MP4 + MJPEG + 无音频 + 分辨率匹配`
2. 再加 `AAC` 音频
3. 再加 `PPA` 缩放
4. 最后再加可选格式和调优

不要一开始就让 AI 同时实现：

- AVI
- 多种音频格式
- 完整 A/V sync
- 网络流
- UI 菜单

## 4. 推荐的 AI 生成工作流

### 阶段 0：冻结输入规格

先把下面信息作为“不可漂移的输入”给 AI：

- 目标平台：`ESP32-P4`
- 构建系统：`ESP-IDF`
- 工程依赖：[`main/idf_component.yml`](/d:/dgx/code/0_lilygo/T5_P4_E_Paper/examples/hdmi_video_renderer/main/idf_component.yml)
- 本地组件：`components/esp_extractor`
- 输入源：`SD card`
- 视频格式：`MJPEG only`
- 首选容器：`MP4`
- 显示输出：`1280x720 HDMI`
- 默认色彩格式：`RGB888`
- 缓冲模式：`2 buffers`

如果这一步不固定，AI 后面很容易擅自改成：

- `H.264`
- 网络流
- LVGL 播放器 UI
- 软件解码
- SPI LCD

### 阶段 1：生成工程骨架

只让 AI 生成这些文件：

- `CMakeLists.txt`
- `main/CMakeLists.txt`
- `main/idf_component.yml`
- `main/Kconfig.projbuild`
- `sdkconfig.defaults`
- `partitions.csv`

这一阶段的验收标准：

- 工程 target 是 `esp32p4`
- 依赖包含 `esp32_p4_function_ev_board` 和 `esp_audio_codec`
- 启用 PSRAM
- 默认 HDMI / RGB888 / 双缓冲
- `menuconfig` 能看到视频文件名和 buffer 相关配置项

### 阶段 2：生成提取层 `app_extractor`

只让 AI 生成：

- `main/app_extractor.h`
- `main/app_extractor.c`

必须要求 AI 实现的内容：

- 文件 I/O 包装函数：`open/read/seek/close/file_size`
- 注册 MP4 / AVI extractor
- 读取并缓存 stream info
- 校验视频格式必须是 `MJPEG`
- 建立音频任务和音频队列
- 用 `esp_audio_dec_*` 或 `esp_audio_simple_dec` 路径完成音频解码
- 对外提供 `init/start/read_frame/get_info/seek/stop/deinit`

这一阶段不要让 AI 写：

- HDMI 显示逻辑
- JPEG 解码
- LCD buffer 分配

阶段验收标准：

- 能打开文件并解析出视频宽高、fps、duration
- 碰到 `H.264` 能返回不支持
- 没音频时也能正常工作

### 阶段 3：生成流适配层 `app_stream_adapter`

只让 AI 生成：

- `main/app_stream_adapter.h`
- `main/app_stream_adapter.c`

必须要求 AI 实现的内容：

- JPEG 硬解初始化
- 解码输出缓冲区管理
- `extract_task` 任务控制
- 视频帧回调
- 必要时通过 `PPA` 进行缩放
- `start/stop/seek/get_info/get_stats/deinit`

这一步的关键要求：

- 适配层只处理“已抽取出来的帧”
- 不要把文件读取逻辑写回这一层
- 不要把 HDMI 初始化写到这一层

阶段验收标准：

- 给它一个 JPEG 帧 buffer 可以解码成 RGB buffer
- 分辨率不匹配时可以走 PPA 缩放
- 可以统计 `frames_processed`

### 阶段 4：生成应用入口 `main.c`

只让 AI 生成：

- `main/main.c`

必须要求 AI 实现的内容：

- 初始化 HDMI 显示
- 分配 LCD buffer
- 创建刷新完成信号量
- 挂载 SD 卡
- 初始化音频设备
- 初始化 `app_stream_adapter`
- 打开目标文件
- 启动播放和循环播放逻辑

阶段验收标准：

- `app_main()` 能完成硬件初始化
- 能找到 SD 卡中的目标文件
- 能进入播放循环

### 阶段 5：生成 README 和使用说明

只让 AI 写文档：

- `README.md`

必须包含：

- 硬件连接方式
- `menuconfig` 项
- 构建命令
- 视频格式限制
- `ffmpeg` 转码命令

推荐先把转码范围缩到最稳组合：

```bash
ffmpeg -i input.mp4 -c:v mjpeg -q:v 5 -vf scale=1280:720 -r 20 -c:a aac output.mp4
```

参考当前项目中的 ffmpeg 建议：
[`README.md`](/d:/dgx/code/0_lilygo/T5_P4_E_Paper/examples/hdmi_video_renderer/README.md):133

### 阶段 6：集成验收

最后再让 AI 做联调，不要在前面阶段提前联调。

验收顺序建议固定为：

1. 编译通过
2. SD 卡挂载成功
3. 能解析视频信息
4. 能显示第一帧
5. 能持续播放视频
6. 再验证音频
7. 最后验证循环播放

## 5. 面向 AI 工具的稳定提示词模板

### 5.1 总控提示词

```text
你要生成一个 ESP-IDF 示例工程，目标芯片是 ESP32-P4。
这个项目不是通用播放器，而是 SD 卡本地媒体文件播放器。

固定约束：
1. 只支持 ESP32-P4
2. 只使用 ESP-IDF，不使用 Arduino
3. 视频只支持 MJPEG
4. 首选 MP4 容器，AVI 作为后续可选扩展
5. 视频从 SD 卡读取
6. 显示输出到 HDMI
7. 使用 BSP 提供的显示和音频初始化 API
8. 容器解析依赖本地组件 components/esp_extractor
9. 不要实现自己的 MP4 demuxer
10. 先保证最小闭环，再逐步加音频和缩放

请严格分阶段生成，每个阶段只修改我指定的文件，不要一次生成整个项目。
```

### 5.2 单阶段提示词模板

```text
当前只实现阶段 X。

只允许修改这些文件：
- <file1>
- <file2>

必须满足：
- <constraint 1>
- <constraint 2>
- <constraint 3>

禁止做这些事：
- 不要修改其他文件
- 不要引入网络流
- 不要改成 H.264
- 不要添加 UI 框架
- 不要重写第三方组件

完成后请输出：
1. 修改了哪些文件
2. 当前阶段完成了什么
3. 还缺什么才能进入下一阶段
```

## 6. 最容易让 AI 生成失败的点

### 6.1 把“媒体解析”也交给 AI 从零实现

这会极大增加不稳定性。  
正确做法是保留 [`components/esp_extractor`](/d:/dgx/code/0_lilygo/T5_P4_E_Paper/examples/hdmi_video_renderer/components/esp_extractor) 作为固定依赖。

### 6.2 没有限定视频编码格式

如果不明确写出 “`MJPEG only`”，AI 很可能默认写成 `H.264` 播放器，但当前代码明确不支持。

### 6.3 一开始就要求完整音视频同步

当前项目虽然有 `HDMI_VIDEO_SYNC_ENABLED` 配置项，但实现重点仍然是：

- 视频侧按 FPS 控制节奏
- 音频侧通过队列和 codec 写入播放

它不是完整媒体框架级别的时钟同步器。  
所以 AI 工作流里应把“高级 A/V sync”视为后续增强，而不是一期硬目标。

### 6.4 不限定输出分辨率和缓冲模式

如果 AI 没被明确要求 `1280x720`、`RGB888`、`双缓冲`、`PSRAM`，就容易生成：

- 不匹配 HDMI 输出的帧大小
- 错误的 buffer 大小
- 错误的像素格式
- 刷新闪烁或内存不足

## 7. 推荐的最终验收清单

生成完成后，用下面的清单检查 AI 产物是否靠谱：

- [`main/idf_component.yml`](/d:/dgx/code/0_lilygo/T5_P4_E_Paper/examples/hdmi_video_renderer/main/idf_component.yml) 包含正确依赖
- [`components/esp_extractor/CMakeLists.txt`](/d:/dgx/code/0_lilygo/T5_P4_E_Paper/examples/hdmi_video_renderer/components/esp_extractor/CMakeLists.txt) 正确链接预编译库
- [`main/app_extractor.c`](/d:/dgx/code/0_lilygo/T5_P4_E_Paper/examples/hdmi_video_renderer/main/app_extractor.c):911 能打开文件并解析流
- [`main/app_extractor.c`](/d:/dgx/code/0_lilygo/T5_P4_E_Paper/examples/hdmi_video_renderer/main/app_extractor.c):696 能拒绝非 MJPEG 视频
- [`main/app_stream_adapter.c`](/d:/dgx/code/0_lilygo/T5_P4_E_Paper/examples/hdmi_video_renderer/main/app_stream_adapter.c):419 能接收视频帧并触发 JPEG 解码
- [`main/app_stream_adapter.c`](/d:/dgx/code/0_lilygo/T5_P4_E_Paper/examples/hdmi_video_renderer/main/app_stream_adapter.c):817 能启动播放
- [`main/main.c`](/d:/dgx/code/0_lilygo/T5_P4_E_Paper/examples/hdmi_video_renderer/main/main.c):68 能初始化显示、SD 卡和音频
- [`main/main.c`](/d:/dgx/code/0_lilygo/T5_P4_E_Paper/examples/hdmi_video_renderer/main/main.c):175 具备循环播放逻辑

## 8. 一句话版结论

如果要让 AI 稳定生成这个项目，最关键的不是“让 AI 更聪明”，而是把任务约束成下面这个固定公式：

`ESP32-P4 + ESP-IDF + SD 卡 MP4(MJPEG) + 本地 extractor 组件 + JPEG 硬解 + PPA 缩放 + HDMI 输出 + 分阶段生成 + 每阶段验收`

只要保持这个公式不漂移，AI 工具就能比较稳定地复现当前项目。
