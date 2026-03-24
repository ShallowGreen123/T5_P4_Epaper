#include "app_extractor.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "esp_log.h"

extern "C" {
#include "esp_avi_extractor.h"
#include "esp_extractor.h"
#include "esp_mp4_extractor.h"
#include "mem_pool.h"
}

#ifndef O_BINARY
#define O_BINARY 0
#endif

struct app_extractor_t {
    esp_extractor_handle_t extractor = nullptr;
    app_extractor_frame_cb_t frame_cb = nullptr;
    esp_codec_dev_handle_t audio_dev = nullptr;

    bool extract_video = false;
    bool extract_audio = false;
    bool has_video = false;
    bool has_audio = false;
    bool eos_reached = false;

    extractor_video_format_t video_format = EXTRACTOR_VIDEO_FORMAT_NONE;
    extractor_audio_format_t audio_format = EXTRACTOR_AUDIO_FORMAT_NONE;

    uint32_t video_width = 0;
    uint32_t video_height = 0;
    uint32_t video_fps = 0;
    uint32_t video_duration = 0;

    uint32_t audio_sample_rate = 0;
    uint8_t audio_channels = 0;
    uint8_t audio_bits = 0;
    uint32_t audio_duration = 0;
};

namespace {

static const char *TAG = "app_extractor";

constexpr uint32_t kOutputPoolSize = 256 * 1024;
constexpr uint16_t kCacheBlockCount = 3;
constexpr uint32_t kCacheBlockSize = 96 * 1024;

bool g_extractors_registered = false;

esp_err_t extr_to_esp_err(esp_extr_err_t err)
{
    switch (err) {
    case ESP_EXTR_ERR_OK:
        return ESP_OK;
    case ESP_EXTR_ERR_ALREADY_EXIST:
        return ESP_OK;
    case ESP_EXTR_ERR_INV_ARG:
        return ESP_ERR_INVALID_ARG;
    case ESP_EXTR_ERR_NOT_FOUND:
    case ESP_EXTR_ERR_EOS:
        return ESP_ERR_NOT_FOUND;
    case ESP_EXTR_ERR_NOT_SUPPORT:
        return ESP_ERR_NOT_SUPPORTED;
    case ESP_EXTR_ERR_NO_MEM:
        return ESP_ERR_NO_MEM;
    default:
        return ESP_FAIL;
    }
}

esp_err_t register_supported_extractors()
{
    if (g_extractors_registered) {
        return ESP_OK;
    }

    esp_mp4_extractor_use_dynamic_parser(true);

    esp_err_t err = extr_to_esp_err(esp_mp4_extractor_register());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register MP4 extractor failed: %s", esp_err_to_name(err));
        return err;
    }

    err = extr_to_esp_err(esp_avi_extractor_register());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register AVI extractor failed: %s", esp_err_to_name(err));
        return err;
    }

    g_extractors_registered = true;
    return ESP_OK;
}

void clear_stream_info(app_extractor_t *extractor)
{
    extractor->has_video = false;
    extractor->has_audio = false;
    extractor->eos_reached = false;
    extractor->video_format = EXTRACTOR_VIDEO_FORMAT_NONE;
    extractor->audio_format = EXTRACTOR_AUDIO_FORMAT_NONE;
    extractor->video_width = 0;
    extractor->video_height = 0;
    extractor->video_fps = 0;
    extractor->video_duration = 0;
    extractor->audio_sample_rate = 0;
    extractor->audio_channels = 0;
    extractor->audio_bits = 0;
    extractor->audio_duration = 0;
}

void *extractor_file_open(char *url, void *ctx)
{
    (void)ctx;
    const int fd = open(url, O_RDONLY | O_BINARY);
    if (fd < 0) {
        ESP_LOGE(TAG, "open failed for %s: errno=%d", url, errno);
        return nullptr;
    }
    return reinterpret_cast<void *>(static_cast<intptr_t>(fd));
}

int extractor_file_read(void *buffer, uint32_t size, void *ctx)
{
    const int fd = static_cast<int>(reinterpret_cast<intptr_t>(ctx));
    const ssize_t read_size = read(fd, buffer, size);
    if (read_size < 0) {
        ESP_LOGE(TAG, "read failed: errno=%d", errno);
        return 0;
    }
    return static_cast<int>(read_size);
}

int extractor_file_seek(uint32_t position, void *ctx)
{
    const int fd = static_cast<int>(reinterpret_cast<intptr_t>(ctx));
    const off_t seek_ret = lseek(fd, static_cast<off_t>(position), SEEK_SET);
    return (seek_ret < 0) ? -1 : 0;
}

uint32_t extractor_file_size(void *ctx)
{
    const int fd = static_cast<int>(reinterpret_cast<intptr_t>(ctx));
    const off_t current = lseek(fd, 0, SEEK_CUR);
    const off_t end = lseek(fd, 0, SEEK_END);
    (void)lseek(fd, current, SEEK_SET);
    if (end <= 0) {
        return 0;
    }
    return static_cast<uint32_t>(end);
}

int extractor_file_close(void *ctx)
{
    const int fd = static_cast<int>(reinterpret_cast<intptr_t>(ctx));
    return close(fd);
}

esp_err_t load_stream_info(app_extractor_t *extractor)
{
    uint16_t video_streams = 0;
    uint16_t audio_streams = 0;

    esp_err_t err = extr_to_esp_err(
        esp_extractor_get_stream_num(extractor->extractor, EXTRACTOR_STREAM_TYPE_VIDEO, &video_streams));
    if (err != ESP_OK || video_streams == 0) {
        ESP_LOGE(TAG, "no video stream found");
        return (err == ESP_OK) ? ESP_ERR_NOT_FOUND : err;
    }

    extractor_stream_info_t stream_info = {};
    err = extr_to_esp_err(
        esp_extractor_get_stream_info(extractor->extractor, EXTRACTOR_STREAM_TYPE_VIDEO, 0, &stream_info));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "get video stream info failed: %s", esp_err_to_name(err));
        return err;
    }

    extractor->has_video = true;
    extractor->video_format = stream_info.stream_info.video_info.format;
    extractor->video_width = stream_info.stream_info.video_info.width;
    extractor->video_height = stream_info.stream_info.video_info.height;
    extractor->video_fps = stream_info.stream_info.video_info.fps;
    extractor->video_duration = stream_info.duration;

    if (extractor->video_format != EXTRACTOR_VIDEO_FORMAT_MJPEG) {
        ESP_LOGE(TAG, "unsupported video format=%d, only MJPEG is allowed", static_cast<int>(extractor->video_format));
        return ESP_ERR_NOT_SUPPORTED;
    }

    err = extr_to_esp_err(
        esp_extractor_get_stream_num(extractor->extractor, EXTRACTOR_STREAM_TYPE_AUDIO, &audio_streams));
    if (err == ESP_OK && audio_streams > 0) {
        memset(&stream_info, 0, sizeof(stream_info));
        err = extr_to_esp_err(
            esp_extractor_get_stream_info(extractor->extractor, EXTRACTOR_STREAM_TYPE_AUDIO, 0, &stream_info));
        if (err == ESP_OK) {
            extractor->has_audio = true;
            extractor->audio_format = stream_info.stream_info.audio_info.format;
            extractor->audio_sample_rate = stream_info.stream_info.audio_info.sample_rate;
            extractor->audio_channels = stream_info.stream_info.audio_info.channel;
            extractor->audio_bits = stream_info.stream_info.audio_info.bits_per_sample;
            extractor->audio_duration = stream_info.duration;
        }
    }

    ESP_LOGI(TAG, "video info: %" PRIu32 "x%" PRIu32 ", %" PRIu32 " fps, duration=%" PRIu32 " ms",
             extractor->video_width, extractor->video_height, extractor->video_fps, extractor->video_duration);
    if (extractor->has_audio) {
        ESP_LOGI(TAG, "audio info: format=%d, %" PRIu32 " Hz, %u ch, %u bits",
                 static_cast<int>(extractor->audio_format), extractor->audio_sample_rate,
                 extractor->audio_channels, extractor->audio_bits);
    }

    return ESP_OK;
}

void release_frame_buffer(app_extractor_t *extractor, extractor_frame_info_t *frame)
{
    if (frame->frame_buffer == nullptr) {
        return;
    }

    mem_pool_handle_t pool = esp_extractor_get_output_pool(extractor->extractor);
    if (pool != nullptr) {
        mem_pool_free(pool, frame->frame_buffer);
    }
    frame->frame_buffer = nullptr;
}

} // namespace

esp_err_t app_extractor_init(app_extractor_frame_cb_t frame_cb,
                             esp_codec_dev_handle_t audio_dev,
                             app_extractor_handle_t *ret_extractor)
{
    if (ret_extractor == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = register_supported_extractors();
    if (err != ESP_OK) {
        return err;
    }

    app_extractor_t *extractor = new app_extractor_t();
    extractor->frame_cb = frame_cb;
    extractor->audio_dev = audio_dev;
    clear_stream_info(extractor);

    *ret_extractor = extractor;
    return ESP_OK;
}

esp_err_t app_extractor_start(app_extractor_handle_t handle,
                              const char *filename,
                              bool extract_video,
                              bool extract_audio)
{
    if (handle == nullptr || filename == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    app_extractor_t *extractor = handle;
    app_extractor_stop(handle);
    clear_stream_info(extractor);

    extractor->extract_video = extract_video;
    extractor->extract_audio = extract_audio;

    uint8_t extract_mask = 0;
    if (extract_video) {
        extract_mask |= ESP_EXTRACT_MASK_VIDEO;
    }
    if (extract_audio) {
        extract_mask |= ESP_EXTRACT_MASK_AUDIO;
    }

    esp_extractor_config_t config = {};
    config.open = extractor_file_open;
    config.read = extractor_file_read;
    config.seek = extractor_file_seek;
    config.file_size = extractor_file_size;
    config.close = extractor_file_close;
    config.input_ctx = extractor;
    config.url = const_cast<char *>(filename);
    config.extract_mask = extract_mask;
    config.cache_block_num = kCacheBlockCount;
    config.cache_block_size = kCacheBlockSize;
    config.output_pool_size = kOutputPoolSize;
    config.output_align = 64;

    esp_err_t err = extr_to_esp_err(esp_extractor_open(&config, &extractor->extractor));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_extractor_open failed: %s", esp_err_to_name(err));
        return err;
    }

    err = extr_to_esp_err(esp_extractor_parse_stream_info(extractor->extractor));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "parse stream info failed: %s", esp_err_to_name(err));
        app_extractor_stop(handle);
        return err;
    }

    err = load_stream_info(extractor);
    if (err != ESP_OK) {
        app_extractor_stop(handle);
        return err;
    }

    return ESP_OK;
}

esp_err_t app_extractor_read_frame(app_extractor_handle_t handle)
{
    if (handle == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    app_extractor_t *extractor = handle;
    if (extractor->extractor == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    if (extractor->eos_reached) {
        return ESP_ERR_NOT_FOUND;
    }

    extractor_frame_info_t frame = {};
    esp_err_t err = extr_to_esp_err(esp_extractor_read_frame(extractor->extractor, &frame));
    if (err != ESP_OK) {
        if (err == ESP_ERR_NOT_FOUND) {
            extractor->eos_reached = true;
        }
        return err;
    }

    if (frame.eos) {
        extractor->eos_reached = true;
        release_frame_buffer(extractor, &frame);
        return ESP_ERR_NOT_FOUND;
    }

    if (frame.frame_buffer != nullptr && extractor->frame_cb != nullptr) {
        const bool is_video = (frame.stream_type == EXTRACTOR_STREAM_TYPE_VIDEO);
        const bool should_dispatch = (is_video && extractor->extract_video) || (!is_video && extractor->extract_audio);
        if (should_dispatch) {
            err = extractor->frame_cb(frame.frame_buffer, frame.frame_size, is_video, frame.pts);
        }
    }

    release_frame_buffer(extractor, &frame);
    return err;
}

esp_err_t app_extractor_get_video_info(app_extractor_handle_t handle,
                                       uint32_t *width,
                                       uint32_t *height,
                                       uint32_t *fps,
                                       uint32_t *duration)
{
    if (handle == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    app_extractor_t *extractor = handle;
    if (!extractor->has_video) {
        return ESP_ERR_NOT_FOUND;
    }

    if (width) {
        *width = extractor->video_width;
    }
    if (height) {
        *height = extractor->video_height;
    }
    if (fps) {
        *fps = extractor->video_fps;
    }
    if (duration) {
        *duration = extractor->video_duration;
    }
    return ESP_OK;
}

esp_err_t app_extractor_get_audio_info(app_extractor_handle_t handle,
                                       uint32_t *sample_rate,
                                       uint8_t *channels,
                                       uint8_t *bits,
                                       uint32_t *duration)
{
    if (handle == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    app_extractor_t *extractor = handle;
    if (!extractor->has_audio) {
        return ESP_ERR_NOT_FOUND;
    }

    if (sample_rate) {
        *sample_rate = extractor->audio_sample_rate;
    }
    if (channels) {
        *channels = extractor->audio_channels;
    }
    if (bits) {
        *bits = extractor->audio_bits;
    }
    if (duration) {
        *duration = extractor->audio_duration;
    }
    return ESP_OK;
}

esp_err_t app_extractor_probe_video_info(const char *filename,
                                         uint32_t *width,
                                         uint32_t *height,
                                         uint32_t *fps,
                                         uint32_t *duration)
{
    if (filename == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    app_extractor_handle_t extractor = nullptr;
    esp_err_t err = app_extractor_init(nullptr, nullptr, &extractor);
    if (err != ESP_OK) {
        return err;
    }

    err = app_extractor_start(extractor, filename, true, false);
    if (err == ESP_OK) {
        err = app_extractor_get_video_info(extractor, width, height, fps, duration);
    }

    app_extractor_deinit(extractor);
    return err;
}

esp_err_t app_extractor_seek(app_extractor_handle_t handle, uint32_t position)
{
    if (handle == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    app_extractor_t *extractor = handle;
    if (extractor->extractor == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    extractor->eos_reached = false;
    return extr_to_esp_err(esp_extractor_seek(extractor->extractor, position));
}

esp_err_t app_extractor_stop(app_extractor_handle_t handle)
{
    if (handle == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    app_extractor_t *extractor = handle;
    extractor->eos_reached = true;

    if (extractor->extractor != nullptr) {
        const esp_err_t err = extr_to_esp_err(esp_extractor_close(extractor->extractor));
        extractor->extractor = nullptr;
        return err;
    }

    return ESP_OK;
}

esp_err_t app_extractor_deinit(app_extractor_handle_t handle)
{
    if (handle == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    app_extractor_stop(handle);
    delete handle;
    return ESP_OK;
}
