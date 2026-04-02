
主因大概率是 GPIO36 的启动脚冲突，不是软件初始化。

屏幕的 STV 直接接到了 GPIO36，而 Espressif 对 ESP32-P4 的下载条件明确要求 `GPIO35=0`、`GPIO36=1`，并且这些 strap 会在 EN 拉高后的很短窗口里被采样。屏幕一插上，GPIO36 就不再是“空着的 strap 脚”，而是被面板/走线/负载占住了，所以复位时很容易不是稳定高电平，ROM 就不会进下载模式。

如果你走的是原生 USB 下载，还要额外警惕 GPIO24/25 的冲突，因为这块板的 EPD CKH/STH 也占了这两个脚。也就是说，这块板的屏幕不只是“显示器”，它还把几个下载/启动相关脚直接占用了。参考：原理图复盘、原理图复盘、ESP32-P4 下载条件。

最直接的验证是先拔屏再进下载模式；或者插屏时用示波器看 GPIO36 在 EN 拉高后的采样窗里是不是还保持高电平。要想边插屏边烧录，通常得给 STV 做隔离/缓冲，或者让面板侧在下载阶段高阻。

[ESP32-P4 芯片启动条件](https://docs.espressif.com/projects/esp-techpedia/zh_CN/latest/esp-friends/get-started/try-firmware/try-firmware-hardware/esp32p4.html)

[ESP硬件设计指南](https://docs.espressif.com/projects/esp-hardware-design-guidelines/zh_CN/latest/esp32p4/schematic-checklist-esp32p4.html)

---

## :four: 引脚 🎁
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


