#include "factory_audio.h"

#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_bit_defs.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "board_config.h"
#include "factory_display.h"

namespace {

constexpr char TAG[] = "factory_audio";

constexpr uint8_t kEs8311Addr = 0x18;
constexpr uint8_t kPca9535Addr = 0x20;
constexpr uint8_t kPcaOutputPort0Reg = 0x02;
constexpr uint8_t kPcaConfigPort0Reg = 0x06;
constexpr uint8_t kAmpEnableMask = (1U << FACTORY_PCA_SHUTDOWN);
constexpr uint32_t kSampleRate = CONFIG_FACTORY_AUDIO_SAMPLE_RATE;
constexpr uint32_t kRecordSeconds = CONFIG_FACTORY_AUDIO_RECORD_SECONDS;
constexpr uint8_t kVolumePercent = CONFIG_FACTORY_AUDIO_VOLUME_PERCENT;
constexpr uint8_t kMicGainDb = CONFIG_FACTORY_AUDIO_MIC_GAIN_DB;
constexpr uint32_t kMclkMultiple = 256;
constexpr uint32_t kMclkHz = kSampleRate * kMclkMultiple;
constexpr size_t kChannelCount = 2;
constexpr size_t kBytesPerSample = sizeof(int16_t);
constexpr size_t kFrameBytes = kChannelCount * kBytesPerSample;
constexpr size_t kIoFrames = 512;
constexpr size_t kIoBufferBytes = kIoFrames * kFrameBytes;
constexpr size_t kRecordBufferBytes = kSampleRate * kRecordSeconds * kFrameBytes;
constexpr uint8_t kMicPassPeakThreshold = CONFIG_FACTORY_AUDIO_MIC_PASS_PEAK_PERCENT;
constexpr uint32_t kI2cTimeoutMs = 1000;

enum class audio_command_t : uint8_t {
    Stop = 0,
    Monitor,
    Record,
    Playback,
    Loopback,
};

struct coeff_div_t {
    uint32_t mclk;
    uint32_t rate;
    uint8_t pre_div;
    uint8_t pre_multi;
    uint8_t adc_div;
    uint8_t dac_div;
    uint8_t fs_mode;
    uint8_t lrck_h;
    uint8_t lrck_l;
    uint8_t bclk_div;
    uint8_t adc_osr;
    uint8_t dac_osr;
};

// Minimal 256x-MCLK coefficient set copied from Espressif's Apache-2.0 ES8311 driver.
static const coeff_div_t kCoeffDiv[] = {
    {2048000, 8000, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {4096000, 16000, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000, 24000, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {8192000, 32000, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {11289600, 44100, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {12288000, 48000, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
};

enum es8311_reg_t : uint8_t {
    ES8311_RESET_REG00 = 0x00,
    ES8311_CLK_MANAGER_REG01 = 0x01,
    ES8311_CLK_MANAGER_REG02 = 0x02,
    ES8311_CLK_MANAGER_REG03 = 0x03,
    ES8311_CLK_MANAGER_REG04 = 0x04,
    ES8311_CLK_MANAGER_REG05 = 0x05,
    ES8311_CLK_MANAGER_REG06 = 0x06,
    ES8311_CLK_MANAGER_REG07 = 0x07,
    ES8311_CLK_MANAGER_REG08 = 0x08,
    ES8311_SDPIN_REG09 = 0x09,
    ES8311_SDPOUT_REG0A = 0x0A,
    ES8311_SYSTEM_REG0D = 0x0D,
    ES8311_SYSTEM_REG0E = 0x0E,
    ES8311_SYSTEM_REG12 = 0x12,
    ES8311_SYSTEM_REG13 = 0x13,
    ES8311_SYSTEM_REG14 = 0x14,
    ES8311_ADC_REG16 = 0x16,
    ES8311_ADC_REG17 = 0x17,
    ES8311_ADC_REG1C = 0x1C,
    ES8311_DAC_REG31 = 0x31,
    ES8311_DAC_REG32 = 0x32,
    ES8311_DAC_REG37 = 0x37,
    ES8311_CHD1_REGFD = 0xFD,
    ES8311_CHD2_REGFE = 0xFE,
};

static i2c_master_dev_handle_t s_codec_dev = nullptr;
static i2c_master_dev_handle_t s_pca_dev = nullptr;
static i2s_chan_handle_t s_tx_handle = nullptr;
static i2s_chan_handle_t s_rx_handle = nullptr;
static QueueHandle_t s_cmd_queue = nullptr;
static TaskHandle_t s_audio_task = nullptr;
static SemaphoreHandle_t s_state_mutex = nullptr;
static int16_t *s_record_buffer = nullptr;
static size_t s_record_capacity = 0;
static size_t s_record_bytes = 0;
static bool s_init_ok = false;
static bool s_init_in_progress = false;
static bool s_stop_latched = false;

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

static esp_err_t add_i2c_device(uint8_t address, i2c_master_dev_handle_t *handle)
{
    if (*handle != nullptr) {
        return ESP_OK;
    }

    i2c_master_bus_handle_t bus = factory_display_get_i2c_bus();
    if (bus == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = address;
    dev_cfg.scl_speed_hz = FACTORY_I2C_FREQ_HZ;
    return i2c_master_bus_add_device(bus, &dev_cfg, handle);
}

static esp_err_t codec_write(uint8_t reg, uint8_t value)
{
    const uint8_t data[2] = {reg, value};
    return i2c_master_transmit(s_codec_dev, data, sizeof(data), kI2cTimeoutMs);
}

static esp_err_t codec_read(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(s_codec_dev, &reg, 1, value, 1, kI2cTimeoutMs);
}

static esp_err_t pca_write(uint8_t reg, uint8_t value)
{
    if (s_pca_dev == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint8_t data[2] = {reg, value};
    return i2c_master_transmit(s_pca_dev, data, sizeof(data), kI2cTimeoutMs);
}

static esp_err_t pca_read(uint8_t reg, uint8_t *value)
{
    if (s_pca_dev == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit_receive(s_pca_dev, &reg, 1, value, 1, kI2cTimeoutMs);
}

static esp_err_t audio_amp_set(bool enable)
{
    if (s_pca_dev == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t output = 0;
    uint8_t config = 0xFF;
    esp_err_t ret = pca_read(kPcaOutputPort0Reg, &output);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = pca_read(kPcaConfigPort0Reg, &config);
    if (ret != ESP_OK) {
        return ret;
    }

    if (enable) {
        output |= kAmpEnableMask;
    } else {
        output &= (uint8_t)~kAmpEnableMask;
    }
    config &= (uint8_t)~kAmpEnableMask;

    ret = pca_write(kPcaOutputPort0Reg, output);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = pca_write(kPcaConfigPort0Reg, config);
    if (ret != ESP_OK) {
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}

static const coeff_div_t *find_coeff(uint32_t mclk, uint32_t rate)
{
    for (const auto &coeff : kCoeffDiv) {
        if (coeff.mclk == mclk && coeff.rate == rate) {
            return &coeff;
        }
    }
    return nullptr;
}

static esp_err_t es8311_sample_frequency_config(uint32_t mclk, uint32_t rate)
{
    const coeff_div_t *coeff = find_coeff(mclk, rate);
    if (coeff == nullptr) {
        ESP_LOGE(TAG, "unsupported sample rate: %" PRIu32 " Hz with %" PRIu32 " Hz MCLK", rate, mclk);
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t regv = 0;
    ESP_RETURN_ON_ERROR(codec_read(ES8311_CLK_MANAGER_REG02, &regv), TAG, "read reg02 failed");
    regv &= 0x07;
    regv |= (uint8_t)((coeff->pre_div - 1) << 5);
    regv |= (uint8_t)(coeff->pre_multi << 3);
    ESP_RETURN_ON_ERROR(codec_write(ES8311_CLK_MANAGER_REG02, regv), TAG, "write reg02 failed");

    ESP_RETURN_ON_ERROR(codec_write(ES8311_CLK_MANAGER_REG03, (uint8_t)((coeff->fs_mode << 6) | coeff->adc_osr)),
                        TAG,
                        "write reg03 failed");
    ESP_RETURN_ON_ERROR(codec_write(ES8311_CLK_MANAGER_REG04, coeff->dac_osr), TAG, "write reg04 failed");
    ESP_RETURN_ON_ERROR(codec_write(ES8311_CLK_MANAGER_REG05,
                                    (uint8_t)(((coeff->adc_div - 1) << 4) | (coeff->dac_div - 1))),
                        TAG,
                        "write reg05 failed");

    ESP_RETURN_ON_ERROR(codec_read(ES8311_CLK_MANAGER_REG06, &regv), TAG, "read reg06 failed");
    regv &= 0xE0;
    regv |= (uint8_t)((coeff->bclk_div < 19) ? (coeff->bclk_div - 1) : coeff->bclk_div);
    ESP_RETURN_ON_ERROR(codec_write(ES8311_CLK_MANAGER_REG06, regv), TAG, "write reg06 failed");

    ESP_RETURN_ON_ERROR(codec_read(ES8311_CLK_MANAGER_REG07, &regv), TAG, "read reg07 failed");
    regv &= 0xC0;
    regv |= coeff->lrck_h;
    ESP_RETURN_ON_ERROR(codec_write(ES8311_CLK_MANAGER_REG07, regv), TAG, "write reg07 failed");
    ESP_RETURN_ON_ERROR(codec_write(ES8311_CLK_MANAGER_REG08, coeff->lrck_l), TAG, "write reg08 failed");
    return ESP_OK;
}

static uint8_t mic_gain_register_value()
{
    uint8_t gain = kMicGainDb;
    if (gain > 42) {
        gain = 42;
    }
    return (uint8_t)((gain + 3) / 6);
}

static esp_err_t es8311_set_volume(uint8_t volume_percent)
{
    const uint8_t reg32 = (volume_percent == 0) ? 0 : (uint8_t)(((int)volume_percent * 256 / 100) - 1);
    return codec_write(ES8311_DAC_REG32, reg32);
}

static esp_err_t es8311_mute(bool mute)
{
    uint8_t reg31 = 0;
    ESP_RETURN_ON_ERROR(codec_read(ES8311_DAC_REG31, &reg31), TAG, "read mute reg failed");
    if (mute) {
        reg31 |= (uint8_t)(BIT(6) | BIT(5));
    } else {
        reg31 &= (uint8_t)~(BIT(6) | BIT(5));
    }
    return codec_write(ES8311_DAC_REG31, reg31);
}

static esp_err_t es8311_init_codec()
{
    uint8_t chip_id1 = 0;
    uint8_t chip_id2 = 0;
    ESP_RETURN_ON_ERROR(codec_read(ES8311_CHD1_REGFD, &chip_id1), TAG, "read chip id1 failed");
    ESP_RETURN_ON_ERROR(codec_read(ES8311_CHD2_REGFE, &chip_id2), TAG, "read chip id2 failed");
    ESP_LOGI(TAG, "ES8311 detected: id=%02x%02x", chip_id1, chip_id2);

    ESP_RETURN_ON_ERROR(codec_write(ES8311_RESET_REG00, 0x1F), TAG, "codec reset failed");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(codec_write(ES8311_RESET_REG00, 0x00), TAG, "codec reset release failed");
    ESP_RETURN_ON_ERROR(codec_write(ES8311_RESET_REG00, 0x80), TAG, "codec power-on failed");

    ESP_RETURN_ON_ERROR(codec_write(ES8311_CLK_MANAGER_REG01, 0x3F), TAG, "clock enable failed");
    uint8_t reg06 = 0;
    ESP_RETURN_ON_ERROR(codec_read(ES8311_CLK_MANAGER_REG06, &reg06), TAG, "read clock reg failed");
    reg06 &= (uint8_t)~BIT(5);
    ESP_RETURN_ON_ERROR(codec_write(ES8311_CLK_MANAGER_REG06, reg06), TAG, "write clock reg failed");
    ESP_RETURN_ON_ERROR(es8311_sample_frequency_config(kMclkHz, kSampleRate), TAG, "sample rate config failed");

    uint8_t reg00 = 0;
    ESP_RETURN_ON_ERROR(codec_read(ES8311_RESET_REG00, &reg00), TAG, "read reset reg failed");
    reg00 &= 0xBF;
    ESP_RETURN_ON_ERROR(codec_write(ES8311_RESET_REG00, reg00), TAG, "set slave mode failed");
    ESP_RETURN_ON_ERROR(codec_write(ES8311_SDPIN_REG09, 0x0C), TAG, "set input format failed");
    ESP_RETURN_ON_ERROR(codec_write(ES8311_SDPOUT_REG0A, 0x0C), TAG, "set output format failed");

    ESP_RETURN_ON_ERROR(codec_write(ES8311_SYSTEM_REG0D, 0x01), TAG, "power analog failed");
    ESP_RETURN_ON_ERROR(codec_write(ES8311_SYSTEM_REG0E, 0x02), TAG, "power adc failed");
    ESP_RETURN_ON_ERROR(codec_write(ES8311_SYSTEM_REG12, 0x00), TAG, "power dac failed");
    ESP_RETURN_ON_ERROR(codec_write(ES8311_SYSTEM_REG13, 0x10), TAG, "enable hp drive failed");
    ESP_RETURN_ON_ERROR(codec_write(ES8311_ADC_REG1C, 0x6A), TAG, "bypass adc eq failed");
    ESP_RETURN_ON_ERROR(codec_write(ES8311_DAC_REG37, 0x08), TAG, "bypass dac eq failed");
    ESP_RETURN_ON_ERROR(codec_write(ES8311_ADC_REG17, 0xC8), TAG, "set adc volume failed");
    ESP_RETURN_ON_ERROR(codec_write(ES8311_SYSTEM_REG14, 0x1A), TAG, "enable analog mic failed");
    ESP_RETURN_ON_ERROR(codec_write(ES8311_ADC_REG16, mic_gain_register_value()), TAG, "set mic gain failed");
    ESP_RETURN_ON_ERROR(es8311_set_volume(kVolumePercent), TAG, "set volume failed");
    ESP_RETURN_ON_ERROR(es8311_mute(false), TAG, "unmute failed");
    return ESP_OK;
}

static esp_err_t i2s_driver_init()
{
    if (s_tx_handle != nullptr && s_rx_handle != nullptr) {
        return ESP_OK;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx_handle, &s_rx_handle), TAG, "create i2s channels failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = GPIO_NUM_43,
            .bclk = GPIO_NUM_42,
            .ws = GPIO_NUM_40,
            .dout = GPIO_NUM_39,
            .din = GPIO_NUM_41,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx_handle, &std_cfg), TAG, "init i2s tx failed");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx_handle, &std_cfg), TAG, "init i2s rx failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx_handle), TAG, "enable i2s tx failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx_handle), TAG, "enable i2s rx failed");
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
    esp_err_t ret = i2s_channel_read(s_rx_handle, io_buffer, kIoBufferBytes, &bytes_read, pdMS_TO_TICKS(120));
    if (ret == ESP_ERR_TIMEOUT || bytes_read == 0) {
        return;
    }
    if (ret != ESP_OK) {
        set_status(FACTORY_AUDIO_MODE_ERROR, "FAIL", "I2S RX failed: %s", esp_err_to_name(ret));
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
        esp_err_t ret = i2s_channel_read(s_rx_handle, io_buffer, request, &bytes_read, pdMS_TO_TICKS(200));
        if (ret == ESP_ERR_TIMEOUT || bytes_read == 0) {
            continue;
        }
        if (ret != ESP_OK) {
            set_status(FACTORY_AUDIO_MODE_ERROR, "FAIL", "I2S RX failed: %s", esp_err_to_name(ret));
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
    (void)es8311_mute(false);

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
        esp_err_t ret = i2s_channel_write(
            s_tx_handle, (const uint8_t *)s_record_buffer + offset, to_write, &bytes_written, pdMS_TO_TICKS(250));
        if (ret != ESP_OK) {
            set_status(FACTORY_AUDIO_MODE_ERROR, "FAIL", "I2S TX failed: %s", esp_err_to_name(ret));
            ok = false;
            break;
        }
        if (bytes_written == 0) {
            ok = false;
            break;
        }
        offset += bytes_written;
    }

    (void)audio_amp_set(false);

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
    set_status(FACTORY_AUDIO_MODE_LOOPBACK, "LOOP", "Live mic-to-speaker loopback. Press Stop to exit.");
    (void)audio_amp_set(true);
    (void)es8311_mute(false);

    while (!stop_requested()) {
        size_t bytes_read = 0;
        esp_err_t ret = i2s_channel_read(s_rx_handle, io_buffer, kIoBufferBytes, &bytes_read, pdMS_TO_TICKS(120));
        if (ret == ESP_ERR_TIMEOUT || bytes_read == 0) {
            continue;
        }
        if (ret != ESP_OK) {
            set_status(FACTORY_AUDIO_MODE_ERROR, "FAIL", "I2S RX failed: %s", esp_err_to_name(ret));
            break;
        }

        analyze_samples(io_buffer, bytes_read / sizeof(int16_t), false);
        size_t bytes_written = 0;
        ret = i2s_channel_write(s_tx_handle, io_buffer, bytes_read, &bytes_written, pdMS_TO_TICKS(120));
        if (ret != ESP_OK || bytes_written == 0) {
            set_status(FACTORY_AUDIO_MODE_ERROR, "FAIL", "I2S TX failed: %s", esp_err_to_name(ret));
            break;
        }
    }

    (void)audio_amp_set(false);
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

    esp_err_t ret = add_i2c_device(kEs8311Addr, &s_codec_dev);
    if (ret != ESP_OK) {
        set_ready_flags(false, false, false);
        set_status(FACTORY_AUDIO_MODE_ERROR, "FAIL", "ES8311 I2C add failed: %s", esp_err_to_name(ret));
        s_init_in_progress = false;
        return false;
    }

    ret = add_i2c_device(kPca9535Addr, &s_pca_dev);
    const bool amp_control_ready = ret == ESP_OK;
    if (!amp_control_ready) {
        ESP_LOGW(TAG, "PCA9535 amp control unavailable: %s", esp_err_to_name(ret));
    }

    if (!allocate_record_buffer()) {
        set_ready_flags(false, false, amp_control_ready);
        set_status(FACTORY_AUDIO_MODE_ERROR, "FAIL", "Unable to allocate %u KB record buffer.",
                   (unsigned)(kRecordBufferBytes / 1024));
        s_init_in_progress = false;
        return false;
    }

    ret = i2s_driver_init();
    if (ret != ESP_OK) {
        set_ready_flags(false, false, amp_control_ready);
        set_status(FACTORY_AUDIO_MODE_ERROR, "FAIL", "I2S init failed: %s", esp_err_to_name(ret));
        s_init_in_progress = false;
        return false;
    }

    ret = es8311_init_codec();
    if (ret != ESP_OK) {
        set_ready_flags(false, true, amp_control_ready);
        set_status(FACTORY_AUDIO_MODE_ERROR, "FAIL", "ES8311 init failed: %s", esp_err_to_name(ret));
        s_init_in_progress = false;
        return false;
    }

    (void)audio_amp_set(false);
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
