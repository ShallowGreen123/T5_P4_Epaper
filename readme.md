[![Build Examples](https://github.com/ShallowGreen123/T5_P4_Epaper/actions/workflows/examples-build.yml/badge.svg)](https://github.com/ShallowGreen123/T5_P4_Epaper/actions/workflows/examples-build.yml)


# T5_P4_E_Paper

这是一个面向 `LILYGO T5-P4 E-Paper` 开发板的纯 `ESP-IDF` 示例仓库。

- `examples/`：`ESP-IDF` 示例工程
- `docs/`：补充说明和调试记录
- `firmware/`：板载 `ESP32-C6` 相关固件
- `hardware/`：原理图和芯片资料

如果你是第一次接触这个项目，建议先从 `examples/pca9535` 或 `examples/i2c_tools` 开始确认串口、下载和 I2C 是否正常。

## 这个项目里有什么

### 1. `examples/`

根目录的 `examples/` 是纯 `ESP-IDF` 示例，适合：

- 已经在用 Espressif 官方工具链
- 想看更底层的初始化和驱动写法
- 想做正式项目集成

当前示例包括：

- `pca9535`：IO 扩展芯片读取
- `i2c_tools`：I2C 工具示例
- `sgm38121`：电源相关芯片示例
- `sd_card_test`：uSD 卡类型、容量、文件读写校验示例
- `es8311_mic_speak` / `es8311_spiffs`：音频相关示例
- `fastEPD_lvgl_demo` / `hdmi_video_renderer` / `hdmi_video_renderer_lvgl`：显示相关示例
- `c6_wifi_scan`：通过板载 `ESP32-C6` 扫描 WiFi
- `camera_id_detect`：读取摄像头 ID 并识别 `SC2336` / `OV2710` / `OV5645`
- `camera_wifi_stream`：通过 WiFi 输出摄像头 MJPEG 画面

### 2. `docs/`

文档目录里放的是补充说明，建议优先看这几个：

- `docs/esp-hosted-c6-Slave.md`：板载 `ESP32-C6` 的 `esp-hosted` 从机说明
- `docs/pinmap.md`：引脚和硬件说明
- `docs/camera-sensor-power.md`：摄像头供电和上电顺序说明

### 3. `firmware/`

`firmware/` 里放的是配套固件，例如板载 `ESP32-C6` 使用的 `esp-hosted` slave 固件。运行 `examples/c6_wifi_scan` 前，需要先确认 C6 端固件已经烧录好。

### 4. `hardware/`

硬件目录里有原理图和芯片资料，遇到硬件问题时很有用。

常用文件：

- `hardware/T5-P4 E-paper V0.1.pdf`
- `hardware/IT8951_D_V0.2.4.3_20170728.pdf`
- `hardware/lt8912.pdf`
- `hardware/pca9535.pdf`

## 新手怎么开始

### 先做功能验证

如果你只是想确认板子和工具链通了，推荐先跑这两个 ESP-IDF 示例：

1. `examples/pca9535`
2. `examples/i2c_tools`

这两个示例依赖少，最适合确认串口、下载、I2C 是否正常。

### 想测试板载 C6 WiFi

推荐顺序：

1. 先看 `docs/esp-hosted-c6-Slave.md`
2. 先给板载 `ESP32-C6` 烧录 `esp-hosted` slave 固件
3. 再运行 `examples/c6_wifi_scan`

注意：`c6_wifi_scan` 不是开箱即用，它依赖 C6 端固件已经准备好。

## 5 分钟上手

### ESP-IDF

先进入一个简单示例目录，比如：

```bash
cd examples/pca9535
```

然后执行：

```bash
idf.py set-target esp32p4
idf.py build
idf.py -p <PORT> flash monitor
```

如果你想测试 WiFi 扫描：

```bash
cd examples/c6_wifi_scan
idf.py set-target esp32p4
idf.py build
idf.py -p <PORT> flash monitor
```

前提是板载 `ESP32-C6` 已经刷好 `esp-hosted` slave 固件。

## 推荐新手起步顺序

如果你完全是第一次接触这块板子，建议按这个顺序来：

1. 先确认串口和下载正常
2. 跑一个最简单的 `I2C` 或 `PCA9535` 示例
3. 再尝试 `C6 WiFi`
4. 最后再碰音频、HDMI、LVGL、摄像头这些依赖更多的功能

## 关键硬件信息

下面这些引脚是最常用、最值得先记住的：

| 功能 | 引脚 |
| --- | --- |
| I2C SDA | `7` |
| I2C SCL | `8` |
| SPI MISO | `44` |
| SPI SCK | `45` |
| SPI MOSI | `46` |
| SD CS | `47` |
| Touch INT | `3` |
| HDMI INT | `4` |
| PCA9535 INT | `5` |
| XL9555 INT / ALL_INT | `6` |
| HDMI DDC SDA/SCL | `9 / 10` |
| C6 SDIO D0/D1/D2/D3 | `14 / 15 / 16 / 17` |
| C6 SDIO CLK/CMD | `18 / 19` |
| C6 RST/EN | `XL9555 P13` |
| C6 WAKEUP | `XL9555 P14` |
| EPD STV | `48` |
| Frontlight PWM1/PWM2 | `53 / 54` |
| HDMI RST / EN | `XL9555 P10 / P11` |

V0.2 里新增了 `XL9555`，触摸复位、音频控制、HDMI 控制、C6 复位/唤醒都已经从旧 `PCA9535` 映射迁到 `XL9555`。如果你需要完整引脚定义，请看 `docs/pinmap.md` 或具体示例代码。

## 常见问题

### 1. 编译不过

- 根目录 `examples/` 需要 `ESP-IDF 5.4` 左右环境
- `idf.py set-target` 必须设为 `esp32p4`

### 2. `c6_wifi_scan` 跑不起来

优先检查：

- 板载 `ESP32-C6` 是否已经烧录 `esp-hosted` slave 固件
- 是否使用了正确的 `esp32p4` 目标
- 串口日志里是否出现 `esp_wifi_remote` / `Received Slave ESP Init`

### 3. 下载不稳定

这块板子的屏幕和部分启动相关引脚有耦合，某些情况下会影响下载模式进入。

如果你遇到反复下载失败：

- 先确认供电稳定
- 尽量先跑最简单的示例排除软件问题
- 再结合原理图和硬件资料排查

## 从哪里继续深入

你可以按自己的目标继续看：

- 想做纯 IDF：从 `examples/` 开始
- 想接入板载 C6 WiFi：先看 `docs/esp-hosted-c6-Slave.md`
- 想接入摄像头：先看 `docs/camera-sensor-power.md` 和 `examples/camera_id_detect`
- 想查引脚和芯片：看 `docs/` 和 `hardware/`

如果只是想“先跑起来一个东西”，推荐从 `examples/pca9535` 或 `examples/i2c_tools` 开始。

---

## 引脚定义（V0.2）
~~~c
// Shared I2C
#define BOARD_I2C_SDA                 (7)
#define BOARD_I2C_SCL                 (8)

// I2C devices
#define BOARD_I2C_ADDR_ES8311         (0x18)
#define BOARD_I2C_ADDR_PCA9535        (0x20) // EPD/TPS only on V0.2
#define BOARD_I2C_ADDR_XL9555         (0x22) // V0.2 control expander
#define BOARD_I2C_ADDR_SGM38121       (0x28)
#define BOARD_I2C_ADDR_TOUCH          (0x5D) // GT911
#define BOARD_I2C_ADDR_BQ27220        (0x55)
#define BOARD_I2C_ADDR_TPS651851      (0x68)
#define BOARD_I2C_ADDR_BQ25896        (0x6B)
#define BOARD_I2C_ADDR_LT8912B_MAIN   (0x48)
#define BOARD_I2C_ADDR_LT8912B_CEC    (0x49)
#define BOARD_I2C_ADDR_LT8912B_AVI    (0x4A)

// Direct GPIO
#define BOARD_TOUCH_INT               (3)
#define BOARD_HDMI_INT                (4)
#define BOARD_PCA9535_INT             (5)
#define BOARD_XL9555_INT              (6)  // ALL_INT
#define BOARD_HDMI_DDC_SDA            (9)
#define BOARD_HDMI_DDC_SCL            (10)
#define BOARD_BOOT                    (35)

// SD Card / SPI
#define BOARD_SD_MISO                 (44)
#define BOARD_SD_SCK                  (45)
#define BOARD_SD_MOSI                 (46)
#define BOARD_SD_CS                   (47)

// E-paper display
#define BOARD_EPD_D0                  (27)
#define BOARD_EPD_D1                  (28)
#define BOARD_EPD_D2                  (29)
#define BOARD_EPD_D3                  (30)
#define BOARD_EPD_D4                  (31)
#define BOARD_EPD_D5                  (32)
#define BOARD_EPD_D6                  (33)
#define BOARD_EPD_D7                  (34)
#define BOARD_EPD_CKV                 (13)
#define BOARD_EPD_CKH                 (24)
#define BOARD_EPD_STH                 (25)
#define BOARD_EPD_LEH                 (26)
#define BOARD_EPD_STV                 (48)
#define BOARD_FRONTLIGHT_LED1_PWM     (53)
#define BOARD_FRONTLIGHT_LED2_PWM     (54)

// ESP32-C6 SDIO
#define BOARD_C6_D0                   (14)
#define BOARD_C6_D1                   (15)
#define BOARD_C6_D2                   (16)
#define BOARD_C6_D3                   (17)
#define BOARD_C6_CLK                  (18)
#define BOARD_C6_CMD                  (19)

// ES8311 audio
#define BOARD_ES8311_I2S_DOUT         (39) // ESP32 -> ES8311 DSDIN
#define BOARD_ES8311_I2S_LRCK         (40)
#define BOARD_ES8311_I2S_DIN          (41) // ES8311 ASDOUT -> ESP32
#define BOARD_ES8311_I2S_SCLK         (42)
#define BOARD_ES8311_I2S_MCLK         (43)

// XL9555 IO expander, I2C address 0x22.
// Values below are zero-based IO indexes used by the BSP; comments show chip port names.
#define BOARD_XL_IO_TOUCH_RST         (0)  // P00, T_RST
#define BOARD_XL_IO_CC_SW0            (1)  // P01
#define BOARD_XL_IO_CC_SW1            (2)  // P02
#define BOARD_XL_IO_LR_RST            (3)  // P03
#define BOARD_XL_IO_NRF_CE            (4)  // P04
#define BOARD_XL_IO_AUDIO_SHUTDOWN    (6)  // P06, SHUTDOWN
#define BOARD_XL_IO_AUDIO_SEL         (7)  // P07, AUDIO_SEL
#define BOARD_XL_IO_HDMI_RST          (8)  // P10, HDMI_RST
#define BOARD_XL_IO_HDMI_EN           (9)  // P11, HDMI_EN
#define BOARD_XL_IO_SENSOR_IRQ        (10) // P12, SEN_IRQ
#define BOARD_XL_IO_C6_RST            (11) // P13, C6_RST/EN
#define BOARD_XL_IO_C6_WAKEUP         (12) // P14, C6_WAKEUP

// PCA9535 IO expander, I2C address 0x20.
// V0.2 keeps PCA9535 for EPD/TPS control only.
#define BOARD_PCA_IO_EPD_OE           (8)  // IO1_0, EP_OE
#define BOARD_PCA_IO_EPD_MODE         (9)  // IO1_1, EP_MODE
#define BOARD_PCA_IO_TPS_PWRUP        (11) // IO1_3, TPS_PWRUP
#define BOARD_PCA_IO_VCOM_CTRL        (12) // IO1_4, VCOM_CTRL
#define BOARD_PCA_IO_TPS_WAKEUP       (13) // IO1_5, TPS_WAKEUP
#define BOARD_PCA_IO_TPS_PWR_GOOD     (14) // IO1_6, TPS_PWR_GOOD
#define BOARD_PCA_IO_TPS_INT          (15) // IO1_7, TPS_INT

// HDMI / LT8912B
#define BOARD_HDMI_SDA                BOARD_I2C_SDA
#define BOARD_HDMI_SCL                BOARD_I2C_SCL
#define BOARD_HDMI_RST                BOARD_XL_IO_HDMI_RST
#define BOARD_HDMI_EN                 BOARD_XL_IO_HDMI_EN

// Expansion header GPIO changes in V0.2
#define BOARD_EXT_LR_IRQ_GPS_PPS      (2)
#define BOARD_EXT_NFC_IRQ             (11)
#define BOARD_EXT_NRF_IRQ_7682_DTR    (12)
#define BOARD_EXT_LR_CS_GPS_TX        (20)
#define BOARD_EXT_LR_BUSY_GPS_RX      (21)
#define BOARD_EXT_NFC_CS              (22)
#define BOARD_EXT_CC_GDO2_7682_TXD    (23)
#define BOARD_EXT_CC_GDO0_7682_RXD    (49)
#define BOARD_EXT_CC_CS_7682_RST      (50)
#define BOARD_EXT_NRF_CS_7682_RI      (51)
#define BOARD_EXT_MODULE_EN_POWER_KEY (52)
~~~
