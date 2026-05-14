#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_check.h"
#include "esp_log.h"

#include "bsp/bsp_board_extra.h"
#include "bsp/esp-bsp.h"

#include "board_es8311.h"
#include "config.h"

static const char *TAG = "es8311_spiffs";

esp_err_t board_audio_amp_set(bool enable)
{
    ESP_RETURN_ON_ERROR(t5_board_audio_select_speaker(true), TAG, "Select speaker path failed");
    ESP_RETURN_ON_ERROR(t5_board_audio_amp_enable(enable), TAG, "Set audio amplifier state failed");
    ESP_LOGI(TAG, "Audio amplifier %s through XL9555 P06", enable ? "enabled" : "disabled");
    return ESP_OK;
}

esp_err_t board_i2s_write(void *audio_buffer, size_t len, size_t *bytes_written, uint32_t timeout_ms)
{
    return bsp_extra_i2s_write(audio_buffer, len, bytes_written, timeout_ms);
}

esp_err_t board_i2s_reconfig_clk(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t slot_mode)
{
    return bsp_extra_codec_set_fs(rate, bits_cfg, slot_mode);
}

esp_err_t board_audio_player_mute(AUDIO_PLAYER_MUTE_SETTING setting)
{
    return bsp_extra_codec_mute_set(setting == AUDIO_PLAYER_MUTE);
}

esp_err_t board_audio_player_set_volume(int volume)
{
    return bsp_extra_codec_volume_set(volume, NULL);
}

esp_err_t board_audio_player_init(void)
{
    ESP_RETURN_ON_ERROR(bsp_extra_codec_init(), TAG, "Initialize codec path failed");
    ESP_RETURN_ON_ERROR(board_audio_amp_set(true), TAG, "Enable speaker path failed");
    ESP_RETURN_ON_ERROR(board_audio_player_set_volume(EXAMPLE_VOICE_VOLUME), TAG, "Apply output volume failed");
    ESP_RETURN_ON_ERROR(bsp_extra_player_init(), TAG, "Initialize audio player failed");
    return ESP_OK;
}

esp_err_t board_audio_player_deinit(void)
{
    esp_err_t ret = bsp_extra_player_del();
    ret |= bsp_extra_codec_dev_stop();
    ret |= board_audio_amp_set(false);
    return ret;
}
