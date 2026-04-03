#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_log.h"

#include "driver/i2c.h"
#include "driver/i2s_std.h"

#include "es8311.h"

#include "board_es8311.h"
#include "config.h"

#define PCA9535_ADDR                 (0x20)
#define PCA9535_OUTPUT_PORT_0_REG    (0x02)
#define PCA9535_CONFIG_PORT_0_REG    (0x06)
#define AUDIO_AMP_STABLE_DELAY_MS    (10)

static const char *TAG = "es8311_mic_speak";

static i2s_chan_handle_t tx_handle;
static i2s_chan_handle_t rx_handle;
static es8311_handle_t s_es8311;
static bool s_i2c_ready;

static esp_err_t board_i2c_init(void)
{
    if (s_i2c_ready) {
        return ESP_OK;
    }

    const i2c_config_t i2c_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_IO,
        .scl_io_num = I2C_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_DISABLE,
        .scl_pullup_en = GPIO_PULLUP_DISABLE,
        .master.clk_speed = I2C_MASTER_CLK_SPEED,
    };

    ESP_RETURN_ON_ERROR(i2c_param_config(I2C_NUM, &i2c_cfg), TAG, "Configure I2C failed");
    ESP_RETURN_ON_ERROR(i2c_driver_install(I2C_NUM, i2c_cfg.mode, 0, 0, 0), TAG, "Install I2C driver failed");

    s_i2c_ready = true;
    ESP_LOGI(TAG, "I2C ready on SDA=%d SCL=%d", I2C_SDA_IO, I2C_SCL_IO);
    return ESP_OK;
}

static esp_err_t pca9535_write_registers(uint8_t start_reg, const uint8_t *data, size_t len)
{
    uint8_t buffer[3];

    if (data == NULL || len == 0 || len > 2) {
        return ESP_ERR_INVALID_ARG;
    }

    buffer[0] = start_reg;
    memcpy(&buffer[1], data, len);
    return i2c_master_write_to_device(I2C_NUM, PCA9535_ADDR, buffer, len + 1, pdMS_TO_TICKS(I2C_OP_TIMEOUT_MS));
}

esp_err_t board_audio_amp_set(bool enable)
{
    const uint8_t output_state[2] = {(uint8_t)(enable ? BOARD_PCA_05_SHUTDOWN : 0x00), 0x00};
    const uint8_t config_state[2] = {(uint8_t)~BOARD_PCA_05_SHUTDOWN, 0xFF};

    ESP_RETURN_ON_ERROR(board_i2c_init(), TAG, "I2C init failed");
    ESP_RETURN_ON_ERROR(pca9535_write_registers(PCA9535_OUTPUT_PORT_0_REG, output_state, sizeof(output_state)),
                        TAG, "Set PCA9535 output state failed");
    ESP_RETURN_ON_ERROR(pca9535_write_registers(PCA9535_CONFIG_PORT_0_REG, config_state, sizeof(config_state)),
                        TAG, "Set PCA9535 direction failed");

    vTaskDelay(pdMS_TO_TICKS(AUDIO_AMP_STABLE_DELAY_MS));
    ESP_LOGI(TAG, "Audio amplifier %s through PCA9535 IO5", enable ? "enabled" : "disabled");
    return ESP_OK;
}

static esp_err_t es8311_codec_init(void)
{
    const es8311_clock_config_t es_clk = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = EXAMPLE_MCLK_FREQ_HZ,
        .sample_frequency = EXAMPLE_SAMPLE_RATE,
    };

    ESP_RETURN_ON_ERROR(board_audio_amp_set(true), TAG, "Audio amplifier enable failed");

    if (s_es8311 == NULL) {
        s_es8311 = es8311_create(I2C_NUM, ES8311_ADDRRES_0);
    }
    ESP_RETURN_ON_FALSE(s_es8311 != NULL, ESP_FAIL, TAG, "Create ES8311 handle failed");

    ESP_RETURN_ON_ERROR(es8311_init(s_es8311, &es_clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16),
                        TAG, "Init ES8311 failed");
    ESP_RETURN_ON_ERROR(es8311_voice_volume_set(s_es8311, EXAMPLE_VOICE_VOLUME, NULL),
                        TAG, "Set ES8311 output volume failed");
    ESP_RETURN_ON_ERROR(es8311_microphone_config(s_es8311, false), TAG, "Enable ES8311 analog microphone failed");
    ESP_RETURN_ON_ERROR(es8311_microphone_gain_set(s_es8311, EXAMPLE_MIC_GAIN),
                        TAG, "Set ES8311 microphone gain failed");
    ESP_RETURN_ON_ERROR(es8311_voice_mute(s_es8311, false), TAG, "Unmute ES8311 output failed");

    ESP_LOGI(TAG, "ES8311 ready at %d Hz, volume=%d, mic_gain=%d",
             EXAMPLE_SAMPLE_RATE, EXAMPLE_VOICE_VOLUME, EXAMPLE_MIC_GAIN);
    return ESP_OK;
}

static esp_err_t i2s_driver_init(void)
{
    if (tx_handle != NULL && rx_handle != NULL) {
        return ESP_OK;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle), TAG, "Create I2S channels failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(EXAMPLE_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_MCK_IO,
            .bclk = I2S_BCK_IO,
            .ws = I2S_WS_IO,
            .dout = I2S_DO_IO,
            .din = I2S_DI_IO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = EXAMPLE_MCLK_MULTIPLE;

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx_handle, &std_cfg), TAG, "Init I2S TX failed");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(rx_handle, &std_cfg), TAG, "Init I2S RX failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(tx_handle), TAG, "Enable I2S TX failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(rx_handle), TAG, "Enable I2S RX failed");

    ESP_LOGI(TAG, "I2S ready: mclk=%d bclk=%d ws=%d dout=%d din=%d",
             I2S_MCK_IO, I2S_BCK_IO, I2S_WS_IO, I2S_DO_IO, I2S_DI_IO);
    return ESP_OK;
}

static void mic_loopback_task(void *args)
{
    uint8_t *audio_buffer = (uint8_t *)calloc(1, EXAMPLE_RECV_BUF_SIZE);
    size_t bytes_read = 0;
    size_t bytes_written = 0;

    if (audio_buffer == NULL) {
        ESP_LOGE(TAG, "Allocate audio buffer failed");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Microphone loopback started");

    while (1) {
        esp_err_t ret = i2s_channel_read(rx_handle, audio_buffer, EXAMPLE_RECV_BUF_SIZE, &bytes_read, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2S read failed: %s", esp_err_to_name(ret));
            continue;
        }

        if (bytes_read == 0) {
            continue;
        }

        ret = i2s_channel_write(tx_handle, audio_buffer, bytes_read, &bytes_written, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2S write failed: %s", esp_err_to_name(ret));
            continue;
        }

        if (bytes_read != bytes_written) {
            ESP_LOGW(TAG, "Loopback underrun: read=%u write=%u",
                     (unsigned)bytes_read, (unsigned)bytes_written);
        }
    }
}

void es8311_start(void)
{
    ESP_LOGI(TAG, "Starting ES8311 mic-to-speaker loopback");

    ESP_ERROR_CHECK(i2s_driver_init());
    ESP_ERROR_CHECK(es8311_codec_init());

    xTaskCreate(mic_loopback_task, "mic_loopback", 4096, NULL, 5, NULL);
}

