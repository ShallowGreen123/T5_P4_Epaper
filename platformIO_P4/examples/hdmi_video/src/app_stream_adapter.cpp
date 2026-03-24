#include "app_stream_adapter.h"

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_extractor.h"

struct app_stream_adapter_t {
    app_stream_frame_cb_t frame_cb = nullptr;
    app_stream_event_cb_t event_cb = nullptr;
    void *user_data = nullptr;

    void **decode_buffers = nullptr;
    uint32_t buffer_count = 0;
    uint32_t buffer_size = 0;
    esp_codec_dev_handle_t audio_dev = nullptr;
    app_stream_jpeg_config_t jpeg_config = APP_STREAM_JPEG_CONFIG_DEFAULT_RGB888();
    uint32_t target_width = 0;
    uint32_t target_height = 0;

    app_extractor_handle_t extractor_handle = nullptr;
    char *filename = nullptr;
    bool extract_audio = false;
    bool running = false;
    bool stop_requested = false;
    bool has_info = false;

    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fps = 0;
    uint32_t duration = 0;

    uint32_t frame_count = 0;
    float current_fps = 0.0f;
    uint32_t fps_window_frames = 0;
    int64_t fps_window_start_us = 0;

    TaskHandle_t extract_task_handle = nullptr;
};

namespace {

static const char *TAG = "stream_adapter";

constexpr uint32_t kExtractTaskStackSize = 6 * 1024;
constexpr UBaseType_t kExtractTaskPriority = 5;
constexpr uint32_t kStopWaitTimeoutMs = 2000;

app_stream_adapter_t *g_adapter_instance = nullptr;

char *dup_string(const char *value)
{
    if (value == nullptr) {
        return nullptr;
    }
    const size_t len = strlen(value) + 1;
    char *copy = static_cast<char *>(malloc(len));
    if (copy != nullptr) {
        memcpy(copy, value, len);
    }
    return copy;
}

void reset_stats(app_stream_adapter_t *adapter)
{
    adapter->frame_count = 0;
    adapter->current_fps = 0.0f;
    adapter->fps_window_frames = 0;
    adapter->fps_window_start_us = 0;
}

void notify_event(app_stream_adapter_t *adapter, app_stream_event_t event, esp_err_t error)
{
    if (adapter->event_cb != nullptr) {
        adapter->event_cb(event, error, adapter->user_data);
    }
}

esp_err_t extractor_frame_callback(uint8_t *buffer,
                                   uint32_t buffer_size,
                                   bool is_video,
                                   uint32_t pts)
{
    (void)pts;

    app_stream_adapter_t *adapter = g_adapter_instance;
    if (adapter == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!is_video) {
        return ESP_OK;
    }

    adapter->frame_count++;
    adapter->fps_window_frames++;

    const int64_t now_us = esp_timer_get_time();
    if (adapter->fps_window_start_us == 0) {
        adapter->fps_window_start_us = now_us;
    } else {
        const int64_t elapsed_us = now_us - adapter->fps_window_start_us;
        if (elapsed_us >= 1000000) {
            adapter->current_fps = (1000000.0f * static_cast<float>(adapter->fps_window_frames)) /
                                   static_cast<float>(elapsed_us);
            adapter->fps_window_frames = 0;
            adapter->fps_window_start_us = now_us;
        }
    }

    if (adapter->frame_cb == nullptr) {
        return ESP_OK;
    }

    return adapter->frame_cb(buffer,
                             buffer_size,
                             adapter->width,
                             adapter->height,
                             adapter->frame_count - 1,
                             adapter->user_data);
}

void extract_task(void *arg)
{
    app_stream_adapter_t *adapter = static_cast<app_stream_adapter_t *>(arg);
    ESP_LOGI(TAG, "extract task started");

    while (!adapter->stop_requested && adapter->running) {
        const esp_err_t err = app_extractor_read_frame(adapter->extractor_handle);
        if (err == ESP_OK) {
            continue;
        }

        adapter->running = false;
        if (err == ESP_ERR_NOT_FOUND) {
            ESP_LOGI(TAG, "end of stream");
            notify_event(adapter, APP_STREAM_EVENT_EOS, ESP_OK);
        } else {
            ESP_LOGE(TAG, "extract loop failed: %s", esp_err_to_name(err));
            notify_event(adapter, APP_STREAM_EVENT_ERROR, err);
        }
        break;
    }

    adapter->extract_task_handle = nullptr;
    ESP_LOGI(TAG, "extract task stopped");
    vTaskDelete(nullptr);
}

} // namespace

esp_err_t app_stream_adapter_init(const app_stream_adapter_config_t *config,
                                  app_stream_adapter_handle_t *ret_adapter)
{
    if (config == nullptr || ret_adapter == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    app_stream_adapter_t *adapter = new app_stream_adapter_t();
    adapter->frame_cb = config->frame_cb;
    adapter->event_cb = config->event_cb;
    adapter->user_data = config->user_data;
    adapter->decode_buffers = config->decode_buffers;
    adapter->buffer_count = config->buffer_count;
    adapter->buffer_size = config->buffer_size;
    adapter->audio_dev = config->audio_dev;
    adapter->jpeg_config = config->jpeg_config;
    adapter->target_width = config->target_width;
    adapter->target_height = config->target_height;

    esp_err_t err = app_extractor_init(extractor_frame_callback,
                                       config->audio_dev,
                                       &adapter->extractor_handle);
    if (err != ESP_OK) {
        delete adapter;
        return err;
    }

    *ret_adapter = adapter;
    return ESP_OK;
}

esp_err_t app_stream_adapter_set_file(app_stream_adapter_handle_t handle,
                                      const char *filename,
                                      bool extract_audio)
{
    if (handle == nullptr || filename == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    app_stream_adapter_t *adapter = handle;
    if (adapter->running) {
        return ESP_ERR_INVALID_STATE;
    }

    free(adapter->filename);
    adapter->filename = dup_string(filename);
    if (adapter->filename == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    adapter->extract_audio = extract_audio && (adapter->audio_dev != nullptr);
    adapter->has_info = false;
    adapter->width = 0;
    adapter->height = 0;
    adapter->fps = 0;
    adapter->duration = 0;

    return ESP_OK;
}

esp_err_t app_stream_adapter_start(app_stream_adapter_handle_t handle)
{
    if (handle == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    app_stream_adapter_t *adapter = handle;
    if (adapter->filename == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    if (adapter->running) {
        return ESP_OK;
    }
    if (adapter->extract_task_handle != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = app_extractor_start(adapter->extractor_handle,
                                        adapter->filename,
                                        true,
                                        adapter->extract_audio);
    if (err != ESP_OK) {
        return err;
    }

    err = app_extractor_get_video_info(adapter->extractor_handle,
                                       &adapter->width,
                                       &adapter->height,
                                       &adapter->fps,
                                       &adapter->duration);
    if (err != ESP_OK) {
        app_extractor_stop(adapter->extractor_handle);
        return err;
    }

    adapter->has_info = true;
    adapter->stop_requested = false;
    adapter->running = true;
    reset_stats(adapter);
    g_adapter_instance = adapter;

    const BaseType_t task_ok = xTaskCreate(extract_task,
                                           "hdmi_extract",
                                           kExtractTaskStackSize,
                                           adapter,
                                           kExtractTaskPriority,
                                           &adapter->extract_task_handle);
    if (task_ok != pdPASS) {
        adapter->running = false;
        app_extractor_stop(adapter->extractor_handle);
        g_adapter_instance = nullptr;
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t app_stream_adapter_stop(app_stream_adapter_handle_t handle)
{
    if (handle == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    app_stream_adapter_t *adapter = handle;
    adapter->stop_requested = true;
    adapter->running = false;

    const uint32_t wait_start = millis();
    while (adapter->extract_task_handle != nullptr &&
           (millis() - wait_start) < kStopWaitTimeoutMs) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (adapter->extract_task_handle != nullptr) {
        ESP_LOGW(TAG, "extract task stop timed out");
    }

    g_adapter_instance = nullptr;
    return app_extractor_stop(adapter->extractor_handle);
}

esp_err_t app_stream_adapter_seek(app_stream_adapter_handle_t handle, uint32_t position)
{
    if (handle == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    app_stream_adapter_t *adapter = handle;
    return app_extractor_seek(adapter->extractor_handle, position);
}

esp_err_t app_stream_adapter_get_info(app_stream_adapter_handle_t handle,
                                      uint32_t *width,
                                      uint32_t *height,
                                      uint32_t *fps,
                                      uint32_t *duration)
{
    if (handle == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    app_stream_adapter_t *adapter = handle;
    if (!adapter->has_info) {
        return ESP_ERR_INVALID_STATE;
    }

    if (width) {
        *width = adapter->width;
    }
    if (height) {
        *height = adapter->height;
    }
    if (fps) {
        *fps = adapter->fps;
    }
    if (duration) {
        *duration = adapter->duration;
    }

    return ESP_OK;
}

esp_err_t app_stream_adapter_get_stats(app_stream_adapter_handle_t handle,
                                       app_stream_stats_t *stats)
{
    if (handle == nullptr || stats == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    app_stream_adapter_t *adapter = handle;
    stats->current_fps = adapter->current_fps;
    stats->frames_processed = adapter->frame_count;
    return ESP_OK;
}

esp_err_t app_stream_adapter_resize_buffers(app_stream_adapter_handle_t handle,
                                            void **decode_buffers,
                                            uint32_t buffer_count,
                                            uint32_t buffer_size)
{
    if (handle == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    app_stream_adapter_t *adapter = handle;
    adapter->decode_buffers = decode_buffers;
    adapter->buffer_count = buffer_count;
    adapter->buffer_size = buffer_size;
    return ESP_OK;
}

esp_err_t app_stream_adapter_probe_video_info(const char *filename,
                                              uint32_t *width,
                                              uint32_t *height,
                                              uint32_t *fps,
                                              uint32_t *duration)
{
    return app_extractor_probe_video_info(filename, width, height, fps, duration);
}

esp_err_t app_stream_adapter_deinit(app_stream_adapter_handle_t handle)
{
    if (handle == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    app_stream_adapter_t *adapter = handle;
    app_stream_adapter_stop(handle);
    app_extractor_deinit(adapter->extractor_handle);
    free(adapter->filename);
    delete adapter;
    return ESP_OK;
}
