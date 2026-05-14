#include "factory_audio.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "bsp/esp-bsp.h"

namespace {

constexpr char TAG[] = "factory_audio";

constexpr uint32_t kSampleRate = CONFIG_FACTORY_AUDIO_SAMPLE_RATE;
constexpr uint32_t kRecordSeconds = CONFIG_FACTORY_AUDIO_RECORD_SECONDS;
constexpr uint8_t kVolumePercent = CONFIG_FACTORY_AUDIO_VOLUME_PERCENT;
constexpr uint8_t kLoopbackVolumePercent = CONFIG_FACTORY_AUDIO_LOOPBACK_VOLUME_PERCENT;
constexpr uint8_t kLoopbackGainPercent = CONFIG_FACTORY_AUDIO_LOOPBACK_GAIN_PERCENT;
constexpr uint8_t kLoopbackLimitPercent = CONFIG_FACTORY_AUDIO_LOOPBACK_LIMIT_PERCENT;
constexpr uint8_t kMicGainDb = CONFIG_FACTORY_AUDIO_MIC_GAIN_DB;
constexpr size_t kChannelCount = 2;
constexpr size_t kBytesPerSample = sizeof(int16_t);
constexpr size_t kFrameBytes = kChannelCount * kBytesPerSample;
constexpr size_t kIoFrames = 512;
constexpr size_t kIoBufferBytes = kIoFrames * kFrameBytes;
constexpr size_t kRecordBufferBytes = kSampleRate * kRecordSeconds * kFrameBytes;
constexpr uint8_t kMicPassPeakThreshold = CONFIG_FACTORY_AUDIO_MIC_PASS_PEAK_PERCENT;

enum class audio_command_t : uint8_t {
    Stop = 0,
    Monitor,
    Record,
    Playback,
    Loopback,
};

static esp_codec_dev_handle_t s_play_dev = nullptr;
static esp_codec_dev_handle_t s_record_dev = nullptr;
static QueueHandle_t s_cmd_queue = nullptr;
static TaskHandle_t s_audio_task = nullptr;
static SemaphoreHandle_t s_state_mutex = nullptr;
static int16_t *s_record_buffer = nullptr;
static size_t s_record_capacity = 0;
static size_t s_record_bytes = 0;
static bool s_init_ok = false;
static bool s_init_in_progress = false;
static bool s_stop_latched = false;
static uint8_t s_runtime_volume = kVolumePercent;

static factory_audio_state_t s_state = {
    .init_attempted = false,
    .codec_ready = false,
    .i2s_ready = false,
    .amp_ready = false,
    .mic_ready = false,
    .speaker_ready = false,
    .recording_ready = false,
    .clipping = false,
    .mode = FACTORY_AUDIO_MODE_IDLE,
    .sample_rate_hz = kSampleRate,
    .record_seconds = kRecordSeconds,
    .bytes_recorded = 0,
    .clip_count = 0,
    .volume_percent = kVolumePercent,
    .mic_gain_db = kMicGainDb,
    .rms_percent = 0,
    .peak_percent = 0,
    .noise_floor_percent = 0,
    .waveform = {},
    .waveform_count = FACTORY_AUDIO_WAVEFORM_BINS,
    .status_text = "Audio not initialized.",
    .result_text = "IDLE",
};

static uint8_t clamp_percent(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 100) {
        return 100;
    }
    return (uint8_t)value;
}

static void lock_state()
{
    if (s_state_mutex != nullptr) {
        xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    }
}

static void unlock_state()
{
    if (s_state_mutex != nullptr) {
        xSemaphoreGive(s_state_mutex);
    }
}

static void set_status(factory_audio_mode_t mode, const char *result, const char *fmt, ...)
{
    char status[sizeof(s_state.status_text)] = {};
    va_list args;
    va_start(args, fmt);
    vsnprintf(status, sizeof(status), fmt, args);
    va_end(args);

    lock_state();
    s_state.mode = mode;
    snprintf(s_state.status_text, sizeof(s_state.status_text), "%s", status);
    snprintf(s_state.result_text, sizeof(s_state.result_text), "%s", result != nullptr ? result : "");
    unlock_state();
}

static void set_ready_flags(bool codec_ready, bool i2s_ready, bool amp_ready)
{
    lock_state();
    s_state.init_attempted = true;
    s_state.codec_ready = codec_ready;
    s_state.i2s_ready = i2s_ready;
    s_state.amp_ready = amp_ready;
    unlock_state();
}

static esp_codec_dev_sample_info_t codec_sample_info()
{
    esp_codec_dev_sample_info_t sample_info = {
        .bits_per_sample = 16,
        .channel = 2,
        .channel_mask = 0,
        .sample_rate = kSampleRate,
        .mclk_multiple = 0,
    };
    return sample_info;
}

static esp_err_t audio_amp_set(bool enable)
{
    ESP_RETURN_ON_ERROR(t5_board_audio_select_speaker(true), TAG, "Select speaker path failed");
    ESP_RETURN_ON_ERROR(t5_board_audio_amp_enable(enable), TAG, "Set audio amplifier state failed");
    return ESP_OK;
}

static esp_err_t codec_mute(bool mute)
{
    ESP_RETURN_ON_FALSE(s_play_dev != nullptr, ESP_ERR_INVALID_STATE, TAG, "Speaker codec unavailable");
    ESP_RETURN_ON_ERROR(esp_codec_dev_set_out_mute(s_play_dev, mute), TAG, "Set mute failed");
    if (!mute) {
        ESP_RETURN_ON_ERROR(esp_codec_dev_set_out_vol(s_play_dev, s_runtime_volume), TAG, "Restore volume failed");
    }
    return ESP_OK;
}

static esp_err_t set_output_volume(uint8_t volume_percent)
{
    ESP_RETURN_ON_FALSE(s_play_dev != nullptr, ESP_ERR_INVALID_STATE, TAG, "Speaker codec unavailable");
    ESP_RETURN_ON_ERROR(esp_codec_dev_set_out_vol(s_play_dev, volume_percent), TAG, "Set output volume failed");
    s_runtime_volume = volume_percent;
    lock_state();
    s_state.volume_percent = volume_percent;
    unlock_state();
    return ESP_OK;
}

static esp_err_t codec_read_samples(void *audio_buffer, size_t len, size_t *bytes_read)
{
    ESP_RETURN_ON_FALSE(s_record_dev != nullptr, ESP_ERR_INVALID_STATE, TAG, "Microphone codec unavailable");
    esp_err_t ret = esp_codec_dev_read(s_record_dev, audio_buffer, len);
    if (bytes_read != nullptr) {
        *bytes_read = (ret == ESP_OK) ? len : 0;
    }
    return ret;
}

static esp_err_t codec_write_samples(const void *audio_buffer, size_t len, size_t *bytes_written)
{
    ESP_RETURN_ON_FALSE(s_play_dev != nullptr, ESP_ERR_INVALID_STATE, TAG, "Speaker codec unavailable");
    esp_err_t ret = esp_codec_dev_write(s_play_dev, (void *)audio_buffer, len);
    if (bytes_written != nullptr) {
        *bytes_written = (ret == ESP_OK) ? len : 0;
    }
    return ret;
}

static esp_err_t init_audio_devices()
{
    if (s_play_dev != nullptr && s_record_dev != nullptr) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "Initialize shared I2C failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = BSP_I2S_GPIO_CFG,
    };
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    ESP_RETURN_ON_ERROR(bsp_audio_init(&std_cfg), TAG, "Initialize board audio I2S failed");

    if (s_play_dev == nullptr) {
        s_play_dev = bsp_audio_codec_speaker_init();
        ESP_RETURN_ON_FALSE(s_play_dev != nullptr, ESP_FAIL, TAG, "Create speaker codec handle failed");
    }
    if (s_record_dev == nullptr) {
        s_record_dev = bsp_audio_codec_microphone_init();
        ESP_RETURN_ON_FALSE(s_record_dev != nullptr, ESP_FAIL, TAG, "Create microphone codec handle failed");
    }

    esp_codec_dev_sample_info_t sample_info = codec_sample_info();
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(s_play_dev, &sample_info), TAG, "Open speaker codec failed");
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(s_record_dev, &sample_info), TAG, "Open microphone codec failed");
    ESP_RETURN_ON_ERROR(set_output_volume(kVolumePercent), TAG, "Apply output volume failed");
    ESP_RETURN_ON_ERROR(esp_codec_dev_set_in_gain(s_record_dev, kMicGainDb), TAG, "Set microphone gain failed");
    ESP_RETURN_ON_ERROR(codec_mute(false), TAG, "Unmute speaker codec failed");
    return ESP_OK;
}

static void clear_waveform_locked()
{
    memset(s_state.waveform, 0, sizeof(s_state.waveform));
    s_state.waveform_count = FACTORY_AUDIO_WAVEFORM_BINS;
}

static void analyze_samples(const int16_t *samples, size_t sample_count, bool from_recording)
{
    if (samples == nullptr || sample_count == 0) {
        return;
    }

    uint64_t sum_sq = 0;
    int32_t peak = 0;
    uint32_t clips = 0;
    int8_t waveform[FACTORY_AUDIO_WAVEFORM_BINS] = {};

    for (size_t i = 0; i < sample_count; ++i) {
        int32_t sample = samples[i];
        int32_t abs_sample = sample < 0 ? -sample : sample;
        if (abs_sample > peak) {
            peak = abs_sample;
        }
        if (abs_sample >= 32000) {
            clips++;
        }
        sum_sq += (uint64_t)abs_sample * (uint64_t)abs_sample;
    }

    for (size_t i = 0; i < FACTORY_AUDIO_WAVEFORM_BINS; ++i) {
        const size_t index = (sample_count <= FACTORY_AUDIO_WAVEFORM_BINS)
                                 ? ((i < sample_count) ? i : (sample_count - 1))
                                 : ((i * (sample_count - 1)) / (FACTORY_AUDIO_WAVEFORM_BINS - 1));
        int32_t scaled = (int32_t)samples[index] * 100 / 32768;
        if (scaled > 100) {
            scaled = 100;
        } else if (scaled < -100) {
            scaled = -100;
        }
        waveform[i] = (int8_t)scaled;
    }

    const float mean_sq = (float)((double)sum_sq / (double)sample_count);
    const uint8_t rms_percent = clamp_percent((int)((sqrtf(mean_sq) * 100.0f / 32768.0f) + 0.5f));
    const uint8_t peak_percent = clamp_percent((int)((peak * 100 / 32768) + 0));
    const bool mic_pass = peak_percent >= kMicPassPeakThreshold;

    lock_state();
    s_state.rms_percent = rms_percent;
    s_state.peak_percent = peak_percent;
    s_state.clip_count = clips;
    s_state.clipping = clips > 0;
    s_state.mic_ready = s_state.mic_ready || mic_pass;
    if (s_state.noise_floor_percent == 0 || rms_percent < s_state.noise_floor_percent) {
        s_state.noise_floor_percent = rms_percent;
    }
    memcpy(s_state.waveform, waveform, sizeof(s_state.waveform));
    s_state.waveform_count = FACTORY_AUDIO_WAVEFORM_BINS;
    if (from_recording) {
        s_state.recording_ready = s_record_bytes > 0;
        s_state.bytes_recorded = (uint32_t)s_record_bytes;
    }
    unlock_state();
}

static bool stop_requested()
{
    if (s_cmd_queue == nullptr) {
        return false;
    }

    audio_command_t pending = audio_command_t::Stop;
    if (xQueuePeek(s_cmd_queue, &pending, 0) == pdTRUE && pending == audio_command_t::Stop) {
        xQueueReceive(s_cmd_queue, &pending, 0);
        s_stop_latched = true;
        return true;
    }
    return false;
}

static audio_command_t next_mode_after_operation()
{
    if (s_stop_latched) {
        s_stop_latched = false;
        return audio_command_t::Stop;
    }
    return audio_command_t::Monitor;
}

static void perform_monitor(int16_t *io_buffer)
{
    size_t bytes_read = 0;
    esp_err_t ret = codec_read_samples(io_buffer, kIoBufferBytes, &bytes_read);
    if (ret != ESP_OK || bytes_read == 0) {
        set_status(FACTORY_AUDIO_MODE_ERROR, "FAIL", "Audio RX failed: %s", esp_err_to_name(ret));
        return;
    }

    analyze_samples(io_buffer, bytes_read / sizeof(int16_t), false);
    lock_state();
    s_state.mode = FACTORY_AUDIO_MODE_MONITOR;
    snprintf(s_state.result_text, sizeof(s_state.result_text), "%s", s_state.mic_ready ? "LIVE" : "LISTEN");
    snprintf(s_state.status_text,
             sizeof(s_state.status_text),
             "Listening: RMS %u%%, Peak %u%%.",
             s_state.rms_percent,
             s_state.peak_percent);
    unlock_state();
}

static void perform_record(int16_t *io_buffer)
{
    s_record_bytes = 0;
    lock_state();
    s_state.mode = FACTORY_AUDIO_MODE_RECORD;
    s_state.recording_ready = false;
    s_state.bytes_recorded = 0;
    s_state.speaker_ready = false;
    s_state.clip_count = 0;
    s_state.clipping = false;
    clear_waveform_locked();
    snprintf(s_state.result_text, sizeof(s_state.result_text), "REC");
    snprintf(s_state.status_text, sizeof(s_state.status_text), "Recording %u seconds...", (unsigned)kRecordSeconds);
    unlock_state();

    while (s_record_bytes < s_record_capacity) {
        if (stop_requested()) {
            set_status(FACTORY_AUDIO_MODE_IDLE, "STOP", "Recording stopped.");
            return;
        }

        const size_t remaining = s_record_capacity - s_record_bytes;
        const size_t request = remaining < kIoBufferBytes ? remaining : kIoBufferBytes;
        size_t bytes_read = 0;
        esp_err_t ret = codec_read_samples(io_buffer, request, &bytes_read);
        if (ret != ESP_OK || bytes_read == 0) {
            set_status(FACTORY_AUDIO_MODE_ERROR, "FAIL", "Audio RX failed: %s", esp_err_to_name(ret));
            return;
        }

        memcpy((uint8_t *)s_record_buffer + s_record_bytes, io_buffer, bytes_read);
        s_record_bytes += bytes_read;

        lock_state();
        s_state.bytes_recorded = (uint32_t)s_record_bytes;
        unlock_state();
    }

    analyze_samples(s_record_buffer, s_record_bytes / sizeof(int16_t), true);
    lock_state();
    s_state.mode = FACTORY_AUDIO_MODE_MONITOR;
    snprintf(s_state.result_text, sizeof(s_state.result_text), "%s", s_state.mic_ready ? "MIC PASS" : "MIC LOW");
    snprintf(s_state.status_text,
             sizeof(s_state.status_text),
             "Recorded %u KB. Peak %u%%, RMS %u%%.",
             (unsigned)(s_record_bytes / 1024),
             s_state.peak_percent,
             s_state.rms_percent);
    unlock_state();
}

static void perform_playback()
{
    if (s_record_buffer == nullptr || s_record_bytes == 0) {
        set_status(FACTORY_AUDIO_MODE_MONITOR, "NO REC", "Record a sample before playback.");
        return;
    }

    lock_state();
    s_state.mode = FACTORY_AUDIO_MODE_PLAYBACK;
    s_state.speaker_ready = false;
    snprintf(s_state.result_text, sizeof(s_state.result_text), "PLAY");
    snprintf(s_state.status_text, sizeof(s_state.status_text), "Playing recorded sample...");
    unlock_state();

    (void)audio_amp_set(true);
    (void)set_output_volume(kVolumePercent);
    (void)codec_mute(false);

    size_t offset = 0;
    bool ok = true;
    while (offset < s_record_bytes) {
        if (stop_requested()) {
            ok = false;
            break;
        }

        const size_t remaining = s_record_bytes - offset;
        const size_t to_write = remaining < kIoBufferBytes ? remaining : kIoBufferBytes;
        size_t bytes_written = 0;
        esp_err_t ret = codec_write_samples((const uint8_t *)s_record_buffer + offset, to_write, &bytes_written);
        if (ret != ESP_OK || bytes_written == 0) {
            set_status(FACTORY_AUDIO_MODE_ERROR, "FAIL", "Audio TX failed: %s", esp_err_to_name(ret));
            ok = false;
            break;
        }
        offset += bytes_written;
    }

    (void)audio_amp_set(false);
    (void)set_output_volume(kVolumePercent);

    lock_state();
    s_state.mode = FACTORY_AUDIO_MODE_MONITOR;
    s_state.speaker_ready = ok && offset >= s_record_bytes;
    snprintf(s_state.result_text, sizeof(s_state.result_text), "%s", s_state.speaker_ready ? "SPK PASS" : "STOP");
    snprintf(s_state.status_text,
             sizeof(s_state.status_text),
             "%s",
             s_state.speaker_ready ? "Playback complete. Speaker path wrote successfully." : "Playback stopped.");
    unlock_state();
}

static void perform_loopback(int16_t *io_buffer)
{
    set_status(FACTORY_AUDIO_MODE_LOOPBACK, "SAFE LOOP", "Safe loopback: low volume, attenuated, limited. Press Stop to exit.");
    (void)set_output_volume(kLoopbackVolumePercent);
    (void)audio_amp_set(true);
    (void)codec_mute(false);

    while (!stop_requested()) {
        size_t bytes_read = 0;
        esp_err_t ret = codec_read_samples(io_buffer, kIoBufferBytes, &bytes_read);
        if (ret != ESP_OK || bytes_read == 0) {
            set_status(FACTORY_AUDIO_MODE_ERROR, "FAIL", "Audio RX failed: %s", esp_err_to_name(ret));
            break;
        }

        analyze_samples(io_buffer, bytes_read / sizeof(int16_t), false);
        const int32_t output_limit = (int32_t)32767 * kLoopbackLimitPercent / 100;
        const size_t sample_count = bytes_read / sizeof(int16_t);
        for (size_t i = 0; i < sample_count; ++i) {
            int32_t sample = (int32_t)io_buffer[i] * kLoopbackGainPercent / 100;
            if (sample > output_limit) {
                sample = output_limit;
            } else if (sample < -output_limit) {
                sample = -output_limit;
            }
            io_buffer[i] = (int16_t)sample;
        }

        size_t bytes_written = 0;
        ret = codec_write_samples(io_buffer, bytes_read, &bytes_written);
        if (ret != ESP_OK || bytes_written == 0) {
            set_status(FACTORY_AUDIO_MODE_ERROR, "FAIL", "Audio TX failed: %s", esp_err_to_name(ret));
            break;
        }
    }

    (void)audio_amp_set(false);
    (void)set_output_volume(kVolumePercent);
    set_status(FACTORY_AUDIO_MODE_MONITOR, "LIVE", "Loopback stopped. Monitoring microphone.");
}

static void audio_task(void *arg)
{
    (void)arg;

    int16_t *io_buffer = (int16_t *)heap_caps_malloc(kIoBufferBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (io_buffer == nullptr) {
        io_buffer = (int16_t *)heap_caps_malloc(kIoBufferBytes, MALLOC_CAP_8BIT);
    }
    if (io_buffer == nullptr) {
        set_status(FACTORY_AUDIO_MODE_ERROR, "FAIL", "Unable to allocate audio IO buffer.");
        vTaskDelete(nullptr);
        return;
    }

    audio_command_t mode = audio_command_t::Monitor;
    while (true) {
        audio_command_t cmd = mode;
        if (xQueueReceive(s_cmd_queue, &cmd, 0) == pdTRUE) {
            mode = cmd;
        }

        switch (mode) {
            case audio_command_t::Stop:
                (void)audio_amp_set(false);
                set_status(FACTORY_AUDIO_MODE_IDLE, "STOP", "Audio stopped.");
                xQueueReceive(s_cmd_queue, &mode, portMAX_DELAY);
                continue;
            case audio_command_t::Record:
                perform_record(io_buffer);
                mode = next_mode_after_operation();
                break;
            case audio_command_t::Playback:
                perform_playback();
                mode = next_mode_after_operation();
                break;
            case audio_command_t::Loopback:
                perform_loopback(io_buffer);
                mode = next_mode_after_operation();
                break;
            case audio_command_t::Monitor:
            default:
                perform_monitor(io_buffer);
                vTaskDelay(pdMS_TO_TICKS(CONFIG_FACTORY_AUDIO_MONITOR_PERIOD_MS));
                mode = audio_command_t::Monitor;
                break;
        }
    }
}

static bool send_command(audio_command_t command)
{
    if (!factory_audio_init() || s_cmd_queue == nullptr) {
        return false;
    }
    xQueueOverwrite(s_cmd_queue, &command);
    return true;
}

static bool allocate_record_buffer()
{
    if (s_record_buffer != nullptr && s_record_capacity >= kRecordBufferBytes) {
        return true;
    }

    s_record_buffer = (int16_t *)heap_caps_malloc(kRecordBufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_record_buffer == nullptr) {
        s_record_buffer = (int16_t *)heap_caps_malloc(kRecordBufferBytes, MALLOC_CAP_8BIT);
    }
    if (s_record_buffer == nullptr) {
        return false;
    }

    s_record_capacity = kRecordBufferBytes;
    return true;
}

}  // namespace

extern "C" bool factory_audio_init(void)
{
    if (s_init_ok) {
        return true;
    }
    if (s_init_in_progress) {
        return false;
    }

    s_init_in_progress = true;
    if (s_state_mutex == nullptr) {
        s_state_mutex = xSemaphoreCreateMutex();
    }
    lock_state();
    s_state.init_attempted = true;
    s_state.sample_rate_hz = kSampleRate;
    s_state.record_seconds = kRecordSeconds;
    s_state.volume_percent = kVolumePercent;
    s_state.mic_gain_db = kMicGainDb;
    snprintf(s_state.status_text, sizeof(s_state.status_text), "Initializing ES8311 audio path...");
    snprintf(s_state.result_text, sizeof(s_state.result_text), "INIT");
    unlock_state();

    if (!allocate_record_buffer()) {
        set_ready_flags(false, false, false);
        set_status(FACTORY_AUDIO_MODE_ERROR, "FAIL", "Unable to allocate %u KB record buffer.",
                   (unsigned)(kRecordBufferBytes / 1024));
        s_init_in_progress = false;
        return false;
    }

    esp_err_t ret = init_audio_devices();
    if (ret != ESP_OK) {
        set_ready_flags(false, false, false);
        set_status(FACTORY_AUDIO_MODE_ERROR, "FAIL", "Board audio init failed: %s", esp_err_to_name(ret));
        s_init_in_progress = false;
        return false;
    }

    const bool amp_control_ready = audio_amp_set(false) == ESP_OK;
    set_ready_flags(true, true, amp_control_ready);

    if (s_cmd_queue == nullptr) {
        s_cmd_queue = xQueueCreate(1, sizeof(audio_command_t));
    }
    if (s_cmd_queue == nullptr) {
        set_status(FACTORY_AUDIO_MODE_ERROR, "FAIL", "Unable to create audio command queue.");
        s_init_in_progress = false;
        return false;
    }

    if (s_audio_task == nullptr) {
        BaseType_t task_ok = xTaskCreate(
            audio_task, "factory_audio", CONFIG_FACTORY_AUDIO_TASK_STACK_SIZE, nullptr, 5, &s_audio_task);
        if (task_ok != pdPASS) {
            set_status(FACTORY_AUDIO_MODE_ERROR, "FAIL", "Unable to start audio task.");
            s_audio_task = nullptr;
            s_init_in_progress = false;
            return false;
        }
    }

    s_init_ok = true;
    s_init_in_progress = false;
    set_status(FACTORY_AUDIO_MODE_MONITOR, "READY", "Audio ready. Speak or record a 3 second sample.");
    return true;
}

extern "C" bool factory_audio_is_ready(void)
{
    return s_init_ok;
}

extern "C" void factory_audio_start_monitor(void)
{
    (void)send_command(audio_command_t::Monitor);
}

extern "C" void factory_audio_record_3s(void)
{
    (void)send_command(audio_command_t::Record);
}

extern "C" void factory_audio_playback(void)
{
    (void)send_command(audio_command_t::Playback);
}

extern "C" void factory_audio_start_loopback(void)
{
    (void)send_command(audio_command_t::Loopback);
}

extern "C" void factory_audio_stop(void)
{
    if (s_cmd_queue == nullptr) {
        return;
    }
    audio_command_t command = audio_command_t::Stop;
    xQueueOverwrite(s_cmd_queue, &command);
}

extern "C" void factory_audio_get_state(factory_audio_state_t *state)
{
    if (state == nullptr) {
        return;
    }

    lock_state();
    *state = s_state;
    unlock_state();
}
