/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <inttypes.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_private/esp_cache_private.h"
#include "esp_timer.h"

#include "bsp/esp-bsp.h"
#include "demos/lv_demos.h"
#include "lvgl.h"
#include "sdkconfig.h"

static const char *TAG = "hdmi_lvgl";

#if !CONFIG_BSP_LCD_TYPE_HDMI
#error "This example requires CONFIG_BSP_LCD_TYPE_HDMI=y"
#endif

#if !CONFIG_BSP_LCD_COLOR_FORMAT_RGB888
#error "This example requires CONFIG_BSP_LCD_COLOR_FORMAT_RGB888=y"
#endif

#define DISPLAY_OUTPUT_H_RES        BSP_LCD_H_RES
#define DISPLAY_OUTPUT_V_RES        BSP_LCD_V_RES
#define DISPLAY_LANE_BITRATE_MBPS   BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS
#define DISPLAY_PIXEL_COUNT         ((uint32_t)(DISPLAY_OUTPUT_H_RES * DISPLAY_OUTPUT_V_RES))
#define HDMI_RGB888_BYTES_PER_PIXEL 3U
#define HDMI_RGB888_BUFFER_SIZE     ((size_t)DISPLAY_PIXEL_COUNT * HDMI_RGB888_BYTES_PER_PIXEL)
#define DISPLAY_FLUSH_TIMEOUT_MS    1000
#define LVGL_TICK_PERIOD_MS         1
#define LVGL_TASK_MAX_DELAY_MS      10

static esp_lcd_panel_handle_t s_lcd_panel;
static esp_lcd_panel_io_handle_t s_lcd_io;
static SemaphoreHandle_t s_trans_done_sem;
static lv_disp_draw_buf_t s_lvgl_draw_buf;
static lv_color_t *s_lvgl_draw_buffer;
static uint8_t *s_hdmi_flush_buffer;
static esp_timer_handle_t s_lvgl_tick_timer;
static uint32_t s_flush_count;

static IRAM_ATTR bool flush_dpi_panel_ready_callback(esp_lcd_panel_handle_t panel,
                                                     esp_lcd_dpi_panel_event_data_t *edata,
                                                     void *user_ctx)
{
    (void)panel;
    (void)edata;
    (void)user_ctx;

    BaseType_t task_awake = pdFALSE;
    if (s_trans_done_sem) {
        xSemaphoreGiveFromISR(s_trans_done_sem, &task_awake);
    }
    return task_awake == pdTRUE;
}

static size_t get_psram_cache_line_size(void)
{
    size_t cache_line_size = 0;
    if (esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &cache_line_size) != ESP_OK || cache_line_size == 0) {
        cache_line_size = 64;
    }
    return cache_line_size;
}

static void convert_lvgl_to_hdmi_rgb888(const lv_color_t *src,
                                        uint32_t src_stride_px,
                                        uint8_t *dst,
                                        uint32_t width,
                                        uint32_t height)
{
    for (uint32_t y = 0; y < height; ++y) {
        const lv_color_t *src_row = src + (y * src_stride_px);
        uint8_t *dst_row = dst + ((size_t)y * width * HDMI_RGB888_BYTES_PER_PIXEL);

        for (uint32_t x = 0; x < width; ++x) {
            const lv_color_t color = src_row[x];
            const size_t dst_offset = (size_t)x * HDMI_RGB888_BYTES_PER_PIXEL;

            /* The existing HDMI/JPEG path uses BGR byte order for RGB888 panel data. */
            dst_row[dst_offset + 0] = LV_COLOR_GET_B(color);
            dst_row[dst_offset + 1] = LV_COLOR_GET_G(color);
            dst_row[dst_offset + 2] = LV_COLOR_GET_R(color);
        }
    }
}

static void lvgl_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    const int32_t area_w = area->x2 - area->x1 + 1;
    const int32_t area_h = area->y2 - area->y1 + 1;

    int32_t x1 = area->x1;
    int32_t y1 = area->y1;
    int32_t x2 = area->x2;
    int32_t y2 = area->y2;

    if (x2 < 0 || y2 < 0 || x1 >= DISPLAY_OUTPUT_H_RES || y1 >= DISPLAY_OUTPUT_V_RES) {
        lv_disp_flush_ready(disp_drv);
        return;
    }

    if (x1 < 0) {
        x1 = 0;
    }
    if (y1 < 0) {
        y1 = 0;
    }
    if (x2 >= DISPLAY_OUTPUT_H_RES) {
        x2 = DISPLAY_OUTPUT_H_RES - 1;
    }
    if (y2 >= DISPLAY_OUTPUT_V_RES) {
        y2 = DISPLAY_OUTPUT_V_RES - 1;
    }

    const uint32_t flush_w = (uint32_t)(x2 - x1 + 1);
    const uint32_t flush_h = (uint32_t)(y2 - y1 + 1);
    const uint32_t flush_px = flush_w * flush_h;

    if (flush_px > DISPLAY_PIXEL_COUNT || s_hdmi_flush_buffer == NULL || s_lcd_panel == NULL) {
        ESP_LOGE(TAG, "Invalid LVGL flush request: area=%" PRId32 "x%" PRId32 " buffer=%p panel=%p",
                 area_w, area_h, s_hdmi_flush_buffer, s_lcd_panel);
        lv_disp_flush_ready(disp_drv);
        return;
    }

    const lv_color_t *src = color_p + ((y1 - area->y1) * area_w) + (x1 - area->x1);
    convert_lvgl_to_hdmi_rgb888(src, (uint32_t)area_w, s_hdmi_flush_buffer, flush_w, flush_h);

    if (s_trans_done_sem) {
        xSemaphoreTake(s_trans_done_sem, 0);
    }

    esp_err_t ret = esp_lcd_panel_draw_bitmap(s_lcd_panel, x1, y1, x2 + 1, y2 + 1, s_hdmi_flush_buffer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to submit LVGL frame: %s", esp_err_to_name(ret));
        lv_disp_flush_ready(disp_drv);
        return;
    }

    if (s_trans_done_sem &&
        xSemaphoreTake(s_trans_done_sem, pdMS_TO_TICKS(DISPLAY_FLUSH_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Display flush timeout (%" PRIu32 "x%" PRIu32 " at %" PRId32 ",%" PRId32 ")",
                 flush_w, flush_h, x1, y1);
    }

    s_flush_count++;
    if (s_flush_count <= 3 || (s_flush_count % 120) == 0) {
        ESP_LOGI(TAG, "Flushed LVGL frame #%" PRIu32 " (%" PRIu32 "x%" PRIu32 ")",
                 s_flush_count, flush_w, flush_h);
    }

    lv_disp_flush_ready(disp_drv);
}

#if !LV_TICK_CUSTOM
static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}
#endif

static esp_err_t allocate_lvgl_buffers(void)
{
    const size_t cache_line_size = get_psram_cache_line_size();

    s_lvgl_draw_buffer = heap_caps_aligned_calloc(cache_line_size,
                                                  DISPLAY_PIXEL_COUNT,
                                                  sizeof(lv_color_t),
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_lvgl_draw_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate LVGL draw buffer (%u pixels)", (unsigned)DISPLAY_PIXEL_COUNT);
        return ESP_ERR_NO_MEM;
    }

    s_hdmi_flush_buffer = heap_caps_aligned_calloc(cache_line_size,
                                                  1,
                                                  HDMI_RGB888_BUFFER_SIZE,
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_hdmi_flush_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate HDMI RGB888 flush buffer (%u bytes)",
                 (unsigned)HDMI_RGB888_BUFFER_SIZE);
        heap_caps_free(s_lvgl_draw_buffer);
        s_lvgl_draw_buffer = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Allocated LVGL draw buffer=%u bytes, HDMI flush buffer=%u bytes",
             (unsigned)(DISPLAY_PIXEL_COUNT * sizeof(lv_color_t)),
             (unsigned)HDMI_RGB888_BUFFER_SIZE);
    return ESP_OK;
}

static esp_err_t init_hdmi_display(void)
{
    bsp_display_config_t display_config = {
        .hdmi_resolution = BSP_HDMI_DEFAULT_RESOLUTION,
        .dsi_bus = {
            .phy_clk_src = 0,
            .lane_bit_rate_mbps = DISPLAY_LANE_BITRATE_MBPS,
        },
    };

    ESP_LOGI(TAG, "Display timing: %dx%d, DSI lane bitrate: %d Mbps",
             DISPLAY_OUTPUT_H_RES, DISPLAY_OUTPUT_V_RES, DISPLAY_LANE_BITRATE_MBPS);

    esp_err_t ret = bsp_display_new(&display_config, &s_lcd_panel, &s_lcd_io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize HDMI display: %s", esp_err_to_name(ret));
        return ret;
    }

    s_trans_done_sem = xSemaphoreCreateBinary();
    if (s_trans_done_sem == NULL) {
        ESP_LOGE(TAG, "Failed to create display flush semaphore");
        return ESP_ERR_NO_MEM;
    }

    const esp_lcd_dpi_panel_event_callbacks_t callbacks = {
        .on_color_trans_done = flush_dpi_panel_ready_callback,
    };
    ret = esp_lcd_dpi_panel_register_event_callbacks(s_lcd_panel, &callbacks, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register DPI panel callbacks: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

static esp_err_t init_lvgl_port(void)
{
    lv_init();
    esp_err_t ret = ESP_OK;

#if !LV_TICK_CUSTOM
    const esp_timer_create_args_t tick_timer_args = {
        .callback = lvgl_tick_cb,
        .name = "lvgl_tick",
    };
    ret = esp_timer_create(&tick_timer_args, &s_lvgl_tick_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LVGL tick timer: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_timer_start_periodic(s_lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start LVGL tick timer: %s", esp_err_to_name(ret));
        return ret;
    }
#endif

    ret = allocate_lvgl_buffers();
    if (ret != ESP_OK) {
        return ret;
    }

    lv_disp_draw_buf_init(&s_lvgl_draw_buf, s_lvgl_draw_buffer, NULL, DISPLAY_PIXEL_COUNT);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = DISPLAY_OUTPUT_H_RES;
    disp_drv.ver_res = DISPLAY_OUTPUT_V_RES;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &s_lvgl_draw_buf;
    disp_drv.full_refresh = 1;
    lv_disp_drv_register(&disp_drv);

    return ESP_OK;
}

static void start_selected_demo(void)
{
#if CONFIG_HDMI_LVGL_DEMO_BENCHMARK
    ESP_LOGI(TAG, "Starting LVGL Benchmark demo");
    lv_demo_benchmark();
#elif CONFIG_HDMI_LVGL_DEMO_STRESS
    ESP_LOGI(TAG, "Starting LVGL Stress demo");
    lv_demo_stress();
#elif CONFIG_HDMI_LVGL_DEMO_WIDGETS
    ESP_LOGI(TAG, "Starting LVGL Widgets demo");
    lv_demo_widgets();
#else
#error "No HDMI LVGL demo selected"
#endif
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting HDMI LVGL Demo Runner");
    ESP_LOGI(TAG, "Free SPIRAM before init: %u bytes",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    esp_err_t ret = init_hdmi_display();
    if (ret != ESP_OK) {
        return;
    }

    ret = init_lvgl_port();
    if (ret != ESP_OK) {
        return;
    }

    start_selected_demo();

    while (true) {
        uint32_t delay_ms = lv_timer_handler();
        if (delay_ms == 0) {
            delay_ms = 1;
        } else if (delay_ms > LVGL_TASK_MAX_DELAY_MS) {
            delay_ms = LVGL_TASK_MAX_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}
