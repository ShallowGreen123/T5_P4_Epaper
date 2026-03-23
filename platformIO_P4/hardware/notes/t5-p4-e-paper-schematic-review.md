# T5-P4 E-paper 原理图解读

## 1. 关键硬件结论

- 主控是 `ESP32-P4`，板上还挂了一个 `ESP32-C6` 作为 Wi-Fi/共处理器。
- 绝大多数低速外设共用一组主 I2C，总线为 `GPIO7(SDA)` 和 `GPIO8(SCL)`。
- 墨水屏数据总线走 `ESP32-P4` 直连 8bit 并口；电源时序和若干控制脚不直连主控，而是通过 `PCA9535` I/O 扩展和 `TPS651851` 电源芯片完成。
- 触摸部分在原理图里表现为 `CTP_*` 接口；结合当前板级验证，已确认本板使用 `GT911 @ 0x5D`，其中 `T_INT -> GPIO3`、`T_RST -> PCA9535 IO0`、`CTP_SDA/SCL -> GPIO7/8`。
- 音频部分是 `ES8311`，I2C 地址 `0x18`；从软件使用视角，I2S 映射已确认是 `MCLK=GPIO43`、`BCLK=GPIO42`、`LRCK=GPIO40`、`ESP32 DOUT=GPIO39`、`ESP32 DIN=GPIO41`。
- HDMI 部分是 `LT8912B`，配置 I2C 仍走 `GPIO7/8`，DDC 走 `GPIO9/10`，中断走 `GPIO4`，复位/使能走 `PCA9535`。
- 电池相关芯片包含 `BQ25896(0x6B)` 和 `BQ27220(0x55)`，也在同一条 I2C 总线上。

## 2. 分页解读

### Page 1: 主控与基础启动

- 这一页是 `ESP32-P4` 主控本体和基础启动/复位网络。
- 能直接读到 `RST/EN` 和若干 `GPIO0~GPIO3` 标注，说明页 1 更偏 SoC 本体与基础连接。

### Page 2: 电池与基础电源

- `BQ25896` 充电管理芯片，I2C 地址 `0x6B`。
- `BQ27220YZFR` 电量计，I2C 地址 `0x55`。
- `SGM38121` 相关电源器件，I2C 地址 `0x28`。
- 这一页还能读到 `SDA-------IO7`、`SCL-------IO8`，说明电源/电池管理同样挂在主 I2C 上。

### Page 3: ESP32-C6 与 SDIO

- `ESP32-C6` 的 `D0~D3/CLK/CMD` 分别接到 `GPIO14~19`。
- `C6_WAKEUP -> GPIO6`，`C6_RST/EN -> GPIO54`。
- 这部分与当前仓库里的 `RemoteWiFiHosted` 定义一致。

### Page 4: 墨水屏电源、I/O 扩展、触摸接口

- `TPS651851RSLR` 是墨水屏高压电源芯片，I2C 地址 `0x68`。
- `PCA9535PW` 是 16bit I/O 扩展，I2C 地址 `0x20`，中断脚 `PCA_INT -> GPIO5`。
- 这一页能直接读到 `STH-------IO25`、`STV-------IO36`、`CKV-------IO13`，对应墨水屏行驱动控制线。
- 还能读到 `T_INT-------IO3`、`SDA-------IO7`、`SCL-------IO8`，对应触摸接口。
- `PCA9535` 的 16 个扩展位已经在原理图中明确命名：`T_RST`、`CC_SW0`、`CC_SW1`、`LR_RST`、`NRF_CE`、`SHUTDOWN`、`HDMI_RST`、`HDMI_EN`、`EP_OE`、`EP_MODE`、`1V8_EN`、`TPS_PWRUP`、`VCOM_CTRL`、`TPS_WAKEUP`、`TPS_PWR_GOOD`、`TPS_INT`。

### Page 5: ESP32-P4 引脚落点总览

- 这页把主控 GPIO 和板级网络对上了，是生成引脚映射最关键的一页。
- 墨水屏并口数据线可直接确认：
  - `EP_D0~EP_D7 -> GPIO27~34`
  - `EP_CKH -> GPIO24`
  - `EP_STH -> GPIO25`
  - `EP_LE -> GPIO26`
  - `EP_CKV -> GPIO13`
  - `EP_STV -> GPIO36`
- 这页也能确认：
  - `SD_CS/SPI_MOSI/SPI_SCK/SPI_MISO -> GPIO47/46/45/44`
  - `I2S_MCLK -> GPIO43`
  - `I2S_SCLK -> GPIO42`
  - `I2S_LRCK -> GPIO40`
  - `ESP32 I2S DOUT -> GPIO39`
  - `ESP32 I2S DIN -> GPIO41`
  - `HDMI_INT -> GPIO4`
  - `T_INT -> GPIO3`
  - `PCA_INT -> GPIO5`
  - `C6_WAKEUP -> GPIO6`

### Page 6: ES8311 音频编解码

- `ES8311` 的 I2C 地址为 `0x18`。
- 这页能直接读到：
  - `I2S_SCLK------IO42`
  - `I2S_ASDOUT------IO41`
  - `I2S_DSDIN------IO39`
- 结合 Page 5，可确认 `I2S_LRCK -> GPIO40`，`I2S_MCLK -> GPIO43`。
- 这里的 `ASDOUT/DSDIN` 是按 `ES8311` 视角命名；落实到 `ESP32-P4` 软件侧时，应按如下方式使用：
  - `ESP32 I2S DOUT -> GPIO39 -> ES8311 DSDIN`
  - `ESP32 I2S DIN -> GPIO41 -> ES8311 ASDOUT`
- 以上映射已与当前板级引脚表对齐，不再作为待确认项。

### Page 7: LT8912B MIPI-DSI 转 HDMI

- HDMI 桥接芯片是 `LT8912B`。
- 控制与状态：
  - `LT8912B_SDA/SCL -> GPIO7/8`
  - `LT8912B_INT -> GPIO4`
  - `LT8912B_RST -> PCA9535 IO6`
  - `HDMI_EN -> PCA9535 IO7`
  - `1V8_EN -> PCA9535 IO10`
- DDC：
  - `DSDA -> GPIO9`
  - `DSCL -> GPIO10`
- 输入侧是 `DSI_DATA0/1` 和 `DSI_CLK`，属于 `ESP32-P4` 的 MIPI DSI 专用功能，不建议当作普通 GPIO 理解。

### Page 8: 摄像头/扩展接口

- 能读到 `CAM_MCLK`、`CAM_RST`、`CAM_SDA`、`CAM_SCL`、`CAM_1V8`、`CAM_2V8` 等网络。
- 但当前仓库尚未形成稳定的软件 pin define，且这一页不是当前 demo 的主路径，所以这部分暂不纳入主映射表。

## 3. 初始化与时序要求

### 墨水屏

- 先初始化主 I2C：`GPIO7/8`。
- 再初始化 `PCA9535(0x20)`，因为 `EP_OE`、`EP_MODE`、`TPS_PWRUP`、`VCOM_CTRL`、`TPS_WAKEUP` 都挂在扩展 IO 上。
- 现有 `FastEPD` 的上电顺序是：
  - `EP_OE=1`
  - `EP_MODE=1`
  - `TPS_WAKEUP=1`
  - `TPS_PWRUP=1`
  - `VCOM_CTRL=1`
  - 等待 `TPS_PWR_GOOD`
  - 通过 `TPS651851(0x68)` 打开各路输出
- 墨水屏真正的行扫描脚仍是 `GPIO13/24/25/26/36 + GPIO27~34`，这些是高速控制路径，不能替换成扩展 IO。

### 触摸

- 触摸中断是 `GPIO3`，复位是 `PCA9535 IO0`。
- 原理图文本层直接给出了 `CTP_SDA/SCL/INT/RST` 网络；结合当前板级验证，已确认当前 T5-P4 E-paper 板卡使用 `GT911`，地址为 `0x5D`。
- 仓库里保留的 `touch_ft5536` 更适合作为历史/兼容性示例；当前板级实现应优先按 `touch_gt911` 的复位与选址流程处理。

### HDMI

- `LT8912B` 初始化前，需要先打开发给桥接芯片的 `1V8_EN`，再拉起 `HDMI_EN`，最后做 `HDMI_RST`。
- 配置寄存器走主 I2C `GPIO7/8`，显示器 DDC 走 `GPIO9/10`。
- 当前仓库代码额外打开了 `ESP32-P4` 的 DSI PHY LDO，这属于 SoC 级电源配置，不是原理图的普通 GPIO 开关。

## 4. 软件实现约束

- `GPIO7/8` 是核心共享 I2C，总线上同时挂了电池、电源、扩展 IO、音频、HDMI 桥接和可能的触摸控制器，初始化时不要重复随意改速率或改引脚。
- `PCA9535 IO14/IO15` 在板级定义里应视为输入：
  - `IO14 -> TPS_PWR_GOOD`
  - `IO15 -> TPS_INT`
- 墨水屏 `EP_OE` / `EP_MODE` 不是主控直出脚，而是扩展 IO 控制脚。
- `BOARD_PCA_12_1V8_EN` 对 HDMI 相关外设很关键，不能默认省略。
- 音频信号名 `ASDOUT/DSDIN` 是按 `ES8311` 侧命名；实际写代码时应固定使用 `ESP32 DOUT=GPIO39`、`ESP32 DIN=GPIO41`，不要被命名视角混淆。

## 5. 已确认 / 待确认

### 已确认

- 主 I2C：`GPIO7/8`
- 触摸控制器：`GT911 @ 0x5D`
- 触摸中断：`GPIO3`
- PCA9535 中断：`GPIO5`
- HDMI 中断：`GPIO4`
- C6 唤醒/复位：`GPIO6/54`
- SD SPI：`GPIO44/45/46/47`
- 墨水屏并口和行驱动控制线：`GPIO13/24/25/26/27~34/36`
- 音频 I2S：`MCLK=43`、`BCLK=42`、`LRCK=40`、`DOUT=39`、`DIN=41`
- I2C 地址：
  - `PCA9535 = 0x20`
  - `ES8311 = 0x18`
  - `BQ27220 = 0x55`
  - `BQ25896 = 0x6B`
  - `TPS651851 = 0x68`
  - `LT8912B = 0x48` 由代码与实际 probe 共同确认

### 待确认

- 摄像头接口在当前工程里的最终 GPIO 归属与软件抽象。

## 6. 建议优先维护的 example

- `examples/i2c_scan`
- `examples/touch_gt911`
- `examples/FastEPD/*`
- `examples/hdmi_video_renderer`
- `examples/es8311/*`
