/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <inttypes.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_private/esp_cache_private.h"
#include "esp_codec_dev.h"
#include "driver/gpio.h"
#include "bsp/esp-bsp.h"
#include "app_stream_adapter.h"
#include "sdkconfig.h"

static const char *TAG = "main";

#ifdef CONFIG_BSP_LCD_TYPE_HDMI
#define DISPLAY_OUTPUT_H_RES 800
#define DISPLAY_OUTPUT_V_RES 600
// #define DISPLAY_OUTPUT_H_RES 1280
// #define DISPLAY_OUTPUT_V_RES 720
#define DISPLAY_LANE_BITRATE_MBPS BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS
#else
#define DISPLAY_OUTPUT_H_RES BSP_LCD_H_RES
#define DISPLAY_OUTPUT_V_RES BSP_LCD_V_RES
#define DISPLAY_LANE_BITRATE_MBPS BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS
#endif

#ifdef CONFIG_BSP_LCD_COLOR_FORMAT_RGB888
#define DISPLAY_BUFFER_SIZE (DISPLAY_OUTPUT_H_RES * DISPLAY_OUTPUT_V_RES * 3)
#define BYTES_PER_PIXEL 3
#else
#define DISPLAY_BUFFER_SIZE (DISPLAY_OUTPUT_H_RES * DISPLAY_OUTPUT_V_RES * 2)
#define BYTES_PER_PIXEL 2
#endif

#define MP4_FILENAME   BSP_SD_MOUNT_POINT "/" CONFIG_MP4_FILENAME
#define ENABLE_LOOP_PLAYBACK    1
#define ENABLE_AUDIO_PLAYBACK   0

static esp_lcd_panel_handle_t lcd_panel;
static esp_lcd_panel_io_handle_t lcd_io;
static void *lcd_buffer[CONFIG_BSP_LCD_DPI_BUFFER_NUMS];
static SemaphoreHandle_t trans_sem = NULL;
static app_stream_adapter_handle_t stream_adapter;
static esp_codec_dev_handle_t g_audio_dev = NULL;  // Global audio device handle

static void play_media_file(const char *filename);

static IRAM_ATTR bool flush_dpi_panel_ready_callback(esp_lcd_panel_handle_t panel_io, esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx)
{
    BaseType_t taskAwake = pdFALSE;

    if (trans_sem) {
        xSemaphoreGiveFromISR(trans_sem, &taskAwake);
    }

    return false;
}

static esp_err_t display_decoded_frame(uint8_t *buffer, uint32_t buffer_size,
                                       uint32_t width, uint32_t height,
                                       uint32_t buffer_index, void *user_data)
{
    // user_data can be used to pass context information such as LCD handle or display state
    esp_lcd_panel_draw_bitmap(lcd_panel, 0, 0, width, height, buffer);
    xSemaphoreTake(trans_sem, 0);
    xSemaphoreTake(trans_sem, portMAX_DELAY);

    return ESP_OK;
}

void app_main()
{
    ESP_LOGI(TAG, "Starting HDMI MP4 Player application");

    // Initialize display
    bsp_display_config_t display_config = {
        .hdmi_resolution = BSP_HDMI_RES_800x600,
        // .hdmi_resolution = BSP_HDMI_RES_1280x720,
        .dsi_bus = {
            .phy_clk_src = 0,
            .lane_bit_rate_mbps = DISPLAY_LANE_BITRATE_MBPS,
        }
    };

    ESP_LOGI(TAG, "Display timing: %dx%d, DSI lane bitrate: %d Mbps",
             DISPLAY_OUTPUT_H_RES, DISPLAY_OUTPUT_V_RES, DISPLAY_LANE_BITRATE_MBPS);

    esp_err_t ret = bsp_display_new(&display_config, &lcd_panel, &lcd_io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize display: %s", esp_err_to_name(ret));
        return;
    }

    trans_sem = xSemaphoreCreateBinary();
    esp_lcd_dpi_panel_event_callbacks_t callbacks = {
        .on_color_trans_done = flush_dpi_panel_ready_callback,
    };
    esp_lcd_dpi_panel_register_event_callbacks(lcd_panel, &callbacks, NULL);

    size_t data_cache_line_size = 0;
    ESP_ERROR_CHECK(esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &data_cache_line_size));
    for (int i = 0; i < CONFIG_BSP_LCD_DPI_BUFFER_NUMS; i++) {
        lcd_buffer[i] = heap_caps_aligned_calloc(data_cache_line_size, 1, DISPLAY_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (lcd_buffer[i] == NULL) {
            ESP_LOGE(TAG, "Failed to allocate buffer %d", i);
            return;
        }
    }
    ESP_LOGI(TAG, "Allocated %d external decode buffers (%u bytes each)",
             CONFIG_BSP_LCD_DPI_BUFFER_NUMS, (unsigned)DISPLAY_BUFFER_SIZE);

    ret = bsp_sdcard_mount();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card");
        return;
    }

    esp_codec_dev_handle_t audio_dev = NULL;
#if ENABLE_AUDIO_PLAYBACK
    ESP_LOGI(TAG, "Initializing audio codec...");
    audio_dev = bsp_audio_codec_speaker_init();
    if (audio_dev == NULL) {
        ESP_LOGW(TAG, "Failed to initialize audio codec, continuing without audio");
        g_audio_dev = NULL;
    } else {
        ESP_LOGI(TAG, "Audio codec initialized successfully");
        esp_codec_dev_set_out_vol(audio_dev, 60);
        g_audio_dev = audio_dev;
    }
#else
    ESP_LOGI(TAG, "Audio playback disabled, starting in video-only mode");
    g_audio_dev = NULL;
#endif

    // Initialize stream adapter with unified configuration
    ESP_LOGI(TAG, "Initializing stream adapter...");

    // User data can be any context information needed by the callback
    void *custom_user_data = NULL;  // Can be any struct pointer or data

    app_stream_adapter_config_t adapter_config = {
        .frame_cb = display_decoded_frame,
        .user_data = custom_user_data,
        .decode_buffers = lcd_buffer,
        .buffer_count = CONFIG_BSP_LCD_DPI_BUFFER_NUMS,
        .buffer_size = DISPLAY_BUFFER_SIZE,
        .audio_dev = audio_dev,
#ifdef CONFIG_BSP_LCD_COLOR_FORMAT_RGB888
        .jpeg_config = {
            .output_format = APP_STREAM_JPEG_OUTPUT_RGB888,
            .bgr_order = true,
        },
#else
        .jpeg_config = APP_STREAM_JPEG_CONFIG_DEFAULT_RGB565(),
#endif
        .target_width = DISPLAY_OUTPUT_H_RES,
        .target_height = DISPLAY_OUTPUT_V_RES,
    };

    ret = app_stream_adapter_init(&adapter_config, &stream_adapter);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize stream adapter: %d", ret);
        if (audio_dev) {
            esp_codec_dev_close(audio_dev);
        }
        return;
    }

    const char *format_str = (adapter_config.jpeg_config.output_format == APP_STREAM_JPEG_OUTPUT_RGB888) ? "RGB888" : "RGB565";
    ESP_LOGI(TAG, "Stream adapter initialized with %s/%s at %dx%d%s",
             format_str,
             adapter_config.jpeg_config.bgr_order ? "BGR" : "RGB",
             DISPLAY_OUTPUT_H_RES, DISPLAY_OUTPUT_V_RES,
             audio_dev ? " and audio support" : " (video-only)");

    FILE *fp = fopen(MP4_FILENAME, "rb");
    if (fp) {
        fclose(fp);

        app_stream_adapter_set_file(stream_adapter, MP4_FILENAME, false);
        play_media_file(MP4_FILENAME);
    } else {
        ESP_LOGW(TAG, "MP4 file not found: %s", MP4_FILENAME);
    }
}

static void play_media_file(const char *filename)
{
#if ENABLE_LOOP_PLAYBACK
    ESP_LOGI(TAG, "Starting loop playback of %s", filename);
#else
    ESP_LOGI(TAG, "Playing %s", filename);
#endif

    // Get stream info once
    uint32_t width, height, fps, duration;
    esp_err_t ret = app_stream_adapter_get_info(stream_adapter, &width, &height, &fps, &duration);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Media info: %"PRIu32"x%"PRIu32", %"PRIu32" fps, duration: %"PRIu32" ms",
                 width, height, fps, duration);
    }

#if ENABLE_LOOP_PLAYBACK
    uint32_t loop_count = 0;

    // Continuous playback loop
    while (1) {
        ESP_LOGI(TAG, "Starting playback loop #%" PRIu32, ++loop_count);

        // Start playback
        ret = app_stream_adapter_start(stream_adapter);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start playback: %d", ret);
            break;
        }

        // Monitor playback with improved detection
        uint32_t stable_count = 0;
        uint32_t last_frames = 0;

        while (1) {
            app_stream_stats_t stats;
            ret = app_stream_adapter_get_stats(stream_adapter, &stats);

            if (ret == ESP_OK) {
                if (stats.frames_processed == last_frames) {
                    stable_count++;
                    // If frame count hasn't changed for 3 consecutive checks, assume end
                    if (stable_count >= 3) {
                        ESP_LOGI(TAG, "Playback loop #%" PRIu32 " finished (%" PRIu32 " frames), restarting...",
                                 loop_count, stats.frames_processed);
                        break;
                    }
                } else {
                    stable_count = 0;
                    last_frames = stats.frames_processed;
                }
            } else {
                ESP_LOGW(TAG, "Error getting stats, assuming playback ended");
                break;
            }

            vTaskDelay(pdMS_TO_TICKS(500)); // Check every 500ms
        }

        // Stop and reset for next loop
        app_stream_adapter_stop(stream_adapter);
        vTaskDelay(pdMS_TO_TICKS(200)); // Brief pause
        app_stream_adapter_set_file(stream_adapter, filename, false);
    }
#else
    // Single playback mode
    ret = app_stream_adapter_start(stream_adapter);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start playback: %d", ret);
    }
#endif
}
