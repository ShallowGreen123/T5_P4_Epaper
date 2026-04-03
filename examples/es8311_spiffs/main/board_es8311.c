#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_log.h"

#include "driver/i2c.h"
#include "driver/i2s_std.h"

#include "audio_player.h"
#include "es8311.h"

#include "board_es8311.h"
#include "config.h"

#define PCA9535_ADDR                 (0x20)
#define PCA9535_OUTPUT_PORT_0_REG    (0x02)
#define PCA9535_CONFIG_PORT_0_REG    (0x06)
#define PCA9535_AUDIO_SHUTDOWN_MASK  BIT(5)
#define AUDIO_AMP_STABLE_DELAY_MS    (10)

static const char *TAG = "es8311_spiffs";

static bool s_i2c_ready;
static bool s_player_ready;
static i2s_chan_handle_t s_tx_handle;
static es8311_handle_t s_es8311;

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
    for (size_t i = 0; i < len; ++i) {
        buffer[i + 1] = data[i];
    }

    return i2c_master_write_to_device(I2C_NUM, PCA9535_ADDR, buffer, len + 1, pdMS_TO_TICKS(I2C_OP_TIMEOUT_MS));
}

static esp_err_t board_audio_amp_enable(bool enable)
{
    const uint8_t output_state[2] = {enable ? PCA9535_AUDIO_SHUTDOWN_MASK : 0x00, 0x00};
    const uint8_t config_state[2] = {(uint8_t)~PCA9535_AUDIO_SHUTDOWN_MASK, 0xFF};

    ESP_RETURN_ON_ERROR(board_i2c_init(), TAG, "I2C init failed");
    ESP_RETURN_ON_ERROR(pca9535_write_registers(PCA9535_OUTPUT_PORT_0_REG, output_state, sizeof(output_state)),
                        TAG, "Write PCA9535 output state failed");
    ESP_RETURN_ON_ERROR(pca9535_write_registers(PCA9535_CONFIG_PORT_0_REG, config_state, sizeof(config_state)),
                        TAG, "Write PCA9535 direction state failed");

    vTaskDelay(pdMS_TO_TICKS(AUDIO_AMP_STABLE_DELAY_MS));
    return ESP_OK;
}

static esp_err_t board_i2s_init(uint32_t sample_rate)
{
    if (s_tx_handle != NULL) {
        return ESP_OK;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx_handle, NULL), TAG, "Create I2S TX channel failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(EXAMPLE_DEFAULT_BITS_PER_SAMPLE, EXAMPLE_DEFAULT_SLOT_MODE),
        .gpio_cfg = {
            .mclk = I2S_MCK_IO,
            .bclk = I2S_BCK_IO,
            .ws = I2S_WS_IO,
            .dout = I2S_DO_IO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = EXAMPLE_MCLK_MULTIPLE;

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx_handle, &std_cfg), TAG, "Init I2S TX failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx_handle), TAG, "Enable I2S TX failed");

    ESP_LOGI(TAG, "I2S TX ready: rate=%" PRIu32 " mclk=%d bclk=%d ws=%d dout=%d",
             sample_rate, I2S_MCK_IO, I2S_BCK_IO, I2S_WS_IO, I2S_DO_IO);
    return ESP_OK;
}

static esp_err_t board_es8311_init(uint32_t sample_rate)
{
    const es8311_clock_config_t es_clk = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = EXAMPLE_ES8311_MCLK_HZ(sample_rate),
        .sample_frequency = (int)sample_rate,
    };

    ESP_RETURN_ON_ERROR(board_audio_amp_enable(true), TAG, "Enable amplifier failed");

    if (s_es8311 == NULL) {
        s_es8311 = es8311_create(I2C_NUM, ES8311_ADDRRES_0);
    }
    ESP_RETURN_ON_FALSE(s_es8311 != NULL, ESP_FAIL, TAG, "Create ES8311 handle failed");

    ESP_RETURN_ON_ERROR(es8311_init(s_es8311, &es_clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16),
                        TAG, "Init ES8311 failed");
    ESP_RETURN_ON_ERROR(es8311_voice_volume_set(s_es8311, EXAMPLE_VOICE_VOLUME, NULL),
                        TAG, "Set ES8311 output volume failed");
    ESP_RETURN_ON_ERROR(es8311_voice_mute(s_es8311, false), TAG, "Unmute ES8311 failed");

    ESP_LOGI(TAG, "ES8311 initialized at %" PRIu32 " Hz", sample_rate);
    return ESP_OK;
}

esp_err_t board_i2s_write(void *audio_buffer, size_t len, size_t *bytes_written, uint32_t timeout_ms)
{
    if (s_tx_handle == NULL || audio_buffer == NULL || bytes_written == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return i2s_channel_write(s_tx_handle, audio_buffer, len, bytes_written, pdMS_TO_TICKS(timeout_ms));
}

esp_err_t board_i2s_reconfig_clk(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t slot_mode)
{
    if (s_tx_handle == NULL || s_es8311 == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(rate);
    i2s_std_slot_config_t slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG((i2s_data_bit_width_t)bits_cfg, slot_mode);
    clk_cfg.mclk_multiple = EXAMPLE_MCLK_MULTIPLE;

    ESP_RETURN_ON_ERROR(i2s_channel_disable(s_tx_handle), TAG, "Disable I2S TX failed");
    ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_clock(s_tx_handle, &clk_cfg), TAG, "Reconfig I2S clock failed");
    ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_slot(s_tx_handle, &slot_cfg), TAG, "Reconfig I2S slot failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx_handle), TAG, "Re-enable I2S TX failed");
    ESP_RETURN_ON_ERROR(es8311_sample_frequency_config(s_es8311, EXAMPLE_ES8311_MCLK_HZ(rate), rate),
                        TAG, "Reconfig ES8311 sample rate failed");

    ESP_LOGI(TAG, "Reconfigured audio clock: rate=%" PRIu32 ", bits=%" PRIu32 ", slot_mode=%d",
             rate, bits_cfg, (int)slot_mode);
    return ESP_OK;
}

esp_err_t board_audio_player_mute(AUDIO_PLAYER_MUTE_SETTING setting)
{
    if (s_es8311 == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return es8311_voice_mute(s_es8311, setting == AUDIO_PLAYER_MUTE);
}

esp_err_t board_audio_player_set_volume(int volume)
{
    if (s_es8311 == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return es8311_voice_volume_set(s_es8311, volume, NULL);
}

esp_err_t board_audio_player_init(void)
{
    if (s_player_ready) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(board_i2c_init(), TAG, "I2C init failed");
    ESP_RETURN_ON_ERROR(board_i2s_init(EXAMPLE_DEFAULT_SAMPLE_RATE), TAG, "I2S init failed");
    ESP_RETURN_ON_ERROR(board_es8311_init(EXAMPLE_DEFAULT_SAMPLE_RATE), TAG, "ES8311 init failed");

    const audio_player_config_t config = {
        .mute_fn = board_audio_player_mute,
        .clk_set_fn = board_i2s_reconfig_clk,
        .write_fn = board_i2s_write,
        .priority = 5,
        .coreID = tskNO_AFFINITY,
    };

    ESP_RETURN_ON_ERROR(audio_player_new(config), TAG, "Create audio player failed");
    ESP_RETURN_ON_ERROR(board_audio_player_set_volume(EXAMPLE_VOICE_VOLUME), TAG, "Apply volume failed");

    s_player_ready = true;
    return ESP_OK;
}

esp_err_t board_audio_player_deinit(void)
{
    esp_err_t ret = ESP_OK;

    if (s_player_ready) {
        ret |= audio_player_delete();
        s_player_ready = false;
    }

    if (s_tx_handle != NULL) {
        ret |= i2s_channel_disable(s_tx_handle);
        ret |= i2s_del_channel(s_tx_handle);
        s_tx_handle = NULL;
    }

    if (s_es8311 != NULL) {
        es8311_delete(s_es8311);
        s_es8311 = NULL;
    }

    if (s_i2c_ready) {
        ret |= board_audio_amp_enable(false);
        ret |= i2c_driver_delete(I2C_NUM);
        s_i2c_ready = false;
    }

    return ret;
}
