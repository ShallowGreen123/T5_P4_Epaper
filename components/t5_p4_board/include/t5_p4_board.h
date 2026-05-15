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
#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

#define T5_BOARD_I2C_SDA                 GPIO_NUM_7
#define T5_BOARD_I2C_SCL                 GPIO_NUM_8
#define T5_BOARD_TOUCH_INT_GPIO          GPIO_NUM_3
#define T5_BOARD_HDMI_INT_GPIO           GPIO_NUM_4
#define T5_BOARD_PCA9535_INT_GPIO        GPIO_NUM_5
#define T5_BOARD_XL9555_INT_GPIO         GPIO_NUM_6

#define T5_BOARD_SD_MISO                 GPIO_NUM_44
#define T5_BOARD_SD_SCK                  GPIO_NUM_45
#define T5_BOARD_SD_MOSI                 GPIO_NUM_46
#define T5_BOARD_SD_CS                   GPIO_NUM_47

#define T5_BOARD_EPD_CKV                 GPIO_NUM_13
#define T5_BOARD_EPD_CKH                 GPIO_NUM_24
#define T5_BOARD_EPD_STH                 GPIO_NUM_25
#define T5_BOARD_EPD_LEH                 GPIO_NUM_26
#define T5_BOARD_EPD_STV                 GPIO_NUM_48
#define T5_BOARD_FRONTLIGHT_LED1         GPIO_NUM_53
#define T5_BOARD_FRONTLIGHT_LED2         GPIO_NUM_NC

#define T5_BOARD_ES8311_MCLK             GPIO_NUM_43
#define T5_BOARD_ES8311_BCLK             GPIO_NUM_42
#define T5_BOARD_ES8311_LRCK             GPIO_NUM_40
#define T5_BOARD_ES8311_DOUT             GPIO_NUM_39
#define T5_BOARD_ES8311_DIN              GPIO_NUM_41

#define T5_BOARD_C6_SDIO_D0              GPIO_NUM_14
#define T5_BOARD_C6_SDIO_D1              GPIO_NUM_15
#define T5_BOARD_C6_SDIO_D2              GPIO_NUM_16
#define T5_BOARD_C6_SDIO_D3              GPIO_NUM_17
#define T5_BOARD_C6_SDIO_CLK             GPIO_NUM_18
#define T5_BOARD_C6_SDIO_CMD             GPIO_NUM_19
#define T5_BOARD_C6_HOST_RESET_GPIO      GPIO_NUM_54

#define T5_BOARD_I2C_ADDR_ES8311         (0x18)
#define T5_BOARD_I2C_ADDR_PCA9535        (0x20)
#define T5_BOARD_I2C_ADDR_XL9555         (0x22)
#define T5_BOARD_I2C_ADDR_SGM38121       (0x28)
#define T5_BOARD_I2C_ADDR_LT8912B_MAIN   (0x48)
#define T5_BOARD_I2C_ADDR_LT8912B_CEC    (0x49)
#define T5_BOARD_I2C_ADDR_LT8912B_AVI    (0x4A)
#define T5_BOARD_I2C_ADDR_TPS651851      (0x68)

#define T5_BOARD_XL_IO_TOUCH_RESET       (0)
#define T5_BOARD_XL_IO_CC_SW0            (1)
#define T5_BOARD_XL_IO_CC_SW1            (2)
#define T5_BOARD_XL_IO_LR_RESET          (3)
#define T5_BOARD_XL_IO_NRF_CE            (4)
#define T5_BOARD_XL_IO_AUDIO_SHUTDOWN    (6)
#define T5_BOARD_XL_IO_AUDIO_SEL         (7)
#define T5_BOARD_XL_IO_HDMI_RESET        (8)
#define T5_BOARD_XL_IO_HDMI_ENABLE       (9)
#define T5_BOARD_XL_IO_SENSOR_IRQ        (10)
#define T5_BOARD_XL_IO_C6_RESET          (11)
#define T5_BOARD_XL_IO_C6_WAKEUP         (12)

#define T5_BOARD_PCA_IO_EPD_OE           (8)
#define T5_BOARD_PCA_IO_EPD_MODE         (9)
#define T5_BOARD_PCA_IO_TPS_PWRUP        (11)
#define T5_BOARD_PCA_IO_VCOM_CTRL        (12)
#define T5_BOARD_PCA_IO_TPS_WAKEUP       (13)
#define T5_BOARD_PCA_IO_TPS_PWR_GOOD     (14)
#define T5_BOARD_PCA_IO_TPS_INT          (15)

typedef struct {
    int dvdd1_mv;
    int dvdd2_mv;
    int avdd1_mv;
    int avdd2_mv;
} t5_board_camera_power_config_t;

esp_err_t t5_board_init(void);

esp_err_t t5_board_xl9555_init(void);
esp_err_t t5_board_xl9555_set_level(uint8_t io_num, bool level);
esp_err_t t5_board_xl9555_get_level(uint8_t io_num, bool *level);

esp_err_t t5_board_pca9535_init(void);
esp_err_t t5_board_pca9535_set_level(uint8_t io_num, bool level);
esp_err_t t5_board_pca9535_get_level(uint8_t io_num, bool *level);

esp_err_t t5_board_gt911_reset(uint32_t address);
esp_err_t t5_board_touch_new(uint16_t width,
                             uint16_t height,
                             esp_lcd_touch_handle_t *ret_touch,
                             esp_lcd_panel_io_handle_t *ret_io,
                             uint32_t *ret_address);
void t5_board_touch_delete(esp_lcd_touch_handle_t touch, esp_lcd_panel_io_handle_t io);

esp_err_t t5_board_audio_amp_enable(bool enable);
esp_err_t t5_board_audio_select_speaker(bool speaker_enabled);

esp_err_t t5_board_hdmi_power_on(void);
esp_err_t t5_board_hdmi_power_off(void);

esp_err_t t5_board_c6_set_reset(bool enable);
esp_err_t t5_board_c6_set_wakeup(bool enable);
esp_err_t t5_board_c6_bootstrap(void);
gpio_num_t t5_board_c6_host_reset_gpio(void);

esp_err_t t5_board_frontlight_init(void);
esp_err_t t5_board_frontlight_set(uint8_t led1_duty, uint8_t led2_duty);

esp_err_t t5_board_camera_power_on(const t5_board_camera_power_config_t *config);
esp_err_t t5_board_camera_power_off(void);

#ifdef __cplusplus
}
#endif
