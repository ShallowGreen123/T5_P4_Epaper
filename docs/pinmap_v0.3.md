# T5-P4 E-paper 引脚映射（V0.3）

> 本文按 `hardware/T5-P4 E-paper V0.3.PDF` 整理。

> `docs/pinmap.md`、根目录 `readme.md` 和现有 BSP 仍主要描述 V0.2；如果与本文冲突，以 V0.3 原理图为准。

## 1. 主控直连 GPIO

| 功能 | 建议名 | GPIO | 来源 | 备注 |
| --- | --- | --- | --- | --- |
| 主 I2C SDA | PIN_I2C_SDA | 7 | 原理图 Page 2/4/6/7/8 | 全板共享 I2C |
| 主 I2C SCL | PIN_I2C_SCL | 8 | 原理图 Page 2/4/6/7/8 | 全板共享 I2C |
| 触摸中断 | PIN_TOUCH_INT | 3 | 原理图 Page 4/5 | GT911 `T_INT` |
| HDMI 中断 | PIN_HDMI_INT | 4 | 原理图 Page 5/7 | LT8912B `HDMI_INT` |
| PCA9535 中断 | PIN_PCA9535_INT | 5 | 原理图 Page 4/5 | EPD/TPS 扩展中断 |
| 传感器中断 | PIN_SENSOR_INT | 6 | 原理图 Page 5/8 | `SEN_IRQ`，V0.3 改为主控直连 |
| HDMI DDC SDA | PIN_HDMI_DDC_SDA | 9 | 原理图 Page 5/7 | HDMI EDID 总线 `DSDA` |
| HDMI DDC SCL | PIN_HDMI_DDC_SCL | 10 | 原理图 Page 5/7 | HDMI EDID 总线 `DSCL` |
| SD SPI MISO | PIN_SD_MISO | 44 | 原理图 Page 5/8 | `SPI_MISO` |
| SD SPI SCK | PIN_SD_SCK | 45 | 原理图 Page 5/8 | `SPI_SCK` |
| SD SPI MOSI | PIN_SD_MOSI | 46 | 原理图 Page 5/8 | `SPI_MOSI` |
| SD SPI CS | PIN_SD_CS | 47 | 原理图 Page 5 | `SD_CS` |
| BOOT | PIN_BOOT | 35 | 原理图 Page 5 | 上拉，低电平触发 |

> V0.3 的 XL9555 `INT` 仅由 `R72` 上拉，未连接 ESP32-P4；`GPIO6` 不再是 `XL9555 INT / ALL_INT`。

## 2. 墨水屏与背光

| 功能 | 建议名 | GPIO | 来源 | 备注 |
| --- | --- | --- | --- | --- |
| EPD D0 | PIN_EPD_D0 | 27 | 原理图 Page 4/5 | `EP_D0` |
| EPD D1 | PIN_EPD_D1 | 28 | 原理图 Page 4/5 | `EP_D1` |
| EPD D2 | PIN_EPD_D2 | 29 | 原理图 Page 4/5 | `EP_D2` |
| EPD D3 | PIN_EPD_D3 | 30 | 原理图 Page 4/5 | `EP_D3` |
| EPD D4 | PIN_EPD_D4 | 31 | 原理图 Page 4/5 | `EP_D4` |
| EPD D5 | PIN_EPD_D5 | 32 | 原理图 Page 4/5 | `EP_D5` |
| EPD D6 | PIN_EPD_D6 | 33 | 原理图 Page 4/5 | `EP_D6` |
| EPD D7 | PIN_EPD_D7 | 34 | 原理图 Page 4/5 | `EP_D7` |
| EPD CKH | PIN_EPD_CKH | 24 | 原理图 Page 4/5 | `EP_CKH` |
| EPD STH | PIN_EPD_STH | 25 | 原理图 Page 4/5 | `EP_STH` |
| EPD LEH | PIN_EPD_LEH | 26 | 原理图 Page 4/5 | `EP_LE` |
| EPD CKV | PIN_EPD_CKV | 13 | 原理图 Page 4/5 | `EP_CKV` |
| EPD STV | PIN_EPD_STV | 48 | 原理图 Page 4/5 | `EP_STV` |
| 背光 PWM1 | PIN_EPD_BL1 | 53 | 原理图 Page 4/5 | `LED_PWM1` |
| 背光 PWM2 | PIN_EPD_BL2 | 52 | 原理图 Page 4/5 | `LED_PWM2`，V0.2 为 GPIO54 |

> 两路 `LED_PWM` 均接到 `TPS61150ADRCR`。V0.3 将 `LED_PWM2` 移到 GPIO52，并将 GPIO54 改作 ESP32-C6 复位。

## 3. XL9555 扩展 IO

I2C 地址：`0x22`。V0.3 只使用 P00～P04；`INT` 未接到 ESP32-P4。

| XL9555 | 建议名 | 板级网络 | 方向 | 来源 | 备注 |
| --- | --- | --- | --- | --- | --- |
| P00 | PIN_XL9555_HDMI_RST | `HDMI_RST` | 输出 | 原理图 Page 7/8 | LT8912B 复位 |
| P01 | PIN_XL9555_HDMI_PWR_EN | `HDMI_PWR_EN` | 输出 | 原理图 Page 2/8 | HDMI 1.8 V 电源使能 |
| P02 | PIN_XL9555_TOUCH_RST | `T_RST` | 输出 | 原理图 Page 4/8 | GT911 复位 |
| P03 | PIN_XL9555_SD_VDD_EN | `SD_VDD_EN` | 输出 | 原理图 Page 5/8 | SD 卡电源开关使能 |
| P04 | PIN_XL9555_AUDIO_EN | `AUDIO_EN` | 输出 | 原理图 Page 6/8 | NS4150B 功放使能 |
| P05 | - | NC | - | 原理图 Page 8 | 当前未接出 |
| P06 | - | NC | - | 原理图 Page 8 | 当前未接出 |
| P07 | - | NC | - | 原理图 Page 8 | 当前未接出 |
| P10 | - | NC | - | 原理图 Page 8 | 当前未接出 |
| P11 | - | NC | - | 原理图 Page 8 | 当前未接出 |
| P12 | - | NC | - | 原理图 Page 8 | 当前未接出 |
| P13 | - | NC | - | 原理图 Page 8 | 当前未接出 |
| P14 | - | NC | - | 原理图 Page 8 | 当前未接出 |
| P15 | - | NC | - | 原理图 Page 8 | 当前未接出 |
| P16 | - | NC | - | 原理图 Page 8 | 当前未接出 |
| P17 | - | NC | - | 原理图 Page 8 | 当前未接出 |

## 4. PCA9535 扩展 IO（EPD/TPS）

I2C 地址：`0x20`，中断输出 `PCA_INT -> GPIO5`。

| PCA9535 | 建议名 | 板级网络 | 方向 | 来源 | 备注 |
| --- | --- | --- | --- | --- | --- |
| IO0_0 | - | NC | - | 原理图 Page 4 | 当前未接出 |
| IO0_1 | - | NC | - | 原理图 Page 4 | 当前未接出 |
| IO0_2 | - | NC | - | 原理图 Page 4 | 当前未接出 |
| IO0_3 | - | NC | - | 原理图 Page 4 | 当前未接出 |
| IO0_4 | - | NC | - | 原理图 Page 4 | 当前未接出 |
| IO0_5 | - | NC | - | 原理图 Page 4 | 当前未接出 |
| IO0_6 | - | NC | - | 原理图 Page 4 | 当前未接出 |
| IO0_7 | - | NC | - | 原理图 Page 4 | 当前未接出 |
| IO1_0 | PIN_PCA9535_EPD_OE | `EP_OE` | 输出 | 原理图 Page 4 | EPD 输出使能 |
| IO1_1 | PIN_PCA9535_EPD_MODE | `EP_MODE` | 输出 | 原理图 Page 4 | EPD 模式控制 |
| IO1_2 | - | NC | - | 原理图 Page 4 | 当前未接出 |
| IO1_3 | PIN_PCA9535_TPS_PWRUP | `TPS_PWRUP` | 输出 | 原理图 Page 4 | EPD 电源上电序列 |
| IO1_4 | PIN_PCA9535_VCOM_CTRL | `VCOM_CTRL` | 输出 | 原理图 Page 4 | EPD VCOM 控制 |
| IO1_5 | PIN_PCA9535_TPS_WAKEUP | `TPS_WAKEUP` | 输出 | 原理图 Page 4 | 唤醒 TPS651851 |
| IO1_6 | PIN_PCA9535_TPS_PWR_GOOD | `TPS_PWR_GOOD` | 输入 | 原理图 Page 4 | 电源良好检测 |
| IO1_7 | PIN_PCA9535_TPS_INT | `TPS_INT` | 输入 | 原理图 Page 4 | TPS651851 中断 |

## 5. ESP32-C6 连接

### SDIO

| 功能 | 建议名 | GPIO | 来源 |
| --- | --- | --- | --- |
| C6 D0 | PIN_C6_D0 | 14 | 原理图 Page 3/5 |
| C6 D1 | PIN_C6_D1 | 15 | 原理图 Page 3/5 |
| C6 D2 | PIN_C6_D2 | 16 | 原理图 Page 3/5 |
| C6 D3 | PIN_C6_D3 | 17 | 原理图 Page 3/5 |
| C6 CLK | PIN_C6_CLK | 18 | 原理图 Page 3/5 |
| C6 CMD | PIN_C6_CMD | 19 | 原理图 Page 3/5 |

### 控制

| 功能 | 建议名 | 映射 | 来源 | 备注 |
| --- | --- | --- | --- | --- |
| C6 `RST/EN` | PIN_C6_RST | GPIO54 | 原理图 Page 3/5 | ESP32-P4 直连 |

> V0.3 原理图没有 `C6_WAKEUP` 网络，C6 复位也不再经过 XL9555。

## 6. 音频 ES8311

| 功能 | 建议名 | GPIO | 来源 | 备注 |
| --- | --- | --- | --- | --- |
| ES8311 I2C SDA | PIN_ES8311_I2C_SDA | 7 | 原理图 Page 6 | 共用主 I2C |
| ES8311 I2C SCL | PIN_ES8311_I2C_SCL | 8 | 原理图 Page 6 | 共用主 I2C |
| I2S MCLK | PIN_ES8311_I2S_MCLK | 43 | 原理图 Page 5/6 | `I2S_MCLK` |
| I2S BCLK | PIN_ES8311_I2S_SCLK | 42 | 原理图 Page 5/6 | `I2S_SCLK` |
| I2S LRCK | PIN_ES8311_I2S_LRCK | 40 | 原理图 Page 5/6 | `I2S_LRCK` |
| ESP32 `DIN` <- ES8311 `ASDOUT` | PIN_ES8311_I2S_ASDOUT | 41 | 原理图 Page 5/6 | 原理图按 codec 视角命名 |
| ESP32 `DOUT` -> ES8311 `DSDIN` | PIN_ES8311_I2S_DSDIN | 39 | 原理图 Page 5/6 | 原理图按 codec 视角命名 |

### 控制

| 功能 | 映射 | 来源 | 备注 |
| --- | --- | --- | --- |
| 功放使能 | `XL9555 P04` | 原理图 Page 6/8 | `AUDIO_EN` |

> V0.3 已移除 V0.2 的 `AUDIO_SEL` 路径切换控制。

## 7. HDMI / LT8912B

| 功能 | 建议名 | 映射 | 来源 | 备注 |
| --- | --- | --- | --- | --- |
| LT8912B 配置 I2C SDA | PIN_HDMI_SDA | GPIO7 | 原理图 Page 7 | 共用主 I2C |
| LT8912B 配置 I2C SCL | PIN_HDMI_SCL | GPIO8 | 原理图 Page 7 | 共用主 I2C |
| LT8912B INT | PIN_HDMI_INT | GPIO4 | 原理图 Page 5/7 | `HDMI_INT` |
| HDMI DDC SDA | PIN_HDMI_DDC_SDA | GPIO9 | 原理图 Page 5/7 | `DSDA` |
| HDMI DDC SCL | PIN_HDMI_DDC_SCL | GPIO10 | 原理图 Page 5/7 | `DSCL` |

### 控制

| 功能 | 映射 | 来源 | 备注 |
| --- | --- | --- | --- |
| LT8912B RST | `XL9555 P00` | 原理图 Page 7/8 | `HDMI_RST` |
| HDMI 电源使能 | `XL9555 P01` | 原理图 Page 2/8 | `HDMI_PWR_EN`，控制 1.8 V LDO |

## 8. I2C 地址表

| 设备 | 地址 | 来源 | 备注 |
| --- | --- | --- | --- |
| GT911 | `0x5D` | 数据手册 | 常用地址，实际地址取决于复位时序 |
| ES8311 | `0x18` | 原理图 Page 6 | 音频 codec |
| PCA9535 | `0x20` | 原理图 Page 4 | EPD/TPS 扩展 |
| XL9555 | `0x22` | 原理图 Page 8 | 板载控制扩展 |
| SGM38121 | `0x28` | 原理图 Page 2 | 摄像头供电相关 |
| ICM20948 传感器 | `0x29` / `0x69` | 原理图 Page 8 | 按短接点选择 |
| LT8912B | `0x48` | 原理图 Page 7 | HDMI Bridge 主配置地址 |
| BQ27220 | `0x55` | 原理图 Page 2 | Fuel gauge |
| TPS651851 | `0x68` | 原理图 Page 4 | EPD 电源 |
| BQ25896 / 兼容充电 IC | `0x6B` / `0x68` | 原理图 Page 2 | 原理图标注 `0X6B/68`，以实装器件为准 |

> 若充电 IC 实装地址为 `0x68`，会与 TPS651851 地址相同；调试和 BOM 冻结时应再次确认器件型号与地址。

## 9. J2 扩展接口

J2 为 2.54 mm 2×10 排针。V0.3 原理图直接标出 GPIO，不再沿用 V0.2 文档中的 `LR_*`、`NFC_*`、`NRF_*` 等功能别名。

| J2 引脚 | 板级网络 | GPIO | 备注 |
| --- | --- | --- | --- |
| 1、3、5 | `VSYS` | - | 系统电源 |
| 2、4 | GND | - | 地 |
| 6 | `SCL` | 8 | 主 I2C SCL |
| 7 | `SPI_MISO` | 44 | 与 SD SPI 共用 |
| 8 | `SDA` | 7 | 主 I2C SDA |
| 9 | `SPI_SCK` | 45 | 与 SD SPI 共用 |
| 10 | `GPIO51` | 51 | 通用 GPIO |
| 11 | `SPI_MOSI` | 46 | 与 SD SPI 共用 |
| 12 | `GPIO50` | 50 | 通用 GPIO |
| 13 | `GPIO2` | 2 | 通用 GPIO |
| 14 | `GPIO49` | 49 | 通用 GPIO |
| 15 | `GPIO11` | 11 | 通用 GPIO |
| 16 | `GPIO23` | 23 | 通用 GPIO |
| 17 | `GPIO12` | 12 | 通用 GPIO |
| 18 | `GPIO22` | 22 | 通用 GPIO |
| 19 | `GPIO20` | 20 | 通用 GPIO |
| 20 | `GPIO21` | 21 | 通用 GPIO |

## 10. 代码现状提示

- 当前 `readme.md` 和 `components/t5_p4_board/include/t5_p4_board.h` 仍包含 V0.2 的 XL9555 映射，不能直接用于 V0.3。
- V0.3 的关键变化为：`GPIO6 = SEN_IRQ`、`GPIO52 = LED_PWM2`、`GPIO54 = C6_RST/EN`，并新增 `XL9555 P03 = SD_VDD_EN`。
- V0.3 的 XL9555 映射已整体调整：`P00/P01/P02/P03/P04` 分别为 `HDMI_RST/HDMI_PWR_EN/T_RST/SD_VDD_EN/AUDIO_EN`。
- EPD 数据与扫描 GPIO、PCA9535 EPD/TPS 控制、C6 SDIO、ES8311 I2S 和 SD SPI 信号保持不变。
- 软件适配时应新增独立的 V0.3 board config，避免用条件补丁继续复用 V0.2 的 XL9555 宏。
