/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define T5_BOARD_I2C_SDA                GPIO_NUM_7
#define T5_BOARD_I2C_SCL                GPIO_NUM_8
#define T5_BOARD_TOUCH_INT_GPIO         GPIO_NUM_3
#define T5_BOARD_HDMI_INT_GPIO          GPIO_NUM_4
#define T5_BOARD_PCA9535_INT_GPIO       GPIO_NUM_5

#define T5_BOARD_SD_MISO                GPIO_NUM_44
#define T5_BOARD_SD_SCK                 GPIO_NUM_45
#define T5_BOARD_SD_MOSI                GPIO_NUM_46
#define T5_BOARD_SD_CS                  GPIO_NUM_47

#define T5_BOARD_ES8311_MCLK            GPIO_NUM_43
#define T5_BOARD_ES8311_BCLK            GPIO_NUM_42
#define T5_BOARD_ES8311_LRCK            GPIO_NUM_40
#define T5_BOARD_ES8311_DOUT            GPIO_NUM_39
#define T5_BOARD_ES8311_DIN             GPIO_NUM_41

#define T5_BOARD_I2C_ADDR_ES8311        (0x18)
#define T5_BOARD_I2C_ADDR_PCA9535       (0x20)
#define T5_BOARD_I2C_ADDR_LT8912B_MAIN  (0x48)
#define T5_BOARD_I2C_ADDR_LT8912B_CEC   (0x49)
#define T5_BOARD_I2C_ADDR_LT8912B_AVI   (0x4A)
#define T5_BOARD_I2C_ADDR_TPS651851     (0x68)

#define T5_BOARD_PCA_IO_TOUCH_RESET     (0)
#define T5_BOARD_PCA_IO_CC_SW0          (1)
#define T5_BOARD_PCA_IO_CC_SW1          (2)
#define T5_BOARD_PCA_IO_LR_RESET        (3)
#define T5_BOARD_PCA_IO_NRF_CE          (4)
#define T5_BOARD_PCA_IO_AUDIO_SHUTDOWN  (5)
#define T5_BOARD_PCA_IO_HDMI_RESET      (6)
#define T5_BOARD_PCA_IO_HDMI_ENABLE     (7)
#define T5_BOARD_PCA_IO_EPD_OE          (8)
#define T5_BOARD_PCA_IO_EPD_MODE        (9)
#define T5_BOARD_PCA_IO_HDMI_1V8_ENABLE (10)
#define T5_BOARD_PCA_IO_TPS_PWRUP       (11)
#define T5_BOARD_PCA_IO_VCOM_CTRL       (12)
#define T5_BOARD_PCA_IO_TPS_WAKEUP      (13)
#define T5_BOARD_PCA_IO_TPS_PWR_GOOD    (14)
#define T5_BOARD_PCA_IO_TPS_INT         (15)

esp_err_t t5_board_pca9535_init(void);
esp_err_t t5_board_pca9535_set_level(uint8_t io_num, bool level);
esp_err_t t5_board_pca9535_get_level(uint8_t io_num, bool *level);
esp_err_t t5_board_hdmi_power_on(void);
esp_err_t t5_board_hdmi_power_off(void);
esp_err_t t5_board_audio_amp_enable(bool enable);

#ifdef __cplusplus
}
#endif
