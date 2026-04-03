
# T5_P4_E_Paper

这是一个面向 `LILYGO T5-P4 E-Paper` 开发板的示例仓库，主要包含两套开发路线：

- `examples/`：纯 `ESP-IDF` 示例
- `platformIO_P4/`：`PlatformIO + Arduino` 示例

如果你是第一次接触这个项目，建议先看完这份 README，再决定走哪一条路线。

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
- `es8311_mic_speak` / `es8311_spiffs`：音频相关示例
- `hdmi_video_renderer` / `hdmi_video_renderer_lvgl`：显示相关示例
- `c6_wifi_scan`：通过板载 `ESP32-C6` 扫描 WiFi

### 2. `platformIO_P4/`

`platformIO_P4/` 是 `PlatformIO + Arduino` 版本示例，适合：

- 更熟悉 Arduino 风格开发
- 想更快试运行功能
- 想直接在 PlatformIO 工程里切换示例

典型示例包括：

- `i2c_scan`
- `c6_wifi_conn`
- `c6_wifi_scan`
- `sd_card`
- `rtc`
- `touch_gt911`
- `lvgl_demo`

### 3. `docs/`

文档目录里放的是补充说明，建议优先看这几个：

- `docs/esp-hosted-c6-Slave.md`：板载 `ESP32-C6` 的 `esp-hosted` 从机说明
- `docs/arduino.md`：如何把 Arduino 作为 ESP-IDF component 使用
- `docs/pinmap.md`：引脚和硬件说明

### 4. `hardware/`

硬件目录里有原理图和芯片资料，遇到硬件问题时很有用。

常用文件：

- `hardware/T5-P4 E-paper V0.1.pdf`
- `hardware/IT8951_D_V0.2.4.3_20170728.pdf`
- `hardware/lt8912.pdf`
- `hardware/pca9535.pdf`

## 新手怎么选路线

### 先做功能验证

如果你只是想确认板子和工具链通了，推荐先跑这两个：

1. `examples/pca9535`
2. `platformIO_P4/examples/i2c_scan`

这两个示例依赖少，最适合确认串口、下载、I2C 是否正常。

### 想测试板载 C6 WiFi

推荐顺序：

1. 先看 `docs/esp-hosted-c6-Slave.md`
2. 先给板载 `ESP32-C6` 烧录 `esp-hosted` slave 固件
3. 再运行 `examples/c6_wifi_scan` 或 `platformIO_P4/examples/c6_wifi_scan`

注意：`c6_wifi_scan` 不是开箱即用，它依赖 C6 端固件已经准备好。

### 想走 Arduino / PlatformIO

直接看 `platformIO_P4/README.md`，然后在 `platformIO_P4/platformio.ini` 里切换 `src_dir` 即可。

## 5 分钟上手

### 路线 A：ESP-IDF

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

### 路线 B：PlatformIO + Arduino

进入工程目录：

```bash
cd platformIO_P4
```

打开 `platformio.ini`，把 `src_dir` 切到你要运行的示例，例如：

```ini
src_dir = examples/c6_wifi_scan
```

然后执行：

```bash
pio run
pio run -t upload
pio device monitor
```

## 推荐新手起步顺序

如果你完全是第一次接触这块板子，建议按这个顺序来：

1. 先确认串口和下载正常
2. 跑一个最简单的 `I2C` 或 `PCA9535` 示例
3. 再尝试 `PlatformIO` 或 `ESP-IDF` 其中一条主开发路线
4. 最后再碰 `C6 WiFi`、音频、HDMI、LVGL 这些依赖更多的功能

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
| C6 SDIO D0/D1/D2/D3 | `14 / 15 / 16 / 17` |
| C6 SDIO CLK/CMD | `18 / 19` |
| C6 RST | `54` |
| C6 WAKEUP | `6` |

如果你需要完整引脚定义，请看 `docs/pinmap.md` 或具体示例代码。

## 常见问题

### 1. 编译不过

- 根目录 `examples/` 需要 `ESP-IDF 5.4` 左右环境
- `idf.py set-target` 必须设为 `esp32p4`
- `PlatformIO` 和 `ESP-IDF` 两套工程不要混着编

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
- 想快速做应用：从 `platformIO_P4/` 开始
- 想接入板载 C6 WiFi：先看 `docs/esp-hosted-c6-Slave.md`
- 想查引脚和芯片：看 `docs/` 和 `hardware/`

如果只是想“先跑起来一个东西”，推荐从 `examples/pca9535` 或 `platformIO_P4/examples/i2c_scan` 开始。

---

## 引脚 🎁
~~~c
// IIC Addr
#define BOARD_I2C_ADDR_14           (0x14)
#define BOARD_I2C_ADDR_ES8311       (0x18)
#define BOARD_I2C_ADDR_PCA9535      (0x20) // PCA9535PW
#define BOARD_I2C_ADDR_SGM38121     (0x28)
#define BOARD_I2C_ADDR_48           (0x48) // LT8912
#define BOARD_I2C_ADDR_49           (0x49)
#define BOARD_I2C_ADDR_4A           (0x4A)
#define BOARD_I2C_ADDR_4B           (0x4B)
// #define BOARD_I2C_ADDR_TOUCH        (0x38) // FT5536
#define BOARD_I2C_ADDR_TOUCH        (0x5D) // GT911
#define BOARD_I2C_ADDR_BQ27220      (0x55) // BQ27220
#define BOARD_I2C_ADDR_TPS651851    (0x68)
#define BOARD_I2C_ADDR_BQ25896      (0x6B) // BQ25896

// IIC
#define BOARD_I2C_SDA       (7)
#define BOARD_I2C_SCL       (8)

// SPI 
#define BOARD_SPI_MISO (44)
#define BOARD_SPI_SCK  (45)
#define BOARD_SPI_MOSI (46)

// PCA9535PW  --  IO expansion
#define BOARD_PCA_INT             (5)
#define BOARD_PCA_SDA             BOARD_I2C_SDA
#define BOARD_PCA_SCL             BOARD_I2C_SCL
#define BOARD_PCA_00_T_RST        (0)
#define BOARD_PCA_01_CC_SW0       (1)
#define BOARD_PCA_02_CC_SW1       (2)
#define BOARD_PCA_03_LR_RST       (3)
#define BOARD_PCA_04_NRF_CE       (4)
#define BOARD_PCA_05_SHUTDOWN     (5)
#define BOARD_PCA_06_HDMI_RST     (6)
#define BOARD_PCA_07_HDMI_EN      (7)
#define BOARD_PCA_10_EP_OE        (8)
#define BOARD_PCA_11_EP_MODE      (9)
#define BOARD_PCA_12_1V8_EN       (10)
#define BOARD_PCA_13_TPS_PWRUP    (11)
#define BOARD_PCA_14_VCOM_CTRL    (12)
#define BOARD_PCA_15_TPS_WAKEUP   (13)
#define BOARD_PCA_16_TPS_PWR_GOOD (14)
#define BOARD_PCA_17_TPS_INT      (15)

// SD Card
#define BOARD_SD_CS    (47)
#define BOARD_SD_MISO  BOARD_SPI_MISO
#define BOARD_SD_SCK   BOARD_SPI_SCK 
#define BOARD_SD_MOSI  BOARD_SPI_MOSI

// Touch
#define BOARD_TOUCH_INT     (3)
#define BOARD_TOUCH_SDA     BOARD_I2C_SDA
#define BOARD_TOUCH_SCL     BOARD_I2C_SCL
#define BOARD_TOUCH_RST     BOARD_PCA_00_T_RST

// Display
#define BOARD_DISPALY_D7    (34)
#define BOARD_DISPALY_D6    (33)
#define BOARD_DISPALY_D5    (32)
#define BOARD_DISPALY_D4    (31)
#define BOARD_DISPALY_D3    (30)
#define BOARD_DISPALY_D2    (29)
#define BOARD_DISPALY_D1    (28)
#define BOARD_DISPALY_D0    (27)
#define BOARD_DISPALY_CKV   (13)
#define BOARD_DISPALY_STH   (25)
#define BOARD_DISPALY_LEH   (26)
#define BOARD_DISPALY_STV   (36)
#define BOARD_DISPALY_CKH   (24)
#define BOARD_DISPALY_LED1  (11)
#define BOARD_DISPALY_LED2  (12)

// ESP32C6 MINI
#define BOARD_C6_D0     (14)
#define BOARD_C6_D1     (15)
#define BOARD_C6_D2     (16)
#define BOARD_C6_D3     (17)
#define BOARD_C6_CLK    (18)
#define BOARD_C6_CMD    (19)
#define BOARD_C6_RST    (54)
#define BOARD_C6_WAKEUP (6)

// ES8311
#define BOARD_ES8311_I2C_SDA    BOARD_I2C_SDA
#define BOARD_ES8311_I2C_SCL    BOARD_I2C_SCL
#define BOARD_ES8311_I2S_MCLK   (43)
#define BOARD_ES8311_I2S_SCLK   (42)
#define BOARD_ES8311_I2S_ASDOUT (39)
#define BOARD_ES8311_I2S_LRCK   (40)
#define BOARD_ES8311_I2S_DSDIN  (41)

// MIPI to HDMI
#define BOARD_HDMI_RST (BOARD_PCA_06_HDMI_RST)
#define BOARD_HDMI_INT (4)
#define BOARD_HDMI_SDA (BOARD_I2C_SDA)
#define BOARD_HDMI_SCL (BOARD_I2C_SCL)
#define BOARD_HDMI_DDC_SDA (9)
#define BOARD_HDMI_DDC_SCL (10)
~~~


