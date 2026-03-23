# T5-P4 E-paper 引脚映射

## 1. 主控直连 GPIO

| 功能 | 现有宏/建议名 | GPIO | 来源 | 备注 |
| --- | --- | --- | --- | --- |
| 主 I2C SDA | `BOARD_I2C_SDA` | 7 | 原理图 Page 2/4/7 + 代码 | 全板共享 I2C |
| 主 I2C SCL | `BOARD_I2C_SCL` | 8 | 原理图 Page 2/4/7 + 代码 | 全板共享 I2C |
| 触摸中断 | `BOARD_TOUCH_INT` | 3 | 原理图 Page 4/5 | `CTP_INT` |
| PCA9535 中断 | `BOARD_PCA_INT` | 5 | 原理图 Page 4/5 | `PCA_INT` |
| HDMI 中断 | `BOARD_HDMI_INT` | 4 | 原理图 Page 5/7 + 代码 | `LT8912B_INT` |
| ESP32-C6 唤醒 | `BOARD_C6_WAKEUP` | 6 | 原理图 Page 3/5 + 代码 | `C6_WAKEUP` |
| ESP32-C6 复位 | `BOARD_C6_RST` | 54 | 原理图 Page 3/5 + 代码 | `C6_RST/EN` |
| HDMI DDC SDA | `BOARD_HDMI_DDC_SDA` | 9 | 原理图 Page 7 + 代码 | `DSDA` |
| HDMI DDC SCL | `BOARD_HDMI_DDC_SCL` | 10 | 原理图 Page 7 + 代码 | `DSCL` |
| SD SPI MISO | `BOARD_SD_MISO` | 44 | 原理图 Page 5 + 代码 | 复用主 SPI |
| SD SPI SCK | `BOARD_SD_SCK` | 45 | 原理图 Page 5 + 代码 | 复用主 SPI |
| SD SPI MOSI | `BOARD_SD_MOSI` | 46 | 原理图 Page 5 + 代码 | 复用主 SPI |
| SD SPI CS | `BOARD_SD_CS` | 47 | 原理图 Page 5 + 代码 | 独立片选 |

## 2. 墨水屏直连并口

| 功能 | 现有宏 | GPIO | 来源 | 备注 |
| --- | --- | --- | --- | --- |
| EPD D0 | `BOARD_DISPALY_D0` | 27 | 原理图 Page 5 + `FastEPD` | 建议后续改名为 `BOARD_DISPLAY_D0` |
| EPD D1 | `BOARD_DISPALY_D1` | 28 | 原理图 Page 5 + `FastEPD` | 同上 |
| EPD D2 | `BOARD_DISPALY_D2` | 29 | 原理图 Page 5 + `FastEPD` | 同上 |
| EPD D3 | `BOARD_DISPALY_D3` | 30 | 原理图 Page 5 + `FastEPD` | 同上 |
| EPD D4 | `BOARD_DISPALY_D4` | 31 | 原理图 Page 5 + `FastEPD` | 同上 |
| EPD D5 | `BOARD_DISPALY_D5` | 32 | 原理图 Page 5 + `FastEPD` | 同上 |
| EPD D6 | `BOARD_DISPALY_D6` | 33 | 原理图 Page 5 + `FastEPD` | 同上 |
| EPD D7 | `BOARD_DISPALY_D7` | 34 | 原理图 Page 5 + `FastEPD` | 同上 |
| EPD CKH | `BOARD_DISPALY_CKH` | 24 | 原理图 Page 5 + `FastEPD` | 行时钟相关 |
| EPD STH | `BOARD_DISPALY_STH` | 25 | 原理图 Page 4/5 + `FastEPD` | 也见 `EP_STH` |
| EPD LEH | `BOARD_DISPALY_LEH` | 26 | 原理图 Page 5 + `FastEPD` | 也见 `EP_LE` |
| EPD CKV | `BOARD_DISPALY_CKV` | 13 | 原理图 Page 4/5 + `FastEPD` | 也见 `EP_CKV` |
| EPD STV | `BOARD_DISPALY_STV` | 36 | 原理图 Page 4/5 + `FastEPD` | 也见 `EP_STV` |
| EPD LED1 | `BOARD_DISPALY_LED1` | 11 | 现有 README + `FastEPD` | 代码里作为 dummy/DC 占位，非主扫描控制 |
| EPD LED2 | `BOARD_DISPALY_LED2` | 12 | 现有 README | 当前仓库使用较少 |

## 3. PCA9535 扩展 IO

| PCA9535 位号 | 现有宏 | 板级网络 | 方向 | 备注 |
| --- | --- | --- | --- | --- |
| IO0 | `BOARD_PCA_00_T_RST` | `T_RST` | 输出 | 触摸复位 |
| IO1 | `BOARD_PCA_01_CC_SW0` | `CC_SW0` | 输出 | 预留/扩展 |
| IO2 | `BOARD_PCA_02_CC_SW1` | `CC_SW1` | 输出 | 预留/扩展 |
| IO3 | `BOARD_PCA_03_LR_RST` | `LR_RST` | 输出 | 预留/扩展 |
| IO4 | `BOARD_PCA_04_NRF_CE` | `NRF_CE` | 输出 | 预留/扩展 |
| IO5 | `BOARD_PCA_05_SHUTDOWN` | `SHUTDOWN` | 输出 | 音频放大器等相关控制常见用法 |
| IO6 | `BOARD_PCA_06_HDMI_RST` | `HDMI_RST` | 输出 | `LT8912B_RST` |
| IO7 | `BOARD_PCA_07_HDMI_EN` | `HDMI_EN` | 输出 | HDMI 使能 |
| IO8 | `BOARD_PCA_10_EP_OE` | `EP_OE` | 输出 | 墨水屏输出使能 |
| IO9 | `BOARD_PCA_11_EP_MODE` | `EP_MODE` | 输出 | 墨水屏模式控制 |
| IO10 | `BOARD_PCA_12_1V8_EN` | `1V8_EN` | 输出 | HDMI/外设 1.8V 供电使能 |
| IO11 | `BOARD_PCA_13_TPS_PWRUP` | `TPS_PWRUP` | 输出 | EPD 电源上电序列 |
| IO12 | `BOARD_PCA_14_VCOM_CTRL` | `VCOM_CTRL` | 输出 | EPD VCOM 控制 |
| IO13 | `BOARD_PCA_15_TPS_WAKEUP` | `TPS_WAKEUP` | 输出 | 唤醒 `TPS651851` |
| IO14 | `BOARD_PCA_16_TPS_PWR_GOOD` | `TPS_PWR_GOOD` | 输入 | 不要当输出使用 |
| IO15 | `BOARD_PCA_17_TPS_INT` | `TPS_INT` | 输入 | 不要当输出使用 |

## 4. ESP32-C6 SDIO 连接

| 功能 | 现有宏 | GPIO | 来源 |
| --- | --- | --- | --- |
| C6 D0 | `BOARD_C6_D0` | 14 | 原理图 Page 3 + 代码 |
| C6 D1 | `BOARD_C6_D1` | 15 | 原理图 Page 3 + 代码 |
| C6 D2 | `BOARD_C6_D2` | 16 | 原理图 Page 3 + 代码 |
| C6 D3 | `BOARD_C6_D3` | 17 | 原理图 Page 3 + 代码 |
| C6 CLK | `BOARD_C6_CLK` | 18 | 原理图 Page 3 + 代码 |
| C6 CMD | `BOARD_C6_CMD` | 19 | 原理图 Page 3 + 代码 |

## 5. 音频 ES8311

| 功能 | 现有宏 | GPIO | 原理图结论 | 备注 |
| --- | --- | --- | --- | --- |
| ES8311 I2C SDA | `BOARD_ES8311_I2C_SDA` | 7 | 已确认 | 共用主 I2C |
| ES8311 I2C SCL | `BOARD_ES8311_I2C_SCL` | 8 | 已确认 | 共用主 I2C |
| I2S MCLK | `BOARD_ES8311_I2S_MCLK` | 43 | 已确认 | Page 5/6 一致 |
| I2S BCLK | `BOARD_ES8311_I2S_SCLK` | 42 | 已确认 | Page 5/6 一致 |
| I2S LRCK | `BOARD_ES8311_I2S_LRCK` | 40 | 已确认 | Page 5/6 一致 |
| ES8311 `ASDOUT` | `BOARD_ES8311_I2S_ASDOUT` | 39 | 已确认 | 原理图文本层读到 `I2S_ASDOUT------IO41` |
| ES8311 `DSDIN` | `BOARD_ES8311_I2S_DSDIN` | 41 | 已确认 | 原理图文本层读到 `I2S_DSDIN------IO39` |

> 说明：原理图是按ES8311 视角命名 `I2S_ASDOUT------IO41`、`I2S_DSDIN------IO39`，而在使用时 ESP32 I2S 的应按照如下连接 `EPS32_DIN --- ES8311_ASDOUT`、`EPS32_DOUT --- ES8311_DSDIN`，所以 ESP32的 `DIN --- IO41`、`DOUT --- IO39`

## 6. HDMI / LT8912B

| 功能 | 现有宏 | 映射 | 来源 | 备注 |
| --- | --- | --- | --- | --- |
| LT8912B 配置 I2C SDA | `BOARD_HDMI_SDA` | GPIO7 | 原理图 Page 7 + 代码 | 共用主 I2C |
| LT8912B 配置 I2C SCL | `BOARD_HDMI_SCL` | GPIO8 | 原理图 Page 7 + 代码 | 共用主 I2C |
| LT8912B INT | `BOARD_HDMI_INT` | GPIO4 | 原理图 Page 7 + 代码 | 中断输入 |
| LT8912B RST | `BOARD_HDMI_RST` | PCA9535 IO6 | 原理图 Page 4/7 + 代码 | 通过扩展 IO 控制 |
| HDMI EN | `BOARD_PCA_07_HDMI_EN` | PCA9535 IO7 | 原理图 Page 4/7 + 代码 | 桥接电路使能 |
| HDMI 1V8 EN | `BOARD_PCA_12_1V8_EN` | PCA9535 IO10 | 原理图 Page 4/7 + 代码 | 1.8V 供电使能 |
| HDMI DDC SDA | `BOARD_HDMI_DDC_SDA` | GPIO9 | 原理图 Page 7 + 代码 | `DSDA` |
| HDMI DDC SCL | `BOARD_HDMI_DDC_SCL` | GPIO10 | 原理图 Page 7 + 代码 | `DSCL` |
| MIPI DSI Data/Clock | 无普通 GPIO 宏 | SoC 专用 DSI PHY | 原理图 Page 5/7 | 不建议当作普通 IO 管理 |

## 7. I2C 地址表

| 设备 | 地址 | 来源 |
| --- | --- | --- |
| ES8311 | `0x18` | 原理图 Page 6 |
| PCA9535 | `0x20` | 原理图 Page 4 |
| SGM38121 | `0x28` | 原理图 Page 2 |
| BQ27220 | `0x55` | 原理图 Page 2 |
| TPS651851 | `0x68` | 原理图 Page 4 |
| BQ25896 | `0x6B` | 原理图 Page 2 |
| LT8912B | `0x48` | 代码 probe + 现有实现 |

## 8. 待确认项

| 项目 | 当前状态 |
| --- | --- |
| 触摸控制器型号 | 确认触摸控制器信号为 `GT911`  |
| 触摸 I2C 地址 | 确认触摸地址为 `0x5D` |
| ES8311 `ASDOUT/DSDIN` 宏方向 | 已确认 |
