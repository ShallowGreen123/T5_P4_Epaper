/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_types.h"
#include "sdkconfig.h"

#define ESP_LCD_COLOR_FORMAT_RGB565    (1)
#define ESP_LCD_COLOR_FORMAT_RGB888    (2)

#if CONFIG_BSP_LCD_COLOR_FORMAT_RGB888
#define BSP_LCD_COLOR_FORMAT        (ESP_LCD_COLOR_FORMAT_RGB888)
#define BSP_LCD_BITS_PER_PIXEL      (24)
#else
#define BSP_LCD_COLOR_FORMAT        (ESP_LCD_COLOR_FORMAT_RGB565)
#define BSP_LCD_BITS_PER_PIXEL      (16)
#endif

#define BSP_LCD_BIGENDIAN           (0)
#define BSP_LCD_COLOR_SPACE         (LCD_RGB_ELEMENT_ORDER_RGB)

#if CONFIG_BSP_LCD_HDMI_800x600_60HZ
#define BSP_LCD_H_RES               (800)
#define BSP_LCD_V_RES               (600)
#elif CONFIG_BSP_LCD_HDMI_1024x768_60HZ
#define BSP_LCD_H_RES               (1024)
#define BSP_LCD_V_RES               (768)
#elif CONFIG_BSP_LCD_HDMI_1280x720_60HZ
#define BSP_LCD_H_RES               (1280)
#define BSP_LCD_V_RES               (720)
#elif CONFIG_BSP_LCD_HDMI_1280x800_60HZ
#define BSP_LCD_H_RES               (1280)
#define BSP_LCD_V_RES               (800)
#elif CONFIG_BSP_LCD_HDMI_1920x1080_30HZ
#define BSP_LCD_H_RES               (1920)
#define BSP_LCD_V_RES               (1080)
#else
#define BSP_LCD_H_RES               (800)
#define BSP_LCD_V_RES               (600)
#endif

#define BSP_LCD_MIPI_DSI_LANE_NUM           (2)
#define BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS  (1000)
#define BSP_MIPI_DSI_PHY_PWR_LDO_CHAN       (3)
#define BSP_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV (2500)

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BSP_HDMI_RES_NONE = 0,
    BSP_HDMI_RES_800x600,
    BSP_HDMI_RES_1024x768,
    BSP_HDMI_RES_1280x720,
    BSP_HDMI_RES_1280x800,
    BSP_HDMI_RES_1920x1080,
} bsp_hdmi_resolution_t;

#if CONFIG_BSP_LCD_HDMI_800x600_60HZ
#define BSP_HDMI_DEFAULT_RESOLUTION BSP_HDMI_RES_800x600
#elif CONFIG_BSP_LCD_HDMI_1024x768_60HZ
#define BSP_HDMI_DEFAULT_RESOLUTION BSP_HDMI_RES_1024x768
#elif CONFIG_BSP_LCD_HDMI_1280x720_60HZ
#define BSP_HDMI_DEFAULT_RESOLUTION BSP_HDMI_RES_1280x720
#elif CONFIG_BSP_LCD_HDMI_1280x800_60HZ
#define BSP_HDMI_DEFAULT_RESOLUTION BSP_HDMI_RES_1280x800
#elif CONFIG_BSP_LCD_HDMI_1920x1080_30HZ
#define BSP_HDMI_DEFAULT_RESOLUTION BSP_HDMI_RES_1920x1080
#else
#define BSP_HDMI_DEFAULT_RESOLUTION BSP_HDMI_RES_800x600
#endif

typedef struct {
    bsp_hdmi_resolution_t hdmi_resolution;
    struct {
        mipi_dsi_phy_clock_source_t phy_clk_src;
        uint32_t lane_bit_rate_mbps;
    } dsi_bus;
} bsp_display_config_t;

typedef struct {
    esp_lcd_dsi_bus_handle_t  mipi_dsi_bus;
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_handle_t io_cec;
    esp_lcd_panel_io_handle_t io_avi;
    esp_lcd_panel_handle_t    panel;
    esp_lcd_panel_handle_t    control;
} bsp_lcd_handles_t;

esp_err_t bsp_display_new(const bsp_display_config_t *config,
                          esp_lcd_panel_handle_t *ret_panel,
                          esp_lcd_panel_io_handle_t *ret_io);
esp_err_t bsp_display_new_with_handles(const bsp_display_config_t *config,
                                       bsp_lcd_handles_t *ret_handles);
void bsp_display_delete(void);
esp_err_t bsp_display_brightness_init(void);
esp_err_t bsp_display_brightness_deinit(void);
esp_err_t bsp_display_brightness_set(int brightness_percent);
esp_err_t bsp_display_backlight_on(void);
esp_err_t bsp_display_backlight_off(void);

#ifdef __cplusplus
}
#endif
