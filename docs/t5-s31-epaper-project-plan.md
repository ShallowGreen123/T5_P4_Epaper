# T5-S31 4.7 英寸墨水屏项目规划与 Rev.A 引脚建议

> 状态：方案评审稿，不可直接用于投板  
> 主控：ESP32-S31，不是 ESP32-S3  
> 依据：ESP32-S31 Series Datasheet Pre-release v0.5（2026-07-13）、现有 T5-P4 V0.2 设计、`D:\dgx\T-Qst H810.pdf`、仓库内两份 E0470 屏资料

## 1. 先冻结的关键信息

1. 屏幕实际分辨率是 **684 x 1216**，不是 648 x 1216。
2. `E0470A01-AF-CF` 是 40 Pin、D0-D15 的 16 位屏；`E0470A03-AF-S` 是另一块 34 Pin、D0-D7 的 8 位屏。两份资料不能混用。
3. A01 当前只有机械图和 FPC 定义，没有完整 AC 时序、上掉电时序、波形表和批次 VCOM。供应商资料补齐前，不能冻结 EPD 时钟和量产波形。
4. “ES720”需要确认完整料号。若实际是常用的 **ES7210**，它是多通道 ADC，只负责麦克风采集，不能独立完成耳机和扬声器播放。
5. 主动笔控制器、艾为线性马达驱动、SX1262 工作频段和模块完整料号都需要在原理图冻结前确定。

## 2. 推荐系统架构

```text
USB-C
  +-- 充电/Power Path -- 电池/电量计 -- VSYS
  |                                  +-- 3V3_D: S31 / Flash / LoRa
  |                                  +-- 3V3_A: Audio / IMU / Pen
  |                                  +-- EPD PMIC: VGH/VGL/VPOS/VNEG/VCOM
  |                                  +-- Frontlight Boost
  |                                  +-- Speaker Amp Rail
  |
  +-- ESP32-S31 USB2 HS OTG (封装专用 USB_DP/USB_DM)

ESP32-S31NRV16 + 外接 32/64 MB Quad Flash
  +-- LCD_CAM 16-bit --> E0470A01-AF-CF
  +-- I2S/TDM --------> ES720/ES7210 双麦 ADC + 播放 Codec/DAC
  +-- SPI2 -----------> SX1262 模块
  +-- I2C SDA=GPIO3 ---> EPD PMIC / PCA9535 / Audio / H810 / Haptic / Pen
  |      SCL=GPIO2  +--> QWIIC 外部扩展
  +-- UART0 ----------> 4 Pin 1.0 mm UART
  +-- PCA9535 --------> 低速控制、按键和外部扩展 GPIO
```

推荐选 `ESP32-S31NRV16`。16 MB 内置 PSRAM 对 4 bpp EPD 双缓冲、字体缓存和双麦音频缓存已经充足；只有需要大模型或大容量解码缓存时才上 NRV32。S31 没有内置应用 Flash，必须外接 Flash。

## 3. ESP32-S31 Rev.A 引脚分配

### 3.1 主控直连 GPIO

| GPIO | 功能 | 外设/方向 | 说明 |
| ---: | --- | --- | --- |
| 0 | XIO_INT | PCA9535 -> S31 | 常开域唤醒中断，低有效；不装 32.768 kHz 晶振 |
| 1 | USER_KEY | 按键输入 | 低有效，可作唤醒键；不装 32.768 kHz 晶振 |
| 2 | I2C_SCL | 板内总线 + QWIIC | 固定使用此脚，HP-I2C 经 GPIO Matrix 路由 |
| 3 | I2C_SDA | 板内总线 + QWIIC | 固定使用此脚，HP-I2C 经 GPIO Matrix 路由 |
| 4 | FL_PWM1 | 前置光 | LEDC PWM，建议 20 kHz 以上 |
| 5 | FL_PWM2 | 前置光 | 双灯串独立调光 |
| 6 | EXP_GPIO1 | 原生扩展 GPIO | 用户可用 |
| 7 | EXP_GPIO2 | 原生扩展 GPIO | 用户可用 |
| 8 | EPD_D0 | LCD_CAM 输出 | 原生 LCD_DATA0 |
| 9 | EPD_D1 | LCD_CAM 输出 | 原生 LCD_DATA1 |
| 10 | EPD_D2 | LCD_CAM 输出 | 原生 LCD_DATA2 |
| 11 | EPD_D3 | LCD_CAM 输出 | 原生 LCD_DATA3 |
| 12 | EPD_D4 | LCD_CAM 输出 | 原生 LCD_DATA4 |
| 13 | EPD_D5 | LCD_CAM 输出 | 原生 LCD_DATA5 |
| 14 | EPD_D6 | LCD_CAM 输出 | 原生 LCD_DATA6 |
| 15 | EPD_D7 | LCD_CAM 输出 | 原生 LCD_DATA7 |
| 16 | EPD_D8 | LCD_CAM 输出 | 原生 LCD_DATA8 |
| 17 | EPD_D9 | LCD_CAM 输出 | 原生 LCD_DATA9 |
| 18 | EPD_D10 | LCD_CAM 输出 | 原生 LCD_DATA10 |
| 19 | EPD_D11 | LCD_CAM 输出 | 原生 LCD_DATA11 |
| 20 | LORA_SCK | SPI2 | 原生 SPI2_CK |
| 21 | LORA_MOSI | SPI2 | 原生 SPI2_D |
| 22 | LORA_MISO | SPI2 | 原生 SPI2_Q |
| 23 | LORA_NSS | SX1262 | 原生 SPI2_CS |
| 24 | LORA_BUSY | SX1262 输入 | 不要放到 IO 扩展器 |
| 25 | LORA_DIO1 | SX1262 中断 | 不要放到 IO 扩展器 |
| 33 | EPD_D12 | LCD_CAM 输出 | 启动默认是 USB Serial/JTAG 脚，面板必须保持 OE 关闭 |
| 34 | EPD_D13 | LCD_CAM 输出 | 同上；USB-C 使用专用 USB HS 脚，不用 GPIO33/34 |
| 35 | EPD_D14 | LCD_CAM 输出 | 原生 LCD_DATA14 |
| 36 | EPD_D15 | LCD_CAM 输出/绑带 | 3.3 V Flash 时用 10 k 上拉；不得再接其他强上下拉 |
| 38 | EPD_XSTL | EPD 输出 | 每行起始脉冲，需与数据时钟严格同步 |
| 39 | EPD_XLE | EPD 输出 | Source latch enable |
| 40 | EPD_XCL | LCD_CAM PCLK | 原生 LCD_PCLK，优先短线和连续参考地 |
| 42 | EPD_CKV | EPD 输出 | Gate clock |
| 43 | EPD_SPV | EPD 输出 | Gate start pulse |
| 44 | EPD_XOE | EPD 输出 | 外加 100 k 下拉，复位和掉电期间强制关闭输出 |
| 45 | EPD_BORDER | EPD 输出 | 最终波形需按 A01 供应商资料确认 |
| 46 | AUDIO_MCLK | ES720/Codec | I2S 主时钟 |
| 47 | AUDIO_BCLK | ES720/Codec | I2S/TDM 位时钟 |
| 48 | AUDIO_LRCK | ES720/Codec | I2S/TDM 帧时钟 |
| 49 | AUDIO_DOUT | S31 -> 播放 Codec | 扬声器/耳机播放数据 |
| 50 | AUDIO_DIN | ES720/ES7210 -> S31 | 双麦 TDM 输入 |
| 51 | HAPTIC_INT | 艾为马达驱动输入 | 波形结束/故障中断 |
| 52 | PEN_INT | 主动笔控制器输入 | 低延迟直连，不经过 IO 扩展器 |
| 53 | EXP_GPIO0 | 原生扩展 GPIO | 用户可用 |
| 54 | JTAG_TDO / EXP1 | 调试/扩展 | 调试期只作 JTAG |
| 55 | JTAG_TCK / EXP2 | 调试/扩展 | 调试期只作 JTAG |
| 56 | JTAG_TDI / EXP3 | 调试/扩展 | 调试期只作 JTAG |
| 57 | JTAG_TMS / EXP4 | 调试/扩展 | 调试期只作 JTAG |
| 58 | UART0_TX | UART 4 Pin | ROM 下载和日志输出 |
| 59 | UART0_RX | UART 4 Pin | ROM 下载和日志输入 |
| 60 | STRAP_KEEP_HIGH | 启动绑带 | 10 k 上拉，禁止分配给按键或外部接口 |
| 61 | BOOT | Boot 按键 | 10 k 上拉，按键接地；GPIO60 同时保持高 |

这张表已分配除 Flash 专用脚和 GPIO37 绑带脚之外的全部可用 GPIO，没有未命名的直连余量。新增硬实时信号时必须重新评审；普通 Reset/Enable 和低速扩展统一进入 PCA9535。GPIO54-57 与 JTAG 只能二选一，不能把它们统计成两组资源。

### 3.2 必须保留的专用脚

| S31 信号 | 用途 | 处理 |
| --- | --- | --- |
| SPICS/SPIQ/SPIWP/SPIHD/SPICLK/SPID | 外接启动 Flash | 专线、等长、禁止复用；对应数据手册封装 Pin 36-42 |
| USB_DP/USB_DM | USB 2.0 HS OTG | 对应封装 Pin 44/45，不是 GPIO44/45；按 90 ohm 差分布线 |
| CHIP_PU | Reset | 10 k 上拉，复位键下拉，满足至少 1 ms 低电平 |
| GPIO37 | JTAG strap | 10 k 下拉；烧录 `EFUSE_JTAG_SEL_ENABLE=1` 后选择物理 JTAG |
| GPIO60/GPIO61 | 启动模式 | 正常启动 61=1；下载模式 61=0、60=1；禁止 60=0 且 61=0 |

`GPIO36` 同时是 EPD_D15 和 VDD_SPI 电压绑带脚。建议外接 3.3 V Quad Flash，并给 GPIO36 配 10 k 上拉；EPD 总线侧不得有上下拉。量产后也可以用 eFuse 固化 Flash 电压配置，但首版仍保留硬件绑带和测试点。

GPIO20-25 属于可切换的 `VDDPST_SD` IO 电源域。SX1262 模块按 3.3 V IO 设计时，必须把该域固定/初始化为 3.3 V，并验证从复位释放到软件配置期间不会出现 1.8 V 或过压状态。

GPIO37 在 S31 v0.5 中没有内部上下拉，禁止悬空。上述 10 k 下拉是为后续物理 JTAG 预留；默认 eFuse 状态下 JTAG 仍来自 USB Serial/JTAG，只有烧录 `EFUSE_JTAG_SEL_ENABLE=1` 后 GPIO37=0 才选择 GPIO54-57。烧 eFuse 是不可逆操作，必须放到安全启动和量产策略评审之后。

### 3.3 PCA9535 低速 IO 建议

PCA9535 的 A0/A1/A2 全部接地，7-bit 地址固定为 `0x20`，`INT` 开漏输出接 GPIO0，并在常开 3.3 V 电源域侧上拉。

| 扩展 IO | 功能 | 方向/默认状态 |
| --- | --- | --- |
| P0.0 | EPD_MODE | 输出，默认低 |
| P0.1 | TPS_WAKEUP | 输出，默认低 |
| P0.2 | TPS_PWRUP | 输出，默认低 |
| P0.3 | VCOM_CTRL | 输出，默认低 |
| P0.4 | TPS_PWR_GOOD | 输入 |
| P0.5 | TPS_INT | 输入 |
| P0.6 | LORA_NRESET | 输出，默认低，初始化后释放 |
| P0.7 | PEN_RESET | 输出，默认低 |
| P1.0 | AUDIO_PA_EN | 输出，默认低 |
| P1.1 | AUDIO_PATH_SEL/CODEC_RST | 输出，默认静音 |
| P1.2 | HP_DET | 耳机插入检测输入 |
| P1.3 | HAPTIC_RESET | 输出，默认低 |
| P1.4 | H810_INT | 输入，经 XIO_INT 唤醒主控 |
| P1.5 | CHARGER_INT | 输入 |
| P1.6 | XIO0 | 外部低速 GPIO |
| P1.7 | XIO1 | 外部低速 GPIO |

PCA9535 上电后所有端口为输入态，输出寄存器默认值为高。表中的“默认低”必须由每根关键 Reset/EN 信号外加 47-100 k 下拉实现；固件初始化时先把相应 Output Port 位写 0，再把 Configuration 位切为输出，避免产生高脉冲。不能依赖 I2C 软件及时执行来保证上电安全状态。P1.6/P1.7 只有普通数字输入输出能力，不适合高速 SPI、精确定时 PWM 或 ADC。

EPD_XOE 必须直连 S31 并有硬件下拉，不能只依赖 I2C 扩展器。这样即使 I2C 异常，仍能先关闭面板输出，再按要求关闭 VCOM 和高压电源。

## 4. EPD 和前置光

### 4.1 A01 40 Pin FPC 核对

| Pin | Signal | Pin | Signal |
| ---: | --- | ---: | --- |
| 1 | VGL | 21 | D7 |
| 2 | NC | 22 | VSS |
| 3 | VGH | 23 | D8 |
| 4 | NC | 24 | D9 |
| 5 | VDD | 25 | D10 |
| 6 | MODE | 26 | D11 |
| 7 | CKV | 27 | D12 |
| 8 | SPV | 28 | D13 |
| 9 | VSS | 29 | D14 |
| 10 | VCOM | 30 | D15 |
| 11 | VDD | 31 | XSTL |
| 12 | VSS | 32 | XLE |
| 13 | XCL | 33 | XOE |
| 14-20 | D0-D6 | 34 | VDD |
| 35 | NC | 36 | VPOS |
| 37 | NC | 38 | VNEG |
| 39 | NC | 40 | BORDER |

建议沿用 `TPS65185/TPS651851` 类 EPD PMIC，但 VCOM 必须按每批屏标签或 OTP 数据设置。首次点屏可把现有工程的 20 MHz、162 clocks/line 和 10 dummy clocks 作为实验起点，不能把 A03 的 8 位时序当作 A01 的量产依据。

上电时先稳定 EPD_VDD，再开启正负高压和 VCOM，最后释放 XOE；下电顺序相反。现有 A03 资料要求 XOE 拉低至少 12 us 后才能关闭 VPOS/VNEG；A01 首次验证可暂取 100 us 保护间隔，最终仍以 A01 正式资料为准。

数据 D0-D15、XCL、XSTL、XLE、CKV、SPV 建议在 S31 端串 22-33 ohm 阻尼。XCL 最短，16 根数据长度差优先控制在 10 mm 内。EPD 高压区与麦克风、音频时钟、S31/SX1262 天线保持距离。

若待机漏电要求严格，D0-D15 前增加带 `Ioff` 的总线缓冲器并由 EPD_VDD/OE 控制，可避免屏逻辑掉电时从 GPIO 反向供电，也能隔离 GPIO36 绑带脚。

前置光推荐双通道升压驱动（现有 TPS61150 架构可复用）：

- 两路 PWM 独立控制，支持亮度均匀性校正。
- PWM 建议高于 20 kHz，避免音频录音收到可听啸叫。
- 先取得 LED 串数量、VF、额定电流和热设计数据，再定电感、肖特基和限流电阻。
- 前置光、音频功放和 EPD 高压不能同时无约束启动，固件做峰值功率调度。

## 5. 音频方案

若“ES720”实际为 `ES7210`，推荐：

```text
MIC_L/MIC_R -> ES7210 ADC --TDM DIN--> S31
S31 --I2S DOUT--> Stereo Codec/DAC -> L/R Headphone
                                  +-> Mono Mix -> Class-D Amp -> Speaker
```

ES7210 只做双麦 ADC；耳机和扬声器必须补一个播放 DAC/Codec。希望耳机保持立体声时，可选带耳机驱动的立体声 Codec，再接独立单声道 Class-D 功放。若只用 ES8311，耳机通常只能做双声道同播的单声道方案，产品定义需提前接受这一点。

设计建议：

- ES720/ES7210、播放 Codec 共用 MCLK/BCLK/LRCK；ADC SDOUT 和 DAC SDIN 分开。
- 3.5 mm 接口先确定 TRS 耳机还是 CTIA TRRS 耳麦；本方案默认 TRS 立体声耳机带插入检测。
- 插入耳机先硬件静音功放，再切换音频路径，避免爆音。
- 双麦尽量放在机身上边缘，两孔间距 35-60 mm，走线等长，远离扬声器腔体、LRA 和升压电感。
- 音频模拟地局部完整，不做割裂地平面；用磁珠/低噪声 LDO 隔离数字电源噪声。

## 6. SX1262

优先使用带 TCXO、RF switch 和匹配网络的成品模块。这样 S31 只需要 `SCK/MOSI/MISO/NSS/BUSY/DIO1/NRESET`；DIO2 控 RF switch、DIO3 控 TCXO 在模块内处理。

- 频段必须按销售区域冻结为 433/470/868/915 MHz 之一。
- `BUSY` 和 `DIO1` 直连主控，`NRESET` 可由 PCA9535 控制。
- LoRa 和 Wi-Fi/Bluetooth 发射不要同时进行；电源和任务层都做互斥调度。
- S31 2.4 GHz 天线与 LoRa 天线放在机身相反端，两处都避开 EPD 金属背板、排线、螺丝和扩展连接器。

## 7. H810 IMU

`T-Qst H810.pdf` 中包含：

- `QMI8658B` 六轴 IMU，可用 I2C 或 SPI。
- `QMC6309` 磁力计，只接在 I2C 总线上。
- P1 五针：1=SDA、2=SCL、3=3V3、4=GND、5=INT。
- P2 四针：1=CS、2=SCK、3=MOSI、4=MISO，只适合 QMI8658B SPI，不能替代 QMC6309 的 I2C。
- QMI8658B 地址通过 SB1/SB2 选择 `0x6A` 或 `0x6B`。

产品板建议只用 P1/I2C。H810 自带 10 k I2C 上拉，主板上拉建议做 DNP/可选，按整条总线电容实测后选择 2.2-4.7 k 的总等效阻值。

H810 把 QMI8658B 的 INT1/INT2 经 1N4148 二极管合并为一个 INT，存在约 0.6 V 压降。S31 的 3.3 V VIH 下理论余量不大，量产版更建议只把所需事件映射到 INT1 并直接引出，或改成正确的开漏合并电路。若保留现电路，固件必须把 QMI 中断配置成与二极管方向一致的高有效模式并做高低温验证。

QMC6309 会被扬声器磁钢和线性马达严重干扰。H810 最好放在远离两者的顶部小板/软板上；播放声音或震动期间暂停磁力计融合，量产做硬铁和软铁校准。若产品不需要绝对航向，删除磁力计会比后期校准更可靠。

## 8. 线性马达

艾为料号未定时，按 `AW86224/AW86225` 一类 I2C LRA 驱动预留：

- I2C（SDA=GPIO3、SCL=GPIO2）配置，`HAPTIC_INT=GPIO51`，RESET 由 PCA9535 控制。
- 驱动电源和 LRA 回流远离双麦、磁力计和晶振。
- 先冻结 LRA 的谐振频率、额定电压、线圈电阻和目标加速度，再选驱动型号和升压参数。
- PCB 预留马达端 TVS/RC、驱动电源 10 uF 以上局部储能和电流测试点。

## 9. 外部接口和按键

QWIIC 和 UART 必须做成两个独立的 JST-SH 1.0 mm 4 Pin，不能共用一个“可切换”接口。

### QWIIC

| Pin | Signal |
| ---: | --- |
| 1 | GND |
| 2 | 3V3_EXT_SW |
| 3 | I2C_SDA / GPIO3 |
| 4 | I2C_SCL / GPIO2 |

QWIIC 与板内器件共用同一总线。为避免外接模块短路或掉电拉低总线导致 EPD/电源管理失联，建议在 QWIIC 连接器前增加可关断的双向 I2C 缓冲/隔离器，并把外部 3.3 V 做限流和负载开关。

### UART

| Pin | Signal |
| ---: | --- |
| 1 | GND |
| 2 | 3V3_EXT_SW |
| 3 | TXD / GPIO58 |
| 4 | RXD / GPIO59 |

针号必须再按选定连接器的顶视图和线材方向核对，丝印同时标信号，不只标数字。UART 是 3.3 V 电平，不是 RS-232。

### 顶部/背部扩展口（建议 2 x 7、1.27 mm）

| Pin | Signal | Pin | Signal |
| ---: | --- | ---: | --- |
| 1 | GND | 2 | 3V3_EXT_SW |
| 3 | GPIO6 | 4 | GPIO7 |
| 5 | GPIO53 | 6 | GPIO54/JTAG_TDO |
| 7 | GPIO55/JTAG_TCK | 8 | GPIO56/JTAG_TDI |
| 9 | GPIO57/JTAG_TMS | 10 | XIO0 / PCA9535 P1.6 |
| 11 | XIO1 / PCA9535 P1.7 | 12 | GND |
| 13 | I2C_SDA / GPIO3 | 14 | I2C_SCL / GPIO2 |

GPIO54-57 在开发阶段保留给 JTAG，量产固件稳定后才允许当扩展 GPIO。外接接口使用可关断的 `3V3_EXT_SW`，加 ESD、每根信号 22-100 ohm 串阻和过流限制。位置优先放背部中下方，不要侵入 S31 或 LoRa 天线净空区。

按键和开关：

- `USER_KEY`：GPIO1 下拉触发，可唤醒。
- `RESET`：CHIP_PU 下拉，至少保持 1 ms。
- `BOOT`：GPIO61 下拉；GPIO60 永远保持高，避免非法启动组合。
- 电源拨动开关只控制主电源 Load Switch/稳压器 EN，不让整机峰值电流直接通过小型拨动开关。充电和电量计可保持供电，关机时切断 S31、EPD、前置光、音频和 LoRa 主电源。

## 10. 电源和 PCB 优化

建议电源树：

| 电源 | 建议能力 | 负载 |
| --- | ---: | --- |
| VSYS/电池 Power Path | 峰值 2.5 A 以上 | 全机输入 |
| 3V3_D Buck | 连续 1.5 A、峰值 2 A | S31、Flash、LoRa、数字 IO |
| 3V3_A LDO/滤波支路 | 300-500 mA | Audio、Pen、IMU、Haptic 控制 |
| EPD_PMIC | 按屏规格 | VGH/VGL/VPOS/VNEG/VCOM |
| Frontlight Boost | 按 LED 串规格 | 两路前置光 |
| Speaker Rail | 按功放和喇叭功率 | Class-D 功放 |

布局分区建议：

1. S31 RF/Flash/晶振区按乐鑫 S31 参考设计原样优先，QFN80 裸芯方案不要自行简化 RF 匹配和电源去耦。
2. EPD PMIC 和前置光升压放在屏 FPC 附近，但远离双麦和天线。
3. 双麦、播放 Codec 和耳机座组成安静音频区；扬声器功放靠近喇叭，不让大电流穿过音频区。
4. SX1262 靠近自己的天线座，RF 走线短，模块下方按供应商要求留地或禁布。
5. H810 远离喇叭、马达、电感、磁吸结构和铁螺丝。
6. USB HS、EPD XCL、Flash、I2S 时钟都需要连续参考地；不要用分割地平面破坏回流。

## 11. 低功耗目标

墨水屏保持画面不耗电，休眠时应关闭 EPD 高压和 VCOM。建议系统深睡目标小于 150 uA：

- S31 Deep-sleep，LP GPIO0 接收 PCA9535 唤醒。
- QMI8658B 保留低功耗运动检测；QMC6309 按需关断。
- ES720/Codec/功放、前置光、EPD PMIC、主动笔和外部 3V3 扩展电源全部关断。
- SX1262 进入 sleep，不要只保持 standby。
- PCA9535 与承担运动唤醒的 QMI8658B 放在常开电源域，其余 H810/Audio/Pen 电路按能力进入 shutdown 或切断电源。
- 对所有被切断电源但仍连接 I2C 的器件，确认 SDA/SCL 具备掉电高阻/Ioff；否则增加总线开关，避免 I2C 上拉反向供电。
- 首版为每路电源加 0 ohm/电流测试焊盘，分别测关断漏电。

## 12. 固件结构建议

- `bsp_s31`: GPIO、I2C、SPI、I2S、USB、电源域和板级初始化。
- `epd_bus`: LCD_CAM I8080 16-bit + GDMA，只处理时序和行传输。
- `epd_power`: TPS65185、VCOM、XOE 和严格上掉电状态机。
- `display`: 4 bpp framebuffer；684 x 1216 单缓冲约 416 KB，双缓冲约 832 KB。避免长期使用 RGB565 全屏双缓冲。
- `audio`: ES720/ES7210 双麦 TDM、播放 Codec、耳机检测、功放静音和回声处理。
- `radio_lora`: SX1262 状态机；与 Wi-Fi/Bluetooth TX 做电源和射频互斥。
- `sensors`: QMI8658B、QMC6309、主动笔和 haptic。
- `power_manager`: Activity vote、自动休眠、外设电源域和唤醒原因。

S31 当前数据手册仍为 Preliminary v0.5，SDK 应固定到明确支持 `esp32s31` 的 ESP-IDF commit，不要在开发中途无审查升级 master。

## 13. 项目阶段

1. **规格冻结**：补齐 A01 时序/波形/VCOM、主动笔、ES720、艾为驱动、LoRa 频段、耳机类型、喇叭/LRA 和电池规格。
2. **最小系统样板**：S31 + Flash + USB/UART/JTAG + 电源，先验证启动、下载、PSRAM 和 RF。
3. **显示样板**：只加入 EPD PMIC、40 Pin FPC 和前置光，验证 16 位总线、上掉电、VCOM、全刷/局刷和温度波形。
4. **功能 EVT**：加入 Audio、SX1262、H810、Haptic、主动笔和接口，完成产测固件。
5. **DVT**：功耗、ESD、EMI、射频共存、音频底噪、磁干扰、跌落和高低温。
6. **PVT**：治具测试 Flash/PSRAM、EPD 信号、高压、双麦、耳机、扬声器、LoRa、IMU、马达、按键和休眠电流。

## 14. 原理图冻结前的阻塞项

| 优先级 | 阻塞项 | 不解决的后果 |
| --- | --- | --- |
| P0 | E0470A01 完整 datasheet、waveform、VCOM | 可能损屏、鬼影或无法稳定刷新 |
| P0 | 主动笔控制器和 FPC 定义 | 无法确定电源、总线和中断 |
| P0 | ES720 完整料号 | 无法确定音频时钟、地址和模拟前端 |
| P0 | 艾为驱动完整料号 + LRA 规格 | 无法定电源和外围参数 |
| P0 | SX1262 模块型号和频段 | 无法定天线、匹配和认证版本 |
| P1 | TRS/TRRS 耳机定义、喇叭功率 | 无法冻结 Codec/功放和插座 |
| P1 | 电池容量、最大尺寸和充电功率 | 无法冻结电源路径和热设计 |
| P1 | S31 silicon revision、Flash 电压和 SDK commit | 启动绑带和软件基线可能变化 |
