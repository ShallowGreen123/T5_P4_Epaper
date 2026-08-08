# T5-P4 E-paper V0.2 与 V0.3 引脚差异

## 1. 对比范围

本文对比以下两份引脚定义：

- V0.2：[`docs/pinmap.md`](pinmap.md)
- V0.3：[`docs/pinmap_v0.3.md`](pinmap_v0.3.md)

当前工程以 **V0.3 原理图和 V0.3 引脚映射为准**。V0.3 与 V0.2 在 XL9555 控制引脚、部分 ESP32-P4 直连 GPIO 和扩展接口定义上不兼容，不能直接复用 V0.2 的板级宏。

## 2. 关键不兼容变化

| 功能 | V0.2 | V0.3 | 软件影响 |
| --- | --- | --- | --- |
| XL9555 中断 | `INT -> ALL_INT -> GPIO6` | `INT` 仅上拉，未接 ESP32-P4 | V0.3 不能再通过 GPIO6 接收 XL9555 中断 |
| 传感器中断 | `XL9555 P12 = SEN_IRQ` | `GPIO6 = SEN_IRQ` | 改为 ESP32-P4 GPIO6 直连输入 |
| HDMI 复位 | `XL9555 P10` | `XL9555 P00` | 必须修改 XL9555 位号/掩码 |
| HDMI 电源使能 | `XL9555 P11 = HDMI_EN` | `XL9555 P01 = HDMI_PWR_EN` | 必须修改位号；V0.3 的信号用于 HDMI 1.8 V 电源使能 |
| 触摸复位 | `XL9555 P00` | `XL9555 P02` | GT911 复位控制位发生变化 |
| SD 卡供电 | 无独立 XL9555 控制 | `XL9555 P03 = SD_VDD_EN` | V0.3 挂载 SD 卡前需要正确使能供电 |
| 音频功放使能 | `XL9555 P06 = SHUTDOWN` | `XL9555 P04 = AUDIO_EN` | 功放控制位和信号名称均变化 |
| 音频路径选择 | `XL9555 P07 = AUDIO_SEL` | 已移除 | 删除 V0.2 的音频路径切换操作 |
| EPD 背光 PWM2 | `GPIO54` | `GPIO52` | PWM 输出 GPIO 必须修改 |
| ESP32-C6 复位 | `XL9555 P13` | `GPIO54` 直连 | 改用 ESP32-P4 GPIO54 控制，不再访问 XL9555 |
| ESP32-C6 唤醒 | `XL9555 P14` | 已移除 | 删除 `C6_WAKEUP` 控制逻辑 |

## 3. XL9555 完整对照

XL9555 的 I2C 地址在两个版本中均为 `0x22`，但端口功能发生了整体调整。

| XL9555 端口 | V0.2 | V0.3 | 变化 |
| --- | --- | --- | --- |
| P00 | `T_RST` | `HDMI_RST` | 功能改变 |
| P01 | `CC_SW0` | `HDMI_PWR_EN` | 功能改变 |
| P02 | `CC_SW1` | `T_RST` | 功能改变 |
| P03 | `LR_RST` | `SD_VDD_EN` | 功能改变 |
| P04 | `NRF_CE` | `AUDIO_EN` | 功能改变 |
| P05 | NC | NC | 不变 |
| P06 | `SHUTDOWN` | NC | V0.3 移除 |
| P07 | `AUDIO_SEL` | NC | V0.3 移除 |
| P10 | `HDMI_RST` | NC | HDMI 复位移至 P00 |
| P11 | `HDMI_EN` | NC | HDMI 电源使能移至 P01 |
| P12 | `SEN_IRQ` | NC | 传感器中断移至 GPIO6 直连 |
| P13 | `C6_RST/EN` | NC | C6 复位移至 GPIO54 直连 |
| P14 | `C6_WAKEUP` | NC | V0.3 移除 |
| P15 | NC | NC | 不变 |
| P16 | NC | NC | 不变 |
| P17 | NC | NC | 不变 |

V0.3 仅使用 P00 至 P04：

```text
P00 HDMI_RST
P01 HDMI_PWR_EN
P02 T_RST
P03 SD_VDD_EN
P04 AUDIO_EN
P05-P17 NC
```

> 警告：V0.2 的 XL9555 位号、位掩码和默认输出值不能用于 V0.3。复用旧的整端口写入值可能误操作 HDMI 电源、HDMI 复位、触摸复位、SD 供电或音频功放。

## 4. ESP32-P4 直连 GPIO 变化

| GPIO | V0.2 | V0.3 |
| --- | --- | --- |
| GPIO6 | `XL9555 INT / ALL_INT` | `SEN_IRQ` |
| GPIO52 | 扩展功能 `MODULE_EN / POWER_KEY` | `LED_PWM2` |
| GPIO54 | `LED_PWM2` | `C6_RST/EN` |

其余表中列出的主控直连信号保持不变，包括主 I2C、触摸中断、HDMI 中断、HDMI DDC、SD SPI 和 BOOT 引脚。

## 5. 各外设变化

### 5.1 HDMI / LT8912B

| 信号 | V0.2 | V0.3 |
| --- | --- | --- |
| 配置 I2C SDA/SCL | GPIO7 / GPIO8 | GPIO7 / GPIO8 |
| LT8912B INT | GPIO4 | GPIO4 |
| HDMI DDC SDA/SCL | GPIO9 / GPIO10 | GPIO9 / GPIO10 |
| LT8912B RST | XL9555 P10 | XL9555 P00 |
| HDMI 电源使能 | XL9555 P11 `HDMI_EN` | XL9555 P01 `HDMI_PWR_EN` |

HDMI 的通信 GPIO 没有变化，真正不兼容的是 XL9555 上的复位和电源控制位置。V0.3 的 HDMI 初始化代码必须使用 P01 控制 HDMI 1.8 V 电源，并使用 P00 执行 LT8912B 复位；具体上电顺序、有效电平和延时应以 V0.3 原理图及 LT8912B 驱动为准。

### 5.2 触摸 GT911

- 中断仍为 GPIO3。
- 复位从 XL9555 P00 移到 XL9555 P02。
- GT911 的实际 I2C 地址取决于复位期间 INT/RST 时序，初始化时不能只假定固定地址而忽略复位时序。

### 5.3 SD 卡

- SPI 信号保持不变：MISO GPIO44、SCK GPIO45、MOSI GPIO46、CS GPIO47。
- V0.3 新增 XL9555 P03 `SD_VDD_EN`，SD 初始化前需要按硬件要求打开供电。

### 5.4 音频

- ES8311 的 I2C 和 I2S GPIO 保持不变。
- 功放控制从 XL9555 P06 `SHUTDOWN` 改为 XL9555 P04 `AUDIO_EN`。
- V0.3 不再提供 XL9555 P07 `AUDIO_SEL`。

### 5.5 ESP32-C6

- SDIO GPIO14 至 GPIO19 保持不变。
- 复位由 XL9555 P13 改为 ESP32-P4 GPIO54 直连。
- V0.3 不再提供 `C6_WAKEUP` 网络。

### 5.6 J2 扩展接口

V0.2 文档使用 `LR_*`、`NFC_*`、`NRF_*`、`CC_*` 等预设功能别名。V0.3 原理图将 J2 定义为直接 GPIO/电源接口，不再承诺这些旧模块功能别名。

GPIO2、GPIO11、GPIO12、GPIO20、GPIO21、GPIO22、GPIO23、GPIO49、GPIO50 和 GPIO51 在 V0.3 仍有引出，但软件应按 J2 的直接 GPIO 定义使用，不应继续依赖 V0.2 的模块别名。

## 6. 保持不变的主要映射

| 模块 | 保持不变的引脚/映射 |
| --- | --- |
| EPD 数据 | GPIO27 至 GPIO34（D0 至 D7） |
| EPD 扫描控制 | CKH GPIO24、STH GPIO25、LEH GPIO26、CKV GPIO13、STV GPIO48 |
| EPD 背光 PWM1 | GPIO53 |
| PCA9535 | I2C 地址 `0x20`，EPD/TPS 端口映射保持不变 |
| ESP32-C6 SDIO | D0-D3 GPIO14-GPIO17、CLK GPIO18、CMD GPIO19 |
| ES8311 I2S | DSDIN GPIO39、LRCK GPIO40、ASDOUT GPIO41、BCLK GPIO42、MCLK GPIO43 |
| 主 I2C | SDA GPIO7、SCL GPIO8 |
| HDMI 通信 | INT GPIO4、DDC SDA GPIO9、DDC SCL GPIO10 |
| SD SPI | MISO GPIO44、SCK GPIO45、MOSI GPIO46、CS GPIO47 |

## 7. I2C 地址说明变化

两版文档中的主要 I2C 地址基本不变。V0.3 文档增加了以下注意事项：

- GT911 常用地址为 `0x5D`，实际地址取决于复位时序。
- 充电 IC 在原理图中标注为 `0x6B / 0x68`，应以实际 BOM 和实装器件为准。
- 如果充电 IC 实际使用 `0x68`，会与 TPS651851 的 `0x68` 地址相同，需要在硬件/BOM 确认阶段处理该冲突。

## 8. V0.2 到 V0.3 软件迁移检查清单

- [ ] 为 V0.3 使用独立的 board config，不复用 V0.2 的 XL9555 宏或端口默认值。
- [ ] 将 `HDMI_RST` 从 XL9555 P10 改到 P00。
- [ ] 将 `HDMI_EN` 从 XL9555 P11 改到 P01，并按 `HDMI_PWR_EN` 的电源控制语义初始化。
- [ ] 将 `T_RST` 从 XL9555 P00 改到 P02。
- [ ] 新增 XL9555 P03 `SD_VDD_EN` 控制。
- [ ] 将音频使能从 XL9555 P06 改到 P04，并移除 P07 `AUDIO_SEL` 操作。
- [ ] 将传感器中断从 XL9555 P12 改为 GPIO6 直连。
- [ ] 不再把 GPIO6 配置为 XL9555 中断。
- [ ] 将 `LED_PWM2` 从 GPIO54 改到 GPIO52。
- [ ] 将 C6 复位从 XL9555 P13 改为 GPIO54 直连。
- [ ] 移除 `C6_WAKEUP` 相关的 XL9555 P14 操作。
- [ ] 检查所有 XL9555 整端口写入和缓存初值，避免旧位图误控制 V0.3 外设。
- [ ] 对照 V0.3 BOM 确认充电 IC 型号和 I2C 地址。

## 9. 结论

V0.3 保留了 EPD、PCA9535、C6 SDIO、ES8311 I2S、SD SPI 以及 HDMI 通信总线的大部分映射，但重新设计了板级电源、复位和使能控制。迁移的核心不是兼容 V0.2，而是完整替换 XL9555 控制表，并同步修改 GPIO6、GPIO52 和 GPIO54 的用途。
