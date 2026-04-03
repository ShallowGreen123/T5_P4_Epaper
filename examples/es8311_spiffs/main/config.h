#pragma once

#include "sdkconfig.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/i2s_std.h"

#define EXAMPLE_SPIFFS_BASE_PATH         "/spiffs"
#define EXAMPLE_SPIFFS_PARTITION_LABEL   "storage"
#define EXAMPLE_MAX_FILE_PATH            (320)

#define EXAMPLE_DEFAULT_SAMPLE_RATE      (44100)
#define EXAMPLE_DEFAULT_BITS_PER_SAMPLE  I2S_DATA_BIT_WIDTH_16BIT
#define EXAMPLE_DEFAULT_SLOT_MODE        I2S_SLOT_MODE_STEREO
#define EXAMPLE_MCLK_MULTIPLE            I2S_MCLK_MULTIPLE_256
#define EXAMPLE_ES8311_MCLK_HZ(rate)     ((rate) * 256)
#define EXAMPLE_VOICE_VOLUME             CONFIG_EXAMPLE_VOICE_VOLUME

#define EXAMPLE_PLAYBACK_TIMEOUT_MS      (CONFIG_EXAMPLE_PLAYBACK_TIMEOUT_SECONDS * 1000)
#define I2C_OP_TIMEOUT_MS                (1000)
#define I2C_MASTER_CLK_SPEED             (100000)

#define I2C_NUM                          I2C_NUM_0
#define I2S_NUM                          I2S_NUM_0

#define BOARD_I2C_SDA                    GPIO_NUM_7
#define BOARD_I2C_SCL                    GPIO_NUM_8

#define I2C_SDA_IO                       BOARD_I2C_SDA
#define I2C_SCL_IO                       BOARD_I2C_SCL

// PCA9535 IO expander
#define BOARD_PCA_05_SHUTDOWN            (0x0020)

#define I2S_MCK_IO                       GPIO_NUM_43
#define I2S_BCK_IO                       GPIO_NUM_42
#define I2S_WS_IO                        GPIO_NUM_40
#define I2S_DO_IO                        GPIO_NUM_39
#define I2S_DI_IO                        GPIO_NUM_41
