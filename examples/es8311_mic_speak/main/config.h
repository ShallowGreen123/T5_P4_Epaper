#pragma once
#include "sdkconfig.h"

/* Example configurations */
#define EXAMPLE_RECV_BUF_SIZE   (2400)
#define EXAMPLE_SAMPLE_RATE     (16000)
#define EXAMPLE_MCLK_MULTIPLE   (384) // If not using 24-bit data width, 256 should be enough
#define EXAMPLE_MCLK_FREQ_HZ    (EXAMPLE_SAMPLE_RATE * EXAMPLE_MCLK_MULTIPLE)
#define EXAMPLE_VOICE_VOLUME    CONFIG_EXAMPLE_VOICE_VOLUME

/* I2C port and GPIOs */
#define I2C_NUM         (0)
/* I2S port and GPIOs */
#define I2S_NUM         (0)

// IIC
#define BOARD_I2C_SDA       (7)
#define BOARD_I2C_SCL       (8)

#define I2C_SCL_IO      (BOARD_I2C_SCL)
#define I2C_SDA_IO      (BOARD_I2C_SDA)

// PCA9535PW  --  IO expansion
#define BOARD_PCA_INT             (5)
#define BOARD_PCA_SDA             BOARD_I2C_SDA
#define BOARD_PCA_SCL             BOARD_I2C_SCL
#define BOARD_PCA_00_T_RST        (0x0001)
#define BOARD_PCA_01_CC_SW0       (0x0002)
#define BOARD_PCA_02_CC_SW1       (0x0004)
#define BOARD_PCA_03_LR_RST       (0x0008)
#define BOARD_PCA_04_NRF_CE       (0x0010)
#define BOARD_PCA_05_SHUTDOWN     (0x0020)
#define BOARD_PCA_06_HDMI_RST     (0x0040)
#define BOARD_PCA_07_HDMI_EN      (0x0080)
#define BOARD_PCA_10_EP_OE        (0x0100 >> 8)
#define BOARD_PCA_11_EP_MODE      (0x0200 >> 8)
#define BOARD_PCA_12_1V8_EN       (0x0400 >> 8)
#define BOARD_PCA_13_TPS_PWRUP    (0x0800 >> 8)
#define BOARD_PCA_14_VCOM_CTRL    (0x1000 >> 8)
#define BOARD_PCA_15_TPS_WAKEUP   (0x2000 >> 8)
#define BOARD_PCA_16_TPS_PWR_GOOD (0x4000 >> 8)
#define BOARD_PCA_17_TPS_INT      (0x8000 >> 8)

// ES8311
#define I2S_MCK_IO      (GPIO_NUM_43)
#define I2S_BCK_IO      (GPIO_NUM_42)
#define I2S_WS_IO       (GPIO_NUM_40)
#define I2S_DO_IO       (GPIO_NUM_41)
#define I2S_DI_IO       (GPIO_NUM_39)





