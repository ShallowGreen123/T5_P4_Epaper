#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "t5_p4_bsp.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct app_stream_adapter_t *app_stream_adapter_handle_t;

typedef enum {
    APP_STREAM_JPEG_OUTPUT_RGB565,
    APP_STREAM_JPEG_OUTPUT_RGB888,
} app_stream_jpeg_output_format_t;

typedef struct {
    app_stream_jpeg_output_format_t output_format;
    bool bgr_order;
} app_stream_jpeg_config_t;

#define APP_STREAM_JPEG_CONFIG_DEFAULT_RGB565() \
    { .output_format = APP_STREAM_JPEG_OUTPUT_RGB565, .bgr_order = true }

#define APP_STREAM_JPEG_CONFIG_DEFAULT_RGB888() \
    { .output_format = APP_STREAM_JPEG_OUTPUT_RGB888, .bgr_order = true }

typedef struct {
    float current_fps;
    uint32_t frames_processed;
} app_stream_stats_t;

// Phase 3 note:
// The callback currently receives extracted MJPEG frame payloads, not decoded RGB frames.
typedef esp_err_t (*app_stream_frame_cb_t)(uint8_t *buffer,
                                           uint32_t buffer_size,
                                           uint32_t width,
                                           uint32_t height,
                                           uint32_t frame_index,
                                           void *user_data);

typedef enum {
    APP_STREAM_EVENT_EOS = 0,
    APP_STREAM_EVENT_ERROR,
} app_stream_event_t;

typedef void (*app_stream_event_cb_t)(app_stream_event_t event,
                                      esp_err_t error,
                                      void *user_data);

typedef struct {
    app_stream_frame_cb_t frame_cb;
    app_stream_event_cb_t event_cb;
    void *user_data;
    void **decode_buffers;
    uint32_t buffer_count;
    uint32_t buffer_size;
    esp_codec_dev_handle_t audio_dev;
    app_stream_jpeg_config_t jpeg_config;
    uint32_t target_width;
    uint32_t target_height;
} app_stream_adapter_config_t;

esp_err_t app_stream_adapter_init(const app_stream_adapter_config_t *config,
                                  app_stream_adapter_handle_t *ret_adapter);

esp_err_t app_stream_adapter_set_file(app_stream_adapter_handle_t handle,
                                      const char *filename,
                                      bool extract_audio);

esp_err_t app_stream_adapter_start(app_stream_adapter_handle_t handle);
esp_err_t app_stream_adapter_stop(app_stream_adapter_handle_t handle);
esp_err_t app_stream_adapter_seek(app_stream_adapter_handle_t handle, uint32_t position);

esp_err_t app_stream_adapter_get_info(app_stream_adapter_handle_t handle,
                                      uint32_t *width,
                                      uint32_t *height,
                                      uint32_t *fps,
                                      uint32_t *duration);

esp_err_t app_stream_adapter_get_stats(app_stream_adapter_handle_t handle,
                                       app_stream_stats_t *stats);

esp_err_t app_stream_adapter_resize_buffers(app_stream_adapter_handle_t handle,
                                            void **decode_buffers,
                                            uint32_t buffer_count,
                                            uint32_t buffer_size);

esp_err_t app_stream_adapter_probe_video_info(const char *filename,
                                              uint32_t *width,
                                              uint32_t *height,
                                              uint32_t *fps,
                                              uint32_t *duration);

esp_err_t app_stream_adapter_deinit(app_stream_adapter_handle_t handle);

#ifdef __cplusplus
}
#endif
