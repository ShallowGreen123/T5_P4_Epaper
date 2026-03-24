#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_types.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/sdmmc_host.h"

// Local BSP compatibility layer for the T5-P4 board.
// The current PlatformIO Arduino environment does not expose the full official
// ESP-BSP package set, so this header keeps the BSP-style API surface stable
// while the implementation adapts the local board hardware directly.

typedef void *esp_codec_dev_handle_t;

typedef enum {
    BSP_HDMI_RES_NONE = 0,
    BSP_HDMI_RES_800x600,
    BSP_HDMI_RES_1024x768,
    BSP_HDMI_RES_1280x720,
    BSP_HDMI_RES_1280x800,
    BSP_HDMI_RES_1920x1080,
} bsp_hdmi_resolution_t;

typedef struct {
    bsp_hdmi_resolution_t hdmi_resolution;
    struct {
        mipi_dsi_phy_pllref_clock_source_t phy_clk_src;
        uint32_t lane_bit_rate_mbps;
    } dsi_bus;
} bsp_display_config_t;

// Fixed phase-1 target: HDMI 1280x720 RGB888.
#define BSP_LCD_H_RES (1280)
#define BSP_LCD_V_RES (720)
#define BSP_LCD_MIPI_DSI_LANE_NUM (2)
#define BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS (1000)
#define BSP_SD_MOUNT_POINT "/sdcard"

// Board I2C addresses.
#define BOARD_I2C_ADDR_ES8311 (0x18)
#define BOARD_I2C_ADDR_PCA9535 (0x20)
#define BOARD_I2C_ADDR_LT8912 (0x48)

// Main I2C bus.
#define BOARD_I2C_SDA (7)
#define BOARD_I2C_SCL (8)

// HDMI related GPIOs.
#define BOARD_HDMI_INT (4)
#define BOARD_HDMI_DDC_SDA (9)
#define BOARD_HDMI_DDC_SCL (10)

// SPI SD card.
#define BOARD_SD_MISO (44)
#define BOARD_SD_SCK (45)
#define BOARD_SD_MOSI (46)
#define BOARD_SD_CS (47)

// ES8311 I2S pins.
#define BOARD_ES8311_I2S_MCLK (43)
#define BOARD_ES8311_I2S_SCLK (42)
#define BOARD_ES8311_I2S_LRCK (40)
#define BOARD_ES8311_I2S_DSDIN (41)
#define BOARD_ES8311_I2S_ASDOUT (39)

// PCA9535 outputs used by this demo.
#define BOARD_PCA_05_SHUTDOWN (5)
#define BOARD_PCA_06_HDMI_RST (6)
#define BOARD_PCA_07_HDMI_EN (7)
#define BOARD_PCA_12_1V8_EN (10)

esp_err_t bsp_i2c_init(void);
i2c_master_bus_handle_t bsp_i2c_get_handle(void);

esp_err_t bsp_hdmi_power_on(void);
esp_err_t bsp_display_new(const bsp_display_config_t *config,
                          esp_lcd_panel_handle_t *ret_panel,
                          esp_lcd_panel_io_handle_t *ret_io);
void bsp_display_delete(void);

esp_err_t bsp_sdcard_mount(void);
sdmmc_card_t *bsp_sdcard_get_handle(void);

esp_err_t bsp_audio_init(const i2s_std_config_t *i2s_config);
esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void);
