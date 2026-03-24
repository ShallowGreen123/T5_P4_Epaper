#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "t5_p4_bsp.h"

extern "C" {
#include "esp_extractor_types.h"
}

typedef struct app_extractor_t *app_extractor_handle_t;

typedef esp_err_t (*app_extractor_frame_cb_t)(uint8_t *buffer, uint32_t buffer_size,
                                              bool is_video, uint32_t pts);

esp_err_t app_extractor_init(app_extractor_frame_cb_t frame_cb,
                             esp_codec_dev_handle_t audio_dev,
                             app_extractor_handle_t *ret_extractor);

esp_err_t app_extractor_start(app_extractor_handle_t extractor,
                              const char *filename,
                              bool extract_video,
                              bool extract_audio);

esp_err_t app_extractor_read_frame(app_extractor_handle_t extractor);

esp_err_t app_extractor_get_video_info(app_extractor_handle_t extractor,
                                       uint32_t *width,
                                       uint32_t *height,
                                       uint32_t *fps,
                                       uint32_t *duration);

esp_err_t app_extractor_get_audio_info(app_extractor_handle_t extractor,
                                       uint32_t *sample_rate,
                                       uint8_t *channels,
                                       uint8_t *bits,
                                       uint32_t *duration);

esp_err_t app_extractor_probe_video_info(const char *filename,
                                         uint32_t *width,
                                         uint32_t *height,
                                         uint32_t *fps,
                                         uint32_t *duration);

esp_err_t app_extractor_seek(app_extractor_handle_t extractor, uint32_t position);
esp_err_t app_extractor_stop(app_extractor_handle_t extractor);
esp_err_t app_extractor_deinit(app_extractor_handle_t extractor);
