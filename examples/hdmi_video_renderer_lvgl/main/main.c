/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/ppa.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_lt8912b.h"
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

#if CONFIG_BSP_LCD_LT8912B_TEST_PATTERN
#error "Disable CONFIG_BSP_LCD_LT8912B_TEST_PATTERN to display the LVGL demo"
#endif

#define DISPLAY_OUTPUT_H_RES        BSP_LCD_H_RES
#define DISPLAY_OUTPUT_V_RES        BSP_LCD_V_RES
#define DISPLAY_LANE_BITRATE_MBPS   BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS
#define DISPLAY_PIXEL_COUNT         ((uint32_t)(DISPLAY_OUTPUT_H_RES * DISPLAY_OUTPUT_V_RES))
#define HDMI_RGB888_BYTES_PER_PIXEL 3U
#define HDMI_RGB888_BUFFER_SIZE     ((size_t)DISPLAY_PIXEL_COUNT * HDMI_RGB888_BYTES_PER_PIXEL)
#define LVGL_DRAW_BUFFER_LINES      ((uint32_t)CONFIG_HDMI_LVGL_DRAW_BUF_LINES)
#define LVGL_DRAW_BUFFER_PIXELS     ((uint32_t)DISPLAY_OUTPUT_H_RES * LVGL_DRAW_BUFFER_LINES)
#define LVGL_DRAW_BUFFER_SIZE       ((size_t)LVGL_DRAW_BUFFER_PIXELS * sizeof(lv_color_t))
#define LVGL_TICK_PERIOD_MS         1
#define LVGL_TASK_MAX_DELAY_MS      10

#if LV_COLOR_DEPTH == 16
#if LV_COLOR_16_SWAP
#error "The HDMI PPA flush path requires LV_COLOR_16_SWAP=0"
#endif
#define LVGL_PPA_COLOR_MODE PPA_SRM_COLOR_MODE_RGB565
#elif LV_COLOR_DEPTH == 32
#define LVGL_PPA_COLOR_MODE PPA_SRM_COLOR_MODE_ARGB8888
#else
#error "The HDMI PPA flush path supports LVGL RGB565 or ARGB8888 only"
#endif

typedef struct {
    ppa_client_handle_t client;
    esp_lcd_panel_handle_t panel;
    esp_lcd_draw_bitmap_hook_data_t hook_data;
} hdmi_ppa_hook_context_t;

static esp_lcd_panel_handle_t s_lcd_panel;
static esp_lcd_panel_io_handle_t s_lcd_io;
static uint8_t *s_hdmi_frame_buffer;
static ppa_client_handle_t s_ppa_client;
static hdmi_ppa_hook_context_t s_ppa_hook_context;
static lv_disp_draw_buf_t s_lvgl_draw_buf;
static lv_disp_drv_t s_lvgl_disp_drv;
static lv_color_t *s_lvgl_draw_buffers[2];
static esp_timer_handle_t s_lvgl_tick_timer;
static uint32_t s_flush_count;
static uint32_t s_refresh_count;
static uint32_t s_refresh_window_count;
static int64_t s_refresh_window_start_us;

static IRAM_ATTR bool ppa_srm_trans_done_callback(ppa_client_handle_t client,
                                                  ppa_event_data_t *edata,
                                                  void *user_ctx)
{
    (void)client;
    (void)edata;

    hdmi_ppa_hook_context_t *hook_context = (hdmi_ppa_hook_context_t *)user_ctx;
    if (hook_context != NULL && hook_context->hook_data.on_hook_end != NULL) {
        return hook_context->hook_data.on_hook_end(hook_context->panel);
    }

    return false;
}

static IRAM_ATTR bool lvgl_flush_ready_callback(esp_lcd_panel_handle_t panel,
                                                esp_lcd_dpi_panel_event_data_t *edata,
                                                void *user_ctx)
{
    (void)panel;
    (void)edata;

    lv_disp_drv_t *disp_drv = (lv_disp_drv_t *)user_ctx;
    if (disp_drv != NULL) {
        lv_disp_flush_ready(disp_drv);
    }
    return false;
}

static size_t get_psram_cache_line_size(void)
{
    size_t cache_line_size = 0;
    if (esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &cache_line_size) != ESP_OK || cache_line_size == 0) {
        cache_line_size = 64;
    }
    return cache_line_size;
}

static esp_err_t ppa_draw_bitmap_hook(esp_lcd_panel_handle_t panel,
                                      const esp_lcd_draw_bitmap_hook_data_t *hook_data,
                                      void *user_ctx)
{
    hdmi_ppa_hook_context_t *hook_context = (hdmi_ppa_hook_context_t *)user_ctx;
    if (hook_context == NULL || hook_context->client == NULL || hook_data == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const int src_block_w = hook_data->src_x_end - hook_data->src_x_start;
    const int src_block_h = hook_data->src_y_end - hook_data->src_y_start;
    const int dst_block_w = hook_data->dst_x_end - hook_data->dst_x_start;
    const int dst_block_h = hook_data->dst_y_end - hook_data->dst_y_start;
    const size_t output_buffer_size = (size_t)hook_data->dst_x_size *
                                      (size_t)hook_data->dst_y_size *
                                      HDMI_RGB888_BYTES_PER_PIXEL;

    if (panel != hook_context->panel || hook_data->bits_per_pixel != 24 ||
        src_block_w <= 0 || src_block_h <= 0 ||
        src_block_w != dst_block_w || src_block_h != dst_block_h ||
        output_buffer_size > UINT32_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&hook_context->hook_data, hook_data, sizeof(*hook_data));

    const ppa_srm_oper_config_t ppa_config = {
        .in = {
            .buffer = hook_data->src_data,
            .pic_w = (uint32_t)hook_data->src_x_size,
            .pic_h = (uint32_t)hook_data->src_y_size,
            .block_w = (uint32_t)src_block_w,
            .block_h = (uint32_t)src_block_h,
            .block_offset_x = (uint32_t)hook_data->src_x_start,
            .block_offset_y = (uint32_t)hook_data->src_y_start,
            .srm_cm = LVGL_PPA_COLOR_MODE,
        },
        .out = {
            .buffer = hook_data->dst_data,
            .buffer_size = (uint32_t)output_buffer_size,
            .pic_w = (uint32_t)hook_data->dst_x_size,
            .pic_h = (uint32_t)hook_data->dst_y_size,
            .block_offset_x = (uint32_t)hook_data->dst_x_start,
            .block_offset_y = (uint32_t)hook_data->dst_y_start,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB888,
        },
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = 1.0f,
        .scale_y = 1.0f,
        .mode = PPA_TRANS_MODE_NON_BLOCKING,
        .user_data = hook_context,
    };

    return ppa_do_scale_rotate_mirror(hook_context->client, &ppa_config);
}

static void fill_boot_diagnostic_frame(uint8_t *buffer, uint32_t width, uint32_t height)
{
    if (buffer == NULL) {
        return;
    }

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            uint8_t shade = 0x10;
            if (x < (width / 3U)) {
                shade = 0xFF;
            } else if (x < ((width * 2U) / 3U)) {
                shade = 0x88;
            }

            const size_t offset = ((size_t)y * width + x) * HDMI_RGB888_BYTES_PER_PIXEL;
            buffer[offset + 0] = shade;
            buffer[offset + 1] = shade;
            buffer[offset + 2] = shade;
        }
    }
}

static esp_err_t submit_boot_diagnostic_frame(void)
{
    if (s_lcd_panel == NULL || s_hdmi_frame_buffer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    fill_boot_diagnostic_frame(s_hdmi_frame_buffer, DISPLAY_OUTPUT_H_RES, DISPLAY_OUTPUT_V_RES);

    esp_err_t ret = esp_lcd_panel_draw_bitmap(s_lcd_panel, 0, 0,
                                              DISPLAY_OUTPUT_H_RES, DISPLAY_OUTPUT_V_RES,
                                              s_hdmi_frame_buffer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to submit boot diagnostic frame: %s", esp_err_to_name(ret));
        return ret;
    }

    const bool hdmi_ready = esp_lcd_panel_lt8912b_is_ready(s_lcd_panel);
    ESP_LOGI(TAG, "Submitted boot diagnostic frame, LT8912 ready=%s", hdmi_ready ? "yes" : "no");
    return ESP_OK;
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

    if (flush_px > LVGL_DRAW_BUFFER_PIXELS || s_ppa_client == NULL || s_lcd_panel == NULL) {
        ESP_LOGE(TAG, "Invalid LVGL flush request: area=%" PRId32 "x%" PRId32 " buffer=%p panel=%p",
                 area_w, area_h, color_p, s_lcd_panel);
        lv_disp_flush_ready(disp_drv);
        return;
    }

    const bool is_last = lv_disp_flush_is_last(disp_drv);
    const int32_t src_x_start = x1 - area->x1;
    const int32_t src_y_start = y1 - area->y1;
    esp_err_t ret = esp_lcd_panel_draw_bitmap_2d(s_lcd_panel,
                                                 x1, y1, x2 + 1, y2 + 1,
                                                 color_p,
                                                 area_w, area_h,
                                                 src_x_start, src_y_start,
                                                 src_x_start + (int32_t)flush_w,
                                                 src_y_start + (int32_t)flush_h);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to submit PPA LVGL flush: %s", esp_err_to_name(ret));
        lv_disp_flush_ready(disp_drv);
        return;
    }

    s_flush_count++;
    if (s_flush_count <= 3) {
        ESP_LOGI(TAG, "Queued PPA flush #%" PRIu32 " (%" PRIu32 "x%" PRIu32 " at %" PRId32 ",%" PRId32 ")",
                 s_flush_count, flush_w, flush_h, x1, y1);
    }

    if (is_last) {
        s_refresh_count++;
        s_refresh_window_count++;

        const int64_t now_us = esp_timer_get_time();
        if (s_refresh_window_start_us == 0) {
            s_refresh_window_start_us = now_us;
        } else if ((now_us - s_refresh_window_start_us) >= 2000000) {
            const float refresh_rate = (float)s_refresh_window_count * 1000000.0f /
                                       (float)(now_us - s_refresh_window_start_us);
            ESP_LOGI(TAG, "LVGL refresh submit rate: %.1f FPS (refreshes=%" PRIu32 ", flushes=%" PRIu32 ")",
                     (double)refresh_rate, s_refresh_count, s_flush_count);
            s_refresh_window_count = 0;
            s_refresh_window_start_us = now_us;
        }
    }
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

    for (size_t i = 0; i < 2; ++i) {
        s_lvgl_draw_buffers[i] = heap_caps_aligned_calloc(cache_line_size,
                                                          1,
                                                          LVGL_DRAW_BUFFER_SIZE,
                                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
        if (s_lvgl_draw_buffers[i] == NULL) {
            ESP_LOGE(TAG, "Failed to allocate LVGL draw buffer %u (%u bytes)",
                     (unsigned)i, (unsigned)LVGL_DRAW_BUFFER_SIZE);
            for (size_t j = 0; j < i; ++j) {
                heap_caps_free(s_lvgl_draw_buffers[j]);
                s_lvgl_draw_buffers[j] = NULL;
            }
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_LOGI(TAG, "Allocated two %u-line LVGL draw buffers: %u bytes total, color depth=%d",
             (unsigned)LVGL_DRAW_BUFFER_LINES,
             (unsigned)(LVGL_DRAW_BUFFER_SIZE * 2U),
             LV_COLOR_DEPTH);
    return ESP_OK;
}

static esp_err_t init_ppa_flush_path(void)
{
    const ppa_client_config_t ppa_client_config = {
        .oper_type = PPA_OPERATION_SRM,
        .max_pending_trans_num = 1,
        .data_burst_length = PPA_DATA_BURST_LENGTH_128,
    };
    esp_err_t ret = ppa_register_client(&ppa_client_config, &s_ppa_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register PPA client: %s", esp_err_to_name(ret));
        return ret;
    }

    const ppa_event_callbacks_t ppa_callbacks = {
        .on_trans_done = ppa_srm_trans_done_callback,
    };
    ret = ppa_client_register_event_callbacks(s_ppa_client, &ppa_callbacks);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register PPA callback: %s", esp_err_to_name(ret));
        ppa_unregister_client(s_ppa_client);
        s_ppa_client = NULL;
        return ret;
    }

    s_ppa_hook_context.client = s_ppa_client;
    s_ppa_hook_context.panel = s_lcd_panel;
    const esp_lcd_panel_hooks_t panel_hooks = {
        .draw_bitmap_hook = ppa_draw_bitmap_hook,
    };
    ret = esp_lcd_dpi_panel_register_hooks(s_lcd_panel, &panel_hooks, &s_ppa_hook_context);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register DPI PPA hook: %s", esp_err_to_name(ret));
        ppa_unregister_client(s_ppa_client);
        s_ppa_client = NULL;
        memset(&s_ppa_hook_context, 0, sizeof(s_ppa_hook_context));
        return ret;
    }

    const esp_lcd_dpi_panel_event_callbacks_t panel_callbacks = {
        .on_color_trans_done = lvgl_flush_ready_callback,
    };
    ret = esp_lcd_dpi_panel_register_event_callbacks(s_lcd_panel, &panel_callbacks, &s_lvgl_disp_drv);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register LVGL flush callback: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Enabled asynchronous PPA %s-to-RGB888 partial flush path",
             LV_COLOR_DEPTH == 16 ? "RGB565" : "ARGB8888");
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

    void *frame_buffer = NULL;
    ret = esp_lcd_dpi_panel_get_frame_buffer(s_lcd_panel, 1, &frame_buffer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get DPI frame buffer: %s", esp_err_to_name(ret));
        return ret;
    }
    s_hdmi_frame_buffer = (uint8_t *)frame_buffer;

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

    ESP_LOGI(TAG, "Submitting boot diagnostic frame before LVGL demo");
    ret = submit_boot_diagnostic_frame();
    if (ret != ESP_OK) {
        return ret;
    }

    lv_disp_draw_buf_init(&s_lvgl_draw_buf,
                          s_lvgl_draw_buffers[0],
                          s_lvgl_draw_buffers[1],
                          LVGL_DRAW_BUFFER_PIXELS);

    lv_disp_drv_init(&s_lvgl_disp_drv);
    s_lvgl_disp_drv.hor_res = DISPLAY_OUTPUT_H_RES;
    s_lvgl_disp_drv.ver_res = DISPLAY_OUTPUT_V_RES;
    s_lvgl_disp_drv.flush_cb = lvgl_flush_cb;
    s_lvgl_disp_drv.draw_buf = &s_lvgl_draw_buf;
    if (lv_disp_drv_register(&s_lvgl_disp_drv) == NULL) {
        ESP_LOGE(TAG, "Failed to register LVGL display driver");
        return ESP_FAIL;
    }

    ret = init_ppa_flush_path();
    if (ret != ESP_OK) {
        return ret;
    }

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
