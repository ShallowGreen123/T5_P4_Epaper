/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "sdkconfig.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "esp_codec_dev.h"
#include "esp_err.h"
#include "esp_vfs_fat.h"

#include "bsp/config.h"
#include "bsp/display.h"
#include <t5_p4_board.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_BOARD_T5_P4_E_PAPER

#define BSP_CAPS_DISPLAY       1
#define BSP_CAPS_TOUCH         0
#define BSP_CAPS_AUDIO         1
#define BSP_CAPS_AUDIO_SPEAKER 1
#define BSP_CAPS_AUDIO_MIC     1
#define BSP_CAPS_SDCARD        1

#define BSP_I2C_SCL            T5_BOARD_I2C_SCL
#define BSP_I2C_SDA            T5_BOARD_I2C_SDA

#define BSP_I2S_MCLK           T5_BOARD_ES8311_MCLK
#define BSP_I2S_BCLK           T5_BOARD_ES8311_BCLK
#define BSP_I2S_SCLK           BSP_I2S_BCLK
#define BSP_I2S_LRCK           T5_BOARD_ES8311_LRCK
#define BSP_I2S_LCLK           BSP_I2S_LRCK
#define BSP_I2S_DOUT           T5_BOARD_ES8311_DOUT
#define BSP_I2S_DIN            T5_BOARD_ES8311_DIN
#define BSP_I2S_DSIN           BSP_I2S_DIN

#define BSP_SD_SPI_MISO        T5_BOARD_SD_MISO
#define BSP_SD_SPI_CS          T5_BOARD_SD_CS
#define BSP_SD_SPI_MOSI        T5_BOARD_SD_MOSI
#define BSP_SD_SPI_CLK         T5_BOARD_SD_SCK

#define BSP_I2C_NUM            CONFIG_BSP_I2C_NUM
#define BSP_SD_MOUNT_POINT     CONFIG_BSP_SD_MOUNT_POINT
#define BSP_SDSPI_HOST         SDSPI_DEFAULT_HOST

#define BSP_I2S_GPIO_CFG       \
    {                          \
        .mclk = BSP_I2S_MCLK,  \
        .bclk = BSP_I2S_BCLK,  \
        .ws = BSP_I2S_LRCK,    \
        .dout = BSP_I2S_DOUT,  \
        .din = BSP_I2S_DIN,    \
        .invert_flags = {      \
            .mclk_inv = false, \
            .bclk_inv = false, \
            .ws_inv = false,   \
        },                     \
    }

typedef struct {
    const esp_vfs_fat_sdmmc_mount_config_t *mount;
    sdmmc_host_t *host;
    union {
        const sdmmc_slot_config_t *sdmmc;
        const sdspi_device_config_t *sdspi;
    } slot;
} bsp_sdcard_cfg_t;

esp_err_t bsp_i2c_init(void);
esp_err_t bsp_i2c_deinit(void);
i2c_master_bus_handle_t bsp_i2c_get_handle(void);

esp_err_t bsp_audio_init(const i2s_std_config_t *i2s_config);
esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void);
esp_codec_dev_handle_t bsp_audio_codec_microphone_init(void);

esp_err_t bsp_sdcard_mount(void);
esp_err_t bsp_sdcard_unmount(void);
sdmmc_card_t *bsp_sdcard_get_handle(void);
void bsp_sdcard_get_sdspi_host(const int slot, sdmmc_host_t *config);
void bsp_sdcard_sdspi_get_slot(const spi_host_device_t spi_host, sdspi_device_config_t *config);
esp_err_t bsp_sdcard_sdspi_mount(bsp_sdcard_cfg_t *cfg);

#ifdef __cplusplus
}
#endif
