# T5-P4 E-paper 引脚映射（V0.2）

> 本文按 `hardware/T5-P4 E-paper V0.2.PDF` 与 `hardware/T5-P4 E-paper V0.2版本变更.txt` 整理。

> 仓库里仍有部分示例沿用 V0.1 的 `PCA9535`/旧 GPIO 假设；如果与本文冲突，以 V0.2 原理图为准。

## 1. 主控直连 GPIO

| 功能           | 建议名              | GPIO | 来源               | 备注                 |
| ------------ | ---------------- | ---- | ---------------- | ------------------ |
| 主 I2C SDA    | PIN_I2C_SDA      | 7    | 原理图 Page 2/4/7/8 | 全板共享 I2C           |
| 主 I2C SCL    | PIN_I2C_SCL      | 8    | 原理图 Page 2/4/7/8 | 全板共享 I2C           |
| 触摸中断         | PIN_TOUCH_INT    | 3    | 原理图 Page 4/5     | GT911 INT          |
| HDMI 中断      | PIN_HDMI_INT     | 4    | 原理图 Page 5/7     | LT8912B INT        |
| PCA9535 中断   | PIN_PCA9535_INT  | 5    | 原理图 Page 4/5     | EPD/TPS 扩展中断       |
| XL9555 中断    | PIN_XL9555_INT   | 6    | 原理图 Page 5/8     | `XL9555 INT` 汇总到主控 |
| HDMI DDC SDA | PIN_HDMI_DDC_SDA | 9    | 原理图 Page 5/7     | HDMI EDID 总线       |
| HDMI DDC SCL | PIN_HDMI_DDC_SCL | 10   | 原理图 Page 5/7     | HDMI EDID 总线       |
| SD SPI MISO  | PIN_SD_MISO      | 44   | 原理图 Page 5       | `SPI_MISO`         |
| SD SPI SCK   | PIN_SD_SCK       | 45   | 原理图 Page 5       | `SPI_SCK`          |
| SD SPI MOSI  | PIN_SD_MOSI      | 46   | 原理图 Page 5       | `SPI_MOSI`         |
| SD SPI CS    | PIN_SD_CS        | 47   | 原理图 Page 5       | `SD_CS`            |
| BOOT         | PIN_BOOT         | 35   | 原理图 Page 5       | 上拉，低电平触发           |

## 2. 墨水屏与背光

| 功能      | 建议名         | GPIO | 来源           | 备注        |
| ------- | ----------- | ---- | ------------ | --------- |
| EPD D0  | PIN_EPD_D0  | 27   | 原理图 Page 4/5 | `EP_D0`   |
| EPD D1  | PIN_EPD_D1  | 28   | 原理图 Page 4/5 | `EP_D1`   |
| EPD D2  | PIN_EPD_D2  | 29   | 原理图 Page 4/5 | `EP_D2`   |
| EPD D3  | PIN_EPD_D3  | 30   | 原理图 Page 4/5 | `EP_D3`   |
| EPD D4  | PIN_EPD_D4  | 31   | 原理图 Page 4/5 | `EP_D4`   |
| EPD D5  | PIN_EPD_D5  | 32   | 原理图 Page 4/5 | `EP_D5`   |
| EPD D6  | PIN_EPD_D6  | 33   | 原理图 Page 4/5 | `EP_D6`   |
| EPD D7  | PIN_EPD_D7  | 34   | 原理图 Page 4/5 | `EP_D7`   |
| EPD CKH | PIN_EPD_CKH | 24   | 原理图 Page 4/5 | `EP_CKH`  |
| EPD STH | PIN_EPD_STH | 25   | 原理图 Page 4/5 | `EP_STH`  |
| EPD LEH | PIN_EPD_LEH | 26   | 原理图 Page 4/5 | `EP_LE`   |
| EPD CKV | PIN_EPD_CKV | 13   | 原理图 Page 4/5 | 墨水屏扫描控制   |
| EPD STV | PIN_EPD_STV | 48   | 原理图 Page 4/5 | `GPIO48`  |
| 背光 PWM1 | PIN_EPD_BL1 | 53   | 原理图 Page 4/5 | `BL1_PWM` |
| 背光 PWM2 | PIN_EPD_BL2 | 54   | 原理图 Page 4/5 | `BL2_PWM` |

> 说明：V0.2 里 `LED1_PWM/LED2_PWM` 接到 `TPS61150ADRCR`，用于背光驱动；不应再按旧版文档理解为 e-paper 的 dummy/DC 占位脚。

## 3. XL9555 扩展 IO（V0.2 新增）

I2C 地址：`0x22`，中断输出 `INT -> ALL_INT -> GPIO6`。

| XL9555 | 建议名                  | 板级网络        | 方向  | 来源         | 备注           |
| ------ | -------------------- | ----------- | --- | ---------- | ------------ |
| P00    | PIN_XL9555_T_RST     | `T_RST`     | 输出  | 原理图 Page 8 | 触摸复位         |
| P01    | PIN_XL9555_CC_SW0    | `CC_SW0`    | 输出  | 原理图 Page 8 | 扩展板/射频复用     |
| P02    | PIN_XL9555_CC_SW1    | `CC_SW1`    | 输出  | 原理图 Page 8 | 扩展板/射频复用     |
| P03    | PIN_XL9555_LR_RST    | `LR_RST`    | 输出  | 原理图 Page 8 | 扩展板复位        |
| P04    | PIN_XL9555_NRF_CE    | `NRF_CE`    | 输出  | 原理图 Page 8 | 扩展板控制        |
| P05    | -                    | NC          | -   | 原理图 Page 8 | 当前未接出        |
| P06    | PIN_XL9555_AUDIO_AF  | `SHUTDOWN`  | 输出  | 原理图 Page 8 | 音频功放关断       |
| P07    | PIN_XL9555_AUDIO_SEL | `AUDIO_SEL` | 输出  | 原理图 Page 8 | 单扬声器/音频路径切换  |
| P10    | PIN_XL9555_HDMI_RST  | `HDMI_RST`  | 输出  | 原理图 Page 8 | LT8912B 复位   |
| P11    | PIN_XL9555_HDMI_EN   | `HDMI_EN`   | 输出  | 原理图 Page 8 | HDMI 相关电源/使能 |
| P12    | PIN_XL9555_SEN_IRQ   | `SEN_IRQ`   | 输入  | 原理图 Page 8 | 传感器/加速度计中断   |
| P13    | PIN_XL9555_C6_RST    | `C6_RST/EN` | 输出  | 原理图 Page 8 | ESP32-C6 复位  |
| P14    | PIN_XL9555_C6_WAKEUP | `C6_WAKEUP` | 输出  | 原理图 Page 8 | ESP32-C6 唤醒  |
| P15    | -                    | NC          | -   | 原理图 Page 8 | 当前未接出        |
| P16    | -                    | NC          | -   | 原理图 Page 8 | 当前未接出        |
| P17    | -                    | NC          | -   | 原理图 Page 8 | 当前未接出        |

## 4. PCA9535 扩展 IO（V0.2 仅负责 EPD/TPS）

I2C 地址：`0x20`，中断输出 `PCA_INT -> GPIO5`。

| PCA9535 | 建议名                      | 板级网络           | 方向  | 来源         | 备注             |
| ------- | ------------------------ | -------------- | --- | ---------- | -------------- |
| IO0_0   | -                        | NC             | -   | 原理图 Page 4 | 当前未接出          |
| IO0_1   | -                        | NC             | -   | 原理图 Page 4 | 同上             |
| IO0_2   | -                        | NC             | -   | 原理图 Page 4 | 同上             |
| IO0_3   | -                        | NC             | -   | 原理图 Page 4 | 同上             |
| IO0_4   | -                        | NC             | -   | 原理图 Page 4 | 同上             |
| IO0_5   | -                        | NC             | -   | 原理图 Page 4 | 同上             |
| IO0_6   | -                        | NC             | -   | 原理图 Page 4 | 同上             |
| IO0_7   | -                        | NC             | -   | 原理图 Page 4 | 同上             |
| IO1_0   | PIN_PCA9535_EPD_OE       | `EP_OE`        | 输出  | 原理图 Page 4 | EPD 输出使能       |
| IO1_1   | PIN_PCA9535_EPD_MODE     | `EP_MODE`      | 输出  | 原理图 Page 4 | EPD 模式控制       |
| IO1_2   | -                        | NC             | -   | 原理图 Page 4 | 当前未接出          |
| IO1_3   | PIN_PCA9535_TPS_PWUP     | `TPS_PWRUP`    | 输出  | 原理图 Page 4 | EPD 电源上电序列     |
| IO1_4   | PIN_PCA9535_VCOM_CTRL    | `VCOM_CTRL`    | 输出  | 原理图 Page 4 | EPD VCOM 控制    |
| IO1_5   | PIN_PCA9535_TPS_WAKEUP   | `TPS_WAKEUP`   | 输出  | 原理图 Page 4 | 唤醒 `TPS651851` |
| IO1_6   | PIN_PCA9535_TPS_PWP_GOOD | `TPS_PWR_GOOD` | 输入  | 原理图 Page 4 | 电源良好检测         |
| IO1_7   | PIN_PCA9535_TPS_INT      | `TPS_INT`      | 输入  | 原理图 Page 4 | `TPS651851` 中断 |

  

> 说明：旧版文档/代码里常见的 `T_RST`、`SHUTDOWN`、`HDMI_RST`、`HDMI_EN`、`C6_WAKEUP` 等 `PCA9535` 映射，在 V0.2 已迁到 `XL9555`，不再适用。

  

## 5. ESP32-C6 连接

### SDIO

| 功能     | 建议名        | GPIO | 来源           |
| ------ | ---------- | ---- | ------------ |
| C6 D0  | PIN_C6_D0  | 14   | 原理图 Page 3/5 |
| C6 D1  | PIN_C6_D1  | 15   | 原理图 Page 3/5 |
| C6 D2  | PIN_C6_D2  | 16   | 原理图 Page 3/5 |
| C6 D3  | PIN_C6_D3  | 17   | 原理图 Page 3/5 |
| C6 CLK | PIN_C6_CLK | 18   | 原理图 Page 3/5 |
| C6 CMD | PIN_C6_CMD | 19   | 原理图 Page 3/5 |

### 控制

| 功能          | 映射           | 来源           | 备注                 |
| ----------- | ------------ | ------------ | ------------------ |
| C6 `RST/EN` | `XL9555 P13` | 原理图 Page 3/8 | V0.2 不再直连 `GPIO54` |
| C6 `WAKEUP` | `XL9555 P14` | 原理图 Page 3/8 | V0.2 不再直连 `GPIO6`  |

## 6. 音频 ES8311

| 功能                             | 建议名                   | GPIO  | 来源           | 备注              |
| ------------------------------ | --------------------- | ----- | ------------ | --------------- |
| ES8311 I2C SDA                 | PIN_ES8311_I2C_SDA    | 7     | 原理图 Page 6   | 共用主 I2C         |
| ES8311 I2C SCL                 | PIN_ES8311_I2C_SCL    | 8     | 原理图 Page 6   | 共用主 I2C         |
| I2S MCLK                       | PIN_ES8311_I2S_MCLK   | 43    | 原理图 Page 5/6 | `I2S_MCLK`      |
| I2S BCLK                       | PIN_ES8311_I2S_SCLK   | 42    | 原理图 Page 5/6 | `I2S_SCLK`      |
| I2S LRCK                       | PIN_ES8311_I2S_LRCK   | 40    | 原理图 Page 5/6 | `I2S_LRCK`      |
| ESP32 `DIN` <- ES8311 `ASDOUT` | PIN_ES8311_I2S_ASDOUT | 41    | 原理图 Page 5/6 | 原理图按 codec 视角命名 |
| ESP32 `DOUT` -> ES8311 `DSDIN` | PIN_ES8311_I2S_DSDIN  | 39    | 原理图 Page 5/6 | 原理图按 codec 视角命名 |

### 控制

| 功能     | 映射           | 来源           | 备注          |
| ------ | ------------ | ------------ | ----------- |
| 功放关断   | `XL9555 P06` | 原理图 Page 6/8 | `SHUTDOWN`  |
| 音频路径切换 | `XL9555 P07` | 原理图 Page 6/8 | `AUDIO_SEL` |

## 7. HDMI / LT8912B

| 功能                 | 建议名              | 映射     | 来源           | 备注         |
| ------------------ | ---------------- | ------ | ------------ | ---------- |
| LT8912B 配置 I2C SDA | PIN_HDMI_SDA     | GPIO7  | 原理图 Page 7   | 共用主 I2C    |
| LT8912B 配置 I2C SCL | PIN_HDMI_SCL     | GPIO8  | 原理图 Page 7   | 共用主 I2C    |
| LT8912B INT        | PIN_HDMI_INT     | GPIO4  | 原理图 Page 5/7 | `HDMI_INT` |
| HDMI DDC SDA       | PIN_HDMI_DDC_SDA | GPIO9  | 原理图 Page 5/7 | `DSDA`     |
| HDMI DDC SCL       | PIN_HDMI_DDC_SCL | GPIO10 | 原理图 Page 5/7 | `DSCL`     |
### 控制

| 功能          | 映射           | 来源             | 备注         |
| ----------- | ------------ | -------------- | ---------- |
| LT8912B RST | `XL9555 P10` | 原理图 Page 7/8   | `HDMI_RST` |
| HDMI EN     | `XL9555 P11` | 原理图 Page 2/7/8 | `HDMI_EN`  |

## 8. I2C 地址表

| 设备            | 地址              | 来源         | 备注          |
| ------------- | --------------- | ---------- | ----------- |
| GT911         | `0x5D`          | 数据手册       | 常用地址 `0x5D` |
| ES8311        | `0x18`          | 原理图 Page 6 | 音频 codec    |
| PCA9535       | `0x20`          | 原理图 Page 4 | EPD/TPS 扩展  |
| XL9555        | `0x22`          | 原理图 Page 8 | 新增通用 IO 扩展  |
| H722/H769 传感器 | `0x29` / `0x69` | 原理图 Page 8 | 需要按短接点选择    |
| SGM38121      | `0x28`          | 原理图 Page 2 | 摄像头供电相关     |
| LT8912B       | `0x48`          | 原理图 Page 7 | HDMI Bridge |
| BQ27220       | `0x55`          | 原理图 Page 2 | Fuel gauge  |
| TPS651851     | `0x68`          | 原理图 Page 4 | EPD 电源      |
| BQ25896       | `0x6B`          | 原理图 Page 2 | Charger     |

## 9. 扩展板复用 GPIO

以下条目来自 V0.2 版本变更说明与原理图 Page 5/8，主要用于外接扩展板功能复用：

| 功能 | GPIO | 备注 |
| --- | --- | --- |
| `LR_IRQ / GPS_PPS` | 2 | 扩展板 |
| `NFC_IRQ` | 11 | 扩展板 |
| `NRF_IRQ / 7682_DTR` | 12 | 扩展板 |
| `LR_CS / GPS_TX` | 20 | 扩展板 |
| `LR_BUSY / GPS_RX` | 21 | 扩展板 |
| `NFC_CS` | 22 | 扩展板 |
| `CC_GDO2 / 7682_TXD` | 23 | 扩展板 |
| `CC_GDO0 / 7682_RXD` | 49 | 扩展板 |
| `CC_CS / 7682_RST` | 50 | 扩展板 |
| `NRF_CS / 7682_RI` | 51 | 扩展板 |
| `MODULE_EN / POWER_KEY` | 52 | 扩展板 |


## 10. 代码现状提示

- 本仓库里仍能看到旧版假设：例如 `GPIO6 = C6_WAKEUP`、`GPIO54 = C6_RST`、`PCA9535` 控 `HDMI_RST/SHUTDOWN` 等。

- V0.2 新硬件应以本文为准：`GPIO6 = ALL_INT`，`C6_RST/EN = XL9555 P13`，`C6_WAKEUP = XL9555 P14`，`SHUTDOWN/AUDIO_SEL/HDMI_RST/HDMI_EN` 也都在 `XL9555` 上。

- 如果后续要统一代码里的板级宏，建议按 `V0.2` 新建一套独立 board config，而不是继续在旧 `PCA9535` 宏名上叠补丁。