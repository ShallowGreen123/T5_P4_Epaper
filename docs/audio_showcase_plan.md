可以，我建议把它做成一个 Audio Showcase 页面：既能产测 ES8311/mic/speaker，又有清楚的演示效果。

目标效果
Home 新增一个入口：Audio。进入后是一页黑白仪表盘风格界面，包含：

- Codec 状态：ES8311 是否初始化成功
- Mic 状态：是否读到有效输入
- Speaker 状态：是否完成回放
- 实时音量条：显示 RMS / Peak / Clipping
- 波形窗口：显示最近一段录音的简洁波形
- 操作按钮：Record 3s、Playback、Loopback、Stop
- 底部状态：采样率、mic gain、音量、最近测试结果

实施计划

1. 新增页面入口
在 factory_page_id_t 里新增 FACTORY_PAGE_AUDIO，在 screen_home.cpp 的 kMenuItems 增加 Audio tile。
如果第一页空间不够，我会放到第二页，保持 Home 不拥挤。

2. 新增 Audio 页面
新建 examples/factory/main/ui/screen_audio.cpp，生命周期接入 factory_screen_audio_lifecycle()。
页面结构保持简洁：上方标题 + 右侧 PASS/FAIL 状态，中间大波形/音量条，下方 4 个操作按钮。

3. 新增音频 port 层
新建 factory_audio.h/.cpp，不要把 I2S/ES8311 逻辑写进 UI。
提供类似这些接口：

~~~cpp
bool factory_audio_init(void);
void factory_audio_start_monitor(void);
void factory_audio_record_3s(void);
void factory_audio_playback(void);
void factory_audio_start_loopback(void);
void factory_audio_stop(void);
const factory_audio_state_t *factory_audio_get_state(void);
~~~

4. 处理 ES8311 + I2S
参考现有 examples/es8311_mic_speak 的初始化参数：16 kHz / 16-bit / stereo / MCLK 43 / BCLK 42 / LRCK 40 / DOUT 39 / DIN 41。
这里有一个关键点：factory 当前已经用新的 i2c_master bus 初始化了共享 I2C，所以不能直接照搬示例里的 legacy i2c_driver_install()。我会优先复用 factory_display_get_i2c_bus()，避免和触摸、电池、PCA9535 冲突。

5. 录音与回放
后台 FreeRTOS task 从 I2S RX 读取音频，保存 3 秒 PCM 到 PSRAM buffer。
回放时写到 I2S TX，同时打开功放。录音结束后计算：

- RMS 音量
- Peak 峰值
- Clipping 次数
- 是否静音/过小/过载

6. 实时监测与演示
页面空闲时进入 mic monitor 模式，每 100-200ms 更新一次音量统计。
由于墨水屏不适合高帧率刷新，UI 不做“流畅动画”，而是做“仪表刷新”：音量条、峰值线、波形每次更新都清晰稳定。

7. 产测判定
自动给出简单结果：

- Codec PASS：ES8311 初始化成功
- Mic PASS：录音峰值超过噪声阈值
- Speaker PASS：回放流程成功写入 I2S
- Audio PASS：三项都通过
- 如果失败，显示明确原因，例如 I2C FAIL、I2S RX FAIL、MIC TOO LOW、CLIPPING。

8. 工程接入
修改：

- factory_types.h：新增页面 ID 和 audio 状态结构
- ui_screens.h：声明 audio lifecycle
- ui.cpp：注册 audio 页面
- screen_home.cpp：新增入口
- CMakeLists.txt / idf_component.yml：增加 esp_driver_i2s 和 ES8311 依赖
- Kconfig.projbuild：增加 sample rate、mic gain、volume、record seconds 等配置

页面设计草图

~~~text
[ < ] Audio Showcase                         PASS

Codec  OK    Mic  LIVE    Speaker  READY

+------------------------------------------------+
|                                                |
|      waveform / recorded audio preview          |
|                                                |
+------------------------------------------------+

Mic Level     [############------]  Peak 72%
Noise Floor   4%      Clip 0

[ Record 3s ] [ Playback ] [ Loopback ] [ Stop ]

16 kHz / 16-bit / Gain 30 dB / Vol 70
~~~

把第一版做到“可真实产测”：能初始化 ES8311、实时看 mic、录 3 秒、回放、显示波形和 PASS/FAIL。视觉上保持 factory 现有的黑白卡片风格，但比普通日志页更像一个干净的音频仪表盘。